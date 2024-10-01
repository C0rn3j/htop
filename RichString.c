/*
htop - RichString.c
(C) 2004,2011 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "RichString.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h> // IWYU pragma: keep
#include <stdlib.h>
#include <string.h>

#include "Macros.h"
#include "XUtils.h"


#define charBytes(n) (sizeof(CharType) * (n))

static void RichString_extendLen(RichString* this, int len) {
   if (this->chptr == this->chstr) {
      // String is in internal buffer
      if (len > RICHSTRING_MAXLEN) {
         // Copy from internal buffer to allocated string
         this->chptr = xMalloc(charBytes(len + 1));
         memcpy(this->chptr, this->chstr, charBytes(this->chlen));
      } else {
         // Still fits in internal buffer, do nothing
         assert(this->chlen <= RICHSTRING_MAXLEN);
      }
   } else {
      // String is managed externally
      if (len > RICHSTRING_MAXLEN) {
         // Just reallocate the buffer accordingly
         this->chptr = xRealloc(this->chptr, charBytes(len + 1));
      } else {
         // Move string into internal buffer and free resources
         memcpy(this->chstr, this->chptr, charBytes(len));
         free(this->chptr);
         this->chptr = this->chstr;
      }
   }

   RichString_setChar(this, len, 0);
   this->chlen = len;
}

static void RichString_setLen(RichString* this, int len) {
   if (len < RICHSTRING_MAXLEN && this->chlen < RICHSTRING_MAXLEN) {
      RichString_setChar(this, len, 0);
      this->chlen = len;
   } else {
      RichString_extendLen(this, len);
   }
}

void RichString_rewind(RichString* this, int count) {
   RichString_setLen(this, this->chlen - count);
}

void RichString_setAttr_preserveWithStandout(RichString* this, attr_t attrs, int pair, void *opts) {
   RichString_setAttrn_preserveWithStandout(this, attrs, pair, opts, 0, this->chlen - 1);
}

#ifdef HAVE_LIBNCURSESW

static size_t mbstowcs_nonfatal(wchar_t* restrict dest, const char* restrict src, size_t n) {
   size_t written = 0;
   mbstate_t ps = { 0 };
   bool broken = false;

   while (n > 0) {
      size_t ret = mbrtowc(dest, src, n, &ps);
      if (ret == (size_t)-1 || ret == (size_t)-2) {
         if (!broken) {
            broken = true;
            *dest++ = L'\xFFFD';
            written++;
         }
         src++;
         n--;
         continue;
      }

      broken = false;

      if (ret == 0) {
         break;
      }

      dest++;
      written++;
      src += ret;
      n -= ret;
   }

   return written;
}

static inline int RichString_writeFromWide(RichString* this, int attrs, const char* data_c, int from, int len) {
   wchar_t data[len];
   len = mbstowcs_nonfatal(data, data_c, len);
   if (len <= 0)
      return 0;

   int newLen = from + len;
   RichString_setLen(this, newLen);
   for (int i = from, j = 0; i < newLen; i++, j++) {
      this->chptr[i] = (CharType) { .attr = attrs & 0xffffff, .chars = { (iswprint(data[j]) ? data[j] : L'\xFFFD') } };
   }

   return len;
}

int RichString_appendnWideColumns(RichString* this, int attrs, const char* data_c, int len, int* columns) {
   wchar_t data[len];
   len = mbstowcs_nonfatal(data, data_c, len);
   if (len <= 0)
      return 0;

   int from = this->chlen;
   int newLen = from + len;
   RichString_setLen(this, newLen);
   int columnsWritten = 0;
   int pos = from;
   for (int j = 0; j < len; j++) {
      wchar_t c = iswprint(data[j]) ? data[j] : L'\xFFFD';
      int cwidth = wcwidth(c);
      if (cwidth > *columns)
         break;

      *columns -= cwidth;
      columnsWritten += cwidth;

      this->chptr[pos] = (CharType) { .attr = attrs & 0xffffff, .chars = { c, '\0' } };
      pos++;
   }

   RichString_setLen(this, pos);
   *columns = columnsWritten;

   return pos - from;
}

static inline int RichString_writeFromAscii(RichString* this, int attrs, const char* data, int from, int len) {
   int newLen = from + len;
   RichString_setLen(this, newLen);
   for (int i = from, j = 0; i < newLen; i++, j++) {
      assert((unsigned char)data[j] <= SCHAR_MAX);
      this->chptr[i] = (CharType) { .attr = attrs & 0xffffff, .chars = { (isprint((unsigned char)data[j]) ? data[j] : L'\xFFFD') } };
   }

   return len;
}

inline void RichString_setAttrn(RichString* this, int attrs, int start, int charcount) {
   int end = CLAMP(start + charcount, 0, this->chlen);
   for (int i = start; i < end; i++) {
      this->chptr[i].attr = attrs;
   }
}

void RichString_setAttrn_preserveWithStandout(RichString* this, attr_t attrs, int pair, void *opts, int start, int finish) {
   finish = CLAMP(finish, 0, this->chlen - 1);

   // Extract the foreground and background color indexes from the passed pair
   int passed_color_pair_number = (int)pair;
   int passed_fg_color = -1, passed_bg_color = -1;
   int passed_bg_color_R = -1, passed_bg_color_G = -1, passed_bg_color_B = -1;
   int passed_fg_color_R = -1, passed_fg_color_G = -1, passed_fg_color_B = -1;


   int before_fg_color = 0, before_bg_color = 0;
   int before_fg_color_R = -1, before_fg_color_G = -1, before_fg_color_B = -1;

   if (passed_color_pair_number != 0) {
      extended_pair_content(passed_color_pair_number, &passed_fg_color, &passed_bg_color);
   }


   cchar_t* ch = this->chptr + start;
	 extended_color_content(passed_bg_color, &passed_bg_color_R, &passed_bg_color_G, &passed_bg_color_B);
   fprintf(stderr, "Passed BackGround RGB (color 201): R=%d, G=%d, B=%d REAL=%d \n", passed_bg_color_R, passed_bg_color_G, passed_bg_color_B, passed_bg_color);
	 extended_color_content(passed_bg_color, &passed_bg_color_R, &passed_bg_color_G, &passed_bg_color_B);
   fprintf(stderr, "Passed ForeGround RGB (color 201): R=%d, G=%d, B=%d\n", passed_fg_color_R, passed_fg_color_G, passed_fg_color_B);
	 extended_color_content(before_fg_color, &before_fg_color_R, &before_fg_color_G, &before_fg_color_B);
    fprintf(stderr, "Before ForeGround RGB (color 200): R=%d, G=%d, B=%d\n", before_fg_color_R, before_fg_color_G, before_fg_color_B);

   init_extended_color(201, passed_bg_color_R, passed_bg_color_G, passed_bg_color_B);
   for (int i = start; i <= finish; i++) {
      // Extract foreground and background color indexes from the current char
      int currentCharPairNum = PAIR_NUMBER(ch->attr);
      before_fg_color = 0, before_bg_color = 0;
      extended_pair_content(currentCharPairNum, &before_fg_color, &before_bg_color);
			if (before_bg_color == -1) before_bg_color = 0;
//      if (currentCharPairNum != 0) {
//         extended_pair_content(currentCharPairNum, &before_fg_color, &before_bg_color);
//      }
      // If we have a colored space, reset it to the default colorpair
//      if (currentCharPairNum != 0 && ch->chars[0] == L' ') {
//         ch->attr = 0;
//         before_fg_color, before_bg_color, currentCharPairNum = 0;
//      }

  //    init_extended_color(200, before_fg_color_R, before_fg_color_G, before_fg_color_B);
	//#define Black   COLOR_BLACK
  //#define Red     COLOR_RED
  //#define Green   COLOR_GREEN
  //#define Yellow  COLOR_YELLOW
  //#define Blue    COLOR_BLUE
  //#define Magenta COLOR_MAGENTA
  //#define Cyan    COLOR_CYAN
  //#define White   COLOR_WHITE

				   	// 1000 magic value used elsewhere, currently we have just 116, so unless we add 840 theme colors, this is fine
                // 1000
								//    ^ existing foreground
								//   ^  existing background
								//  ^   new background - action color (5) - dead process, new process, search, select and wahtevre the fifth one was

      int colorBGOverride = 1000 + before_fg_color + (before_bg_color*10) + (passed_bg_color*100);
       int after_fg_color = -1, after_bg_color = -1;
      extended_pair_content(colorBGOverride, &after_fg_color, &after_bg_color);

//      init_extended_pair(4000, 200, passed_bg_color);
//      attr_set(0, 4000, NULL);
			//attr_set(0, 62, NULL);
			//ColorPair(999, pair)

      // TODO: When text color matches higlight, the resulting STANDOUT is the same as on default text,
      //       so we at least set italics
//      chtype attrToPass = A_STANDOUT;
      // If current char is not a space and its ColorPair Index is not the default 0,
      //    apply our own attrToPass with STANDOUT + optionally ITALICS,
      //    instead of the passed pair, which has the BG highlight color
//      ch->attr = attrs;
      ch->ext_color = colorBGOverride;
			// Bold same BG and FG, looks bad, will need to fix text/bg colors instead
			if (after_fg_color == after_bg_color) {
				ch->attr |= A_BOLD;
			}
      //ch->attr = (ch->chars[0] != L' ' && currentCharPairNum != 0)
      //            ? (ch->attr | A_STANDOUT)
      //            : (unsigned int)pair;
      fprintf(stderr, "PAIRING before_fg_color=%d; before_bg_color=%d; passed_bg_color=%d; numero=%d \n", before_fg_color, before_bg_color,passed_bg_color, colorBGOverride );
      fprintf(stderr, "PAIRING after_fg_color=%d;  after_bg_color=%d;                      numero=%d \n\n", after_fg_color, after_bg_color, colorBGOverride );
      ch++;
   }

}

void RichString_appendChr(RichString* this, int attrs, char c, int count) {
   int from = this->chlen;
   int newLen = from + count;
   RichString_setLen(this, newLen);
   for (int i = from; i < newLen; i++) {
      this->chptr[i] = (CharType) { .attr = attrs, .chars = { c, 0 } };
   }
}

int RichString_findChar(const RichString* this, char c, int start) {
   const wchar_t wc = btowc(c);
   const cchar_t* ch = this->chptr + start;
   for (int i = start; i < this->chlen; i++) {
      if (ch->chars[0] == wc)
         return i;
      ch++;
   }
   return -1;
}

#else /* HAVE_LIBNCURSESW */

static inline int RichString_writeFromWide(RichString* this, int attrs, const char* data_c, int from, int len) {
   int newLen = from + len;
   RichString_setLen(this, newLen);
   for (int i = from, j = 0; i < newLen; i++, j++) {
      this->chptr[i] = (((unsigned char)data_c[j]) >= 32 ? ((unsigned char)data_c[j]) : '?') | attrs;
   }
   this->chptr[newLen] = 0;

   return len;
}

int RichString_appendnWideColumns(RichString* this, int attrs, const char* data_c, int len, int* columns) {
   int written = RichString_writeFromWide(this, attrs, data_c, this->chlen, MINIMUM(len, *columns));
   *columns = written;
   return written;
}

static inline int RichString_writeFromAscii(RichString* this, int attrs, const char* data_c, int from, int len) {
   return RichString_writeFromWide(this, attrs, data_c, from, len);
}

void RichString_setAttrn(RichString* this, int attrs, int start, int charcount) {
   int end = CLAMP(start + charcount, 0, this->chlen);
   for (int i = start; i < end; i++) {
      this->chptr[i] = (this->chptr[i] & 0xff) | attrs;
   }
}

void RichString_setAttrn_preserveWithStandout(RichString* this, int attrs, int start, int finish) {
   finish = CLAMP(finish, 0, this->chlen - 1);

   // Extract the foreground and background color indexes from the passed attrs
   short passed_color_pair_number = (short)PAIR_NUMBER(attrs);
   short passed_fg_color = -1, passed_bg_color = -1;
   if (passed_color_pair_number != 0) {
      pair_content(passed_color_pair_number, &passed_fg_color, &passed_bg_color);
   }

   chtype* ch = this->chptr + start;
   for (int i = start; i <= finish; i++) {
      // Extract foreground and background color indexes from the current char
      short currentCharPairNum = (short)PAIR_NUMBER(*ch);
      short before_fg_color = -1, before_bg_color = -1;
      if (currentCharPairNum != 0) {
         pair_content(currentCharPairNum, &before_fg_color, &before_bg_color);
      }

      // TODO: When text color matches higlight, the resulting STANDOUT is the same as on default text,
      //       so we at least set italics
      chtype attrToPass = A_STANDOUT;
      if (before_fg_color == passed_bg_color) {
         attrToPass |= A_ITALIC;
      }
      // If current char is not a space and its ColorPair Index is not the default 0,
      //    apply our own attrToPass with STANDOUT + optionally ITALICS,
      //    instead of the passed attrs, which has the BG highlight color
      *ch = ((*ch & A_CHARTEXT) != L' ' && currentCharPairNum != 0)
            ? *ch | attrToPass
            : (*ch & A_CHARTEXT) | (unsigned int)attrs;
      ch++;
   }
}

void RichString_appendChr(RichString* this, int attrs, char c, int count) {
   int from = this->chlen;
   int newLen = from + count;
   RichString_setLen(this, newLen);
   for (int i = from; i < newLen; i++) {
      this->chptr[i] = c | attrs;
   }
}

int RichString_findChar(const RichString* this, char c, int start) {
   const chtype* ch = this->chptr + start;
   for (int i = start; i < this->chlen; i++) {
      if ((*ch & 0xff) == (chtype) c)
         return i;
      ch++;
   }
   return -1;
}

#endif /* HAVE_LIBNCURSESW */

void RichString_delete(RichString* this) {
   if (this->chlen > RICHSTRING_MAXLEN) {
      free(this->chptr);
      this->chptr = this->chstr;
   }
}

void RichString_setAttr(RichString* this, int attrs) {
   RichString_setAttrn(this, attrs, 0, this->chlen);
}

int RichString_appendWide(RichString* this, int attrs, const char* data) {
   return RichString_writeFromWide(this, attrs, data, this->chlen, strlen(data));
}

int RichString_appendnWide(RichString* this, int attrs, const char* data, int len) {
   return RichString_writeFromWide(this, attrs, data, this->chlen, len);
}

int RichString_writeWide(RichString* this, int attrs, const char* data) {
   return RichString_writeFromWide(this, attrs, data, 0, strlen(data));
}

int RichString_appendAscii(RichString* this, int attrs, const char* data) {
   return RichString_writeFromAscii(this, attrs, data, this->chlen, strlen(data));
}

int RichString_appendnAscii(RichString* this, int attrs, const char* data, int len) {
   return RichString_writeFromAscii(this, attrs, data, this->chlen, len);
}

int RichString_writeAscii(RichString* this, int attrs, const char* data) {
   return RichString_writeFromAscii(this, attrs, data, 0, strlen(data));
}

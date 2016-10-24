/*
  This file is part of Deadbeef Player source code
  http://deadbeef.sourceforge.net

  utf8 string manipulation

  Copyright (C) 2009-2013 Alexey Yakovenko

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Alexey Yakovenko waker@users.sourceforge.net
*/

/* 
    based on Basic UTF-8 manipulation routines
    by Jeff Bezanson
    placed in the public domain Fall 2005
*/
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <alloca.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
//#include <alloca.h>
#include "ctype.h"
#include "utf8.h"
#include "u8_lc_map.h"
#include "u8_uc_map.h"

static const uint32_t offsetsFromUTF8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/* conversions without error checking
   only works for valid UTF-8, i.e. no 5- or 6-byte sequences
   srcsz = source size in bytes, or -1 if 0-terminated
   sz = dest size in # of wide characters

   returns # characters converted
   dest will always be L'\0'-terminated, even if there isn't enough room
   for all the characters.
   if sz = srcsz+1 (i.e. 4*srcsz+4 bytes), there will always be enough space.
*/
int u8_toucs(uint32_t *dest, int32_t sz, const char *src, int32_t srcsz)
{
    uint32_t ch;
    const char *src_end = src + srcsz;
    int32_t nb;
    int32_t i=0;

    while (i < sz-1) {
        nb = trailingBytesForUTF8[(unsigned char)*src];
        if (srcsz == -1) {
            if (*src == 0)
                goto done_toucs;
        }
        else {
            if (src + nb >= src_end)
                goto done_toucs;
        }
        ch = 0;
        switch (nb) {
            /* these fall through deliberately */
        case 3: ch += (unsigned char)*src++; ch <<= 6;
        case 2: ch += (unsigned char)*src++; ch <<= 6;
        case 1: ch += (unsigned char)*src++; ch <<= 6;
        case 0: ch += (unsigned char)*src++;
        }
        ch -= offsetsFromUTF8[nb];
        dest[i++] = ch;
    }
 done_toucs:
    dest[i] = 0;
    return i;
}

/* srcsz = number of source characters, or -1 if 0-terminated
   sz = size of dest buffer in bytes

   returns # characters converted
   dest will only be '\0'-terminated if there is enough space. this is
   for consistency; imagine there are 2 bytes of space left, but the next
   character requires 3 bytes. in this case we could NUL-terminate, but in
   general we can't when there's insufficient space. therefore this function
   only NUL-terminates if all the characters fit, and there's space for
   the NUL as well.
   the destination string will never be bigger than the source string.
*/
int u8_toutf8(char *dest, int32_t sz, uint32_t *src, int32_t srcsz)
{
    uint32_t ch;
    int32_t i = 0;
    char *dest_end = dest + sz;

    while (srcsz<0 ? src[i]!=0 : i < srcsz) {
        ch = src[i];
        if (ch < 0x80) {
            if (dest >= dest_end)
                return i;
            *dest++ = (char)ch;
        }
        else if (ch < 0x800) {
            if (dest >= dest_end-1)
                return i;
            *dest++ = (ch>>6) | 0xC0;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        else if (ch < 0x10000) {
            if (dest >= dest_end-2)
                return i;
            *dest++ = (ch>>12) | 0xE0;
            *dest++ = ((ch>>6) & 0x3F) | 0x80;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        else if (ch < 0x200000) {
            if (dest >= dest_end-3)
                return i;
            *dest++ = (ch>>18) | 0xF0;
            *dest++ = ((ch>>12) & 0x3F) | 0x80;
            *dest++ = ((ch>>6) & 0x3F) | 0x80;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        i++;
    }
    if (dest < dest_end)
        *dest = '\0';
    return i;
}

int u8_wc_toutf8(char *dest, uint32_t ch)
{
    if (ch < 0x80) {
        dest[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        dest[0] = (ch>>6) | 0xC0;
        dest[1] = (ch & 0x3F) | 0x80;
        return 2;
    }
    if (ch < 0x10000) {
        dest[0] = (ch>>12) | 0xE0;
        dest[1] = ((ch>>6) & 0x3F) | 0x80;
        dest[2] = (ch & 0x3F) | 0x80;
        return 3;
    }
    if (ch < 0x200000) {
        dest[0] = (ch>>18) | 0xF0;
        dest[1] = ((ch>>12) & 0x3F) | 0x80;
        dest[2] = ((ch>>6) & 0x3F) | 0x80;
        dest[3] = (ch & 0x3F) | 0x80;
        return 4;
    }
    return 0;
}

/* charnum => byte offset */
int u8_offset(char *str, int32_t charnum)
{
    int32_t offs=0;

    while (charnum > 0 && str[offs]) {
        (void)(isutf(str[++offs]) || isutf(str[++offs]) ||
               isutf(str[++offs]) || ++offs);
        charnum--;
    }
    return offs;
}

/* byte offset => charnum */
int u8_charnum(char *s, int32_t offset)
{
    int32_t charnum = 0, offs=0;

    while (offs < offset && s[offs]) {
        (void)(isutf(s[++offs]) || isutf(s[++offs]) ||
               isutf(s[++offs]) || ++offs);
        charnum++;
    }
    return charnum;
}

/* number of characters */
int u8_strlen(const char *s)
{
    int32_t count = 0;
    int32_t i = 0;

    while (u8_nextchar(s, &i) != 0)
        count++;

    return count;
}

/* reads the next utf-8 sequence out of a string, updating an index */
uint32_t u8_nextchar(const char *s, int32_t *i)
{
    uint32_t ch = 0;
    int32_t sz = 0;

    do {
        ch <<= 6;
        ch += (unsigned char)s[(*i)++];
        sz++;
    } while (s[*i] && !isutf(s[*i]));
    ch -= offsetsFromUTF8[sz-1];

    return ch;
}

/* copies num_chars characters from src to dest, return bytes written */
int u8_strncpy (char *dest, const char* src, int num_chars)
{
    const char *s = src;
    int32_t num_bytes = 0;
    while (num_chars && *s) {
        int32_t i = 0;
        u8_nextchar (s, &i);
        num_chars--;
        num_bytes += i;
        s += i;
    }
    strncpy (dest, src, s - src);
    dest[s - src] = 0;
    return num_bytes;
}

int u8_strnbcpy (char *dest, const char* src, int num_bytes) {
    int32_t prev_index = 0;
    int32_t index = 0;
    int32_t nb = num_bytes;
    while (src[index] && num_bytes > 0) {
        u8_inc (src, &index);
        int32_t charlen = index - prev_index;
        if (charlen > num_bytes) {
            break;
        }
        memcpy (dest, &src[prev_index], charlen);
        prev_index = index;
        dest += charlen;
        num_bytes -= charlen;
    }
    return nb - num_bytes;
}

int u8_charcpy (char *dest, const char *src, int num_bytes) {
    int32_t index = 0;
    u8_inc (src, &index);
    if (index > num_bytes) {
        return 0;
    }
    memcpy (dest, src, index);
    return index;
}

void u8_inc(const char *s, int32_t *i)
{
    (void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) ||
           isutf(s[++(*i)]) || ++(*i));
}

void u8_dec(const char *s, int32_t *i)
{
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) ||
           isutf(s[--(*i)]) || --(*i));
}

int octal_digit(char c)
{
    return (c >= '0' && c <= '7');
}

int hex_digit(char c)
{
    return ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f'));
}

/* assumes that src points to the character after a backslash
   returns number of input characters processed */
int u8_read_escape_sequence(const char *str, uint32_t *dest)
{
    uint32_t ch;
    char digs[]="\0\0\0\0\0\0\0\0\0";
    int32_t dno=0, i=1;

    ch = (uint32_t)str[0];    /* take literal character */
    if (str[0] == 'n')
        ch = L'\n';
    else if (str[0] == 't')
        ch = L'\t';
    else if (str[0] == 'r')
        ch = L'\r';
    else if (str[0] == 'b')
        ch = L'\b';
    else if (str[0] == 'f')
        ch = L'\f';
    else if (str[0] == 'v')
        ch = L'\v';
    else if (str[0] == 'a')
        ch = L'\a';
    else if (octal_digit(str[0])) {
        i = 0;
        do {
            digs[dno++] = str[i++];
        } while (octal_digit(str[i]) && dno < 3);
        ch = strtol(digs, NULL, 8);
    }
    else if (str[0] == 'x') {
        while (hex_digit(str[i]) && dno < 2) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = strtol(digs, NULL, 16);
    }
    else if (str[0] == 'u') {
        while (hex_digit(str[i]) && dno < 4) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = strtol(digs, NULL, 16);
    }
    else if (str[0] == 'U') {
        while (hex_digit(str[i]) && dno < 8) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = strtol(digs, NULL, 16);
    }
    *dest = ch;

    return i;
}

// convert a string with literal \uxxxx or \Uxxxxxxxx characters to UTF-8
// example: u8_unescape(mybuf, 256, "hello\\u220e")
// note the double backslash is needed if called on a C string literal
int u8_unescape(char *buf, int32_t sz, const char *src)
{
    int32_t c=0, amt;
    uint32_t ch;
    char temp[4];

    while (*src && c < sz) {
        if (*src == '\\') {
            src++;
            amt = u8_read_escape_sequence(src, &ch);
        }
        else {
            ch = (uint32_t)*src;
            amt = 1;
        }
        src += amt;
        amt = u8_wc_toutf8(temp, ch);
        if (amt > sz-c)
            break;
        memcpy(&buf[c], temp, amt);
        c += amt;
    }
    if (c < sz)
        buf[c] = '\0';
    return c;
}

int u8_escape_wchar(char *buf, int32_t sz, uint32_t ch)
{
    if (ch == L'\n')
        return snprintf(buf, sz, "\\n");
    else if (ch == L'\t')
        return snprintf(buf, sz, "\\t");
    else if (ch == L'\r')
        return snprintf(buf, sz, "\\r");
    else if (ch == L'\b')
        return snprintf(buf, sz, "\\b");
    else if (ch == L'\f')
        return snprintf(buf, sz, "\\f");
    else if (ch == L'\v')
        return snprintf(buf, sz, "\\v");
    else if (ch == L'\a')
        return snprintf(buf, sz, "\\a");
    else if (ch == L'\\')
        return snprintf(buf, sz, "\\\\");
    else if (ch < 32 || ch == 0x7f)
        return snprintf(buf, sz, "\\x%hhX", (unsigned char)ch);
    else if (ch > 0xFFFF)
        return snprintf(buf, sz, "\\U%.8X", (uint32_t)ch);
    else if (ch >= 0x80 && ch <= 0xFFFF)
        return snprintf(buf, sz, "\\u%.4hX", (unsigned short)ch);

    return snprintf(buf, sz, "%c", (char)ch);
}

int u8_escape(char *buf, int32_t sz, const char *src, int32_t escape_quotes)
{
    int32_t c=0, i=0, amt;

    while (src[i] && c < sz) {
        if (escape_quotes && src[i] == '"') {
            amt = snprintf(buf, sz - c, "\\\"");
            i++;
        }
        else {
            amt = u8_escape_wchar(buf, sz - c, u8_nextchar(src, &i));
        }
        c += amt;
        buf += amt;
    }
    if (c < sz)
        *buf = '\0';
    return c;
}

char *u8_strchr(char *s, uint32_t ch, int32_t *charn)
{
    int32_t i = 0, lasti=0;
    uint32_t c;

    *charn = 0;
    while (s[i]) {
        c = u8_nextchar(s, &i);
        if (c == ch) {
            return &s[lasti];
        }
        lasti = i;
        (*charn)++;
    }
    return NULL;
}

char *u8_memchr(char *s, uint32_t ch, size_t sz, int32_t *charn)
{
    int32_t i = 0, lasti=0;
    uint32_t c;
    int32_t csz;

    *charn = 0;
    while (i < sz) {
        c = csz = 0;
        do {
            c <<= 6;
            c += (unsigned char)s[i++];
            csz++;
        } while (i < sz && !isutf(s[i]));
        c -= offsetsFromUTF8[csz-1];

        if (c == ch) {
            return &s[lasti];
        }
        lasti = i;
        (*charn)++;
    }
    return NULL;
}

int u8_is_locale_utf8(char *locale)
{
    /* this code based on libutf8 */
    const char* cp = locale;

    for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++) {
        if (*cp == '.') {
            const char* encoding = ++cp;
            for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++)
                ;
            if ((cp-encoding == 5 && !strncmp(encoding, "UTF-8", 5))
                || (cp-encoding == 4 && !strncmp(encoding, "utf8", 4)))
                return 1; /* it's UTF-8 */
            break;
        }
    }
    return 0;
}

int u8_vprintf(char *fmt, va_list ap)
{
    int32_t cnt, sz=0;
    char *buf;
    uint32_t *wcs;

    sz = 512;
    buf = (char*)alloca(sz);
 try_print:
    cnt = vsnprintf(buf, sz, fmt, ap);
    if (cnt >= sz) {
        buf = (char*)alloca(cnt - sz + 1);
        sz = cnt + 1;
        goto try_print;
    }
    wcs = (uint32_t*)alloca((cnt+1) * sizeof(uint32_t));
    cnt = u8_toucs(wcs, cnt+1, buf, cnt);
    printf("%ls", (wchar_t*)wcs);
    return cnt;
}

int u8_printf(char *fmt, ...)
{
    int32_t cnt;
    va_list args;

    va_start(args, fmt);

    cnt = u8_vprintf(fmt, args);

    va_end(args);
    return cnt;
}

// adaptation of g_utf8_validate

#define UTF8_COMPUTE(Char, Mask, Len)                                         \
  if (Char < 128)                                                             \
    {                                                                         \
      Len = 1;                                                                \
      Mask = 0x7f;                                                            \
    }                                                                         \
  else if ((Char & 0xe0) == 0xc0)                                             \
    {                                                                         \
      Len = 2;                                                                \
      Mask = 0x1f;                                                            \
    }                                                                         \
  else if ((Char & 0xf0) == 0xe0)                                             \
    {                                                                         \
      Len = 3;                                                                \
      Mask = 0x0f;                                                            \
    }                                                                         \
  else if ((Char & 0xf8) == 0xf0)                                             \
    {                                                                         \
      Len = 4;                                                                \
      Mask = 0x07;                                                            \
    }                                                                         \
  else if ((Char & 0xfc) == 0xf8)                                             \
    {                                                                         \
      Len = 5;                                                                \
      Mask = 0x03;                                                            \
    }                                                                         \
  else if ((Char & 0xfe) == 0xfc)                                             \
    {                                                                         \
      Len = 6;                                                                \
      Mask = 0x01;                                                            \
    }                                                                         \
  else                                                                        \
    Len = -1;

#define UTF8_LENGTH(Char)              \
  ((Char) < 0x80 ? 1 :                 \
   ((Char) < 0x800 ? 2 :               \
    ((Char) < 0x10000 ? 3 :            \
     ((Char) < 0x200000 ? 4 :          \
      ((Char) < 0x4000000 ? 5 : 6)))))


#define UTF8_GET(Result, Chars, Count, Mask, Len)                             \
  (Result) = (Chars)[0] & (Mask);                                             \
  for ((Count) = 1; (Count) < (Len); ++(Count))                               \
    {                                                                         \
      if (((Chars)[(Count)] & 0xc0) != 0x80)                                  \
        {                                                                     \
          (Result) = -1;                                                      \
          break;                                                              \
        }                                                                     \
      (Result) <<= 6;                                                         \
      (Result) |= ((Chars)[(Count)] & 0x3f);                                  \
    }

#define UNICODE_VALID(Char)                   \
    ((Char) < 0x110000 &&                     \
     (((Char) & 0xFFFFF800) != 0xD800) &&     \
     ((Char) < 0xFDD0 || (Char) > 0xFDEF) &&  \
     ((Char) & 0xFFFE) != 0xFFFE)


int u8_valid (const char  *str,
        int max_len,
        const char **end)
{

    const char *p;

    if (!str) {
        return 0;
    }

    if (end)
        *end = str;

    p = str;

    while ((max_len < 0 || (p - str) < max_len) && *p)
    {
        int i, mask = 0, len;
        int32_t result;
        unsigned char c = (unsigned char) *p;

        UTF8_COMPUTE (c, mask, len);

        if (len == -1)
            break;

        /* check that the expected number of bytes exists in str */
        if (max_len >= 0 &&
                ((max_len - (p - str)) < len))
            break;

        UTF8_GET (result, p, i, mask, len);

        if (UTF8_LENGTH (result) != len) /* Check for overlong UTF-8 */
            break;

        if (result == (int32_t)-1)
            break;

        if (!UNICODE_VALID (result))
            break;

        p += len;
    }

    if (end)
        *end = p;

    /* See that we covered the entire length if a length was
     * passed in, or that we ended on a nul if not
     */
    if (max_len >= 0 && p != (str + max_len) && *p != 0) {
        return 0;
    }
    else if (max_len < 0 && *p != '\0') {
        return 0;
    }
    return 1;
}

#if 0
static const char lowerchars[] = "áéíñóúüäöåæøàçèêабвгдеёжзийклмнорпстуфхцчшщъыьэюя";
static const char upperchars[] = "ÁÉÍÑÓÚÜÄÖÅÆØÀÇÈÊАБВГДЕЁЖЗИЙКЛМНОРПСТУФХЦЧШЩЪЫЬЭЮЯ";
#endif

int
u8_tolower_slow (const char *input, int len, char *out) {
    struct u8_case_map_t *lc = u8_lc_in_word_set (input, len);
    if (lc) {
        int ll = strlen (lc->lower);
        memcpy (out, lc->lower, ll);
        out[ll] = 0;
        return ll;
    }
    return 0;
}

int
u8_tolower (const signed char *c, int l, char *out) {
    if (*c >= 65 && *c <= 90) {
        *out = *c + 0x20;
        out[1] = 0;
        return 1;
    }
    else if (*c > 0) {
        *out = *c;
        out[1] = 0;
        return 1;
    }
    else {
        int ll = u8_tolower_slow (c, l, out);
        if (ll) {
            return ll;
        }
        memcpy (out, c, l);
        out[l] = 0;
        return l;
    }
}

int
u8_toupper_slow (const char *input, int len, char *out) {
    struct u8_uppercase_map_t *uc = u8_uc_in_word_set (input, len);
    if (uc) {
        int ll = strlen (uc->upper);
        memcpy (out, uc->upper, ll);
        out[ll] = 0;
        return ll;
    }
    return 0;
}

int
u8_toupper (const signed char *c, int l, char *out) {
    if (*c >= 97 && *c <= 122) {
        *out = *c - 0x20;
        out[1] = 0;
        return 1;
    }
    else if (*c > 0) {
        *out = *c;
        out[1] = 0;
        return 1;
    }
    else {
        int ll = u8_toupper_slow (c, l, out);
        if (ll) {
            return ll;
        }
        memcpy (out, c, l);
        out[l] = 0;
        return l;
    }
}

const char *
utfcasestr (const char *s1, const char *s2) {
#if 0 // small u8_tolower test
    while (*s2) {
        int32_t i = 0;
        u8_nextchar (s2, &i);
        const char *next = s2 + i;
        char lw[10];
        int l = u8_tolower (s2, next-s2, lw);
        s2 = next;
        fprintf (stderr, "%s", lw);
    }
    fprintf (stderr, "\n");
    return NULL;
#endif
    while (*s1) {
        const char *p1 = s1;
        const char *p2 = s2;
        while (*p2 && *p1) {
            int32_t i1 = 0;
            int32_t i2 = 0;
            char lw1[10];
            char lw2[10];
            const char *next;
            u8_nextchar (p1, &i1);
            u8_nextchar (p2, &i2);
            int l1 = u8_tolower (p1, i1, lw1);
            int l2 = u8_tolower (p2, i2, lw2);
            //fprintf (stderr, "comparing %s to %s\n", lw1, lw2);
            if (strcmp (lw1, lw2)) {
                //fprintf (stderr, "fail\n");
                break;
            }
            p1 += i1;
            p2 += i2;
        }
        if (*p2 == 0) {
            //fprintf (stderr, "%s found in %s\n", s2, s1);
            return p1;
        }
        int32_t i = 0;
        u8_nextchar (s1, &i);
        s1 += i;
    }
    return NULL;
}

#define min(x,y) ((x)<(y)?(x):(y))
// s2 must be lowercase
const char *
utfcasestr_fast (const char *s1, const char *s2) {
    while (*s1) {
        const char *p1 = s1;
        const char *p2 = s2;
        while (*p2 && *p1) {
            int32_t i1 = 0;
            int32_t i2 = 0;
            char lw1[10];
            const char *next;
            u8_nextchar (p1, &i1);
            u8_nextchar (p2, &i2);
            int l1 = u8_tolower (p1, i1, lw1);
            if (memcmp (lw1, p2, min(i2,l1))) {
                break;
            }
            p1 += i1;
            p2 += i2;
        }
        if (*p2 == 0) {
            return p1;
        }
        int32_t i = 0;
        u8_nextchar (s1, &i);
        s1 += i;
    }
    return NULL;
}

int
u8_strcasecmp (const char *a, const char *b) {
    const char *p1 = a, *p2 = b;
    while (*p1 && *p2) {
        int32_t i1 = 0;
        int32_t i2 = 0;
        char s1[10], s2[10];
        const char *next;
        u8_nextchar (p1, &i1);
        u8_nextchar (p2, &i2);
        int l1 = u8_tolower (p1, i1, s1);
        int l2 = u8_tolower (p2, i2, s2);
        int res = 0;
        if (l1 != l2) {
            res = l1-l2;
        }
        else {
            res = memcmp (s1, s2, l1);
        }
        if (res) {
            return res;
        }
        p1 += i1;
        p2 += i2;
    }

    if (*p1) {
        return 1;
    }
    else if (*p2) {
        return -1;
    }

    return 0;
}

void
u8_lc_map_test (void) {
    struct u8_case_map_t *lc;
    lc = u8_lc_in_word_set ("Á", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("É", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("Í", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("Ñ", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("П", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("Л", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
    lc = u8_lc_in_word_set ("А", 2);
    printf ("%s -> %s\n", lc->name, lc->lower);
}

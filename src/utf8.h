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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

/* is c the start of a utf8 sequence? */
#define isutf(c) (((c)&0xC0)!=0x80)

/* convert UTF-8 data to wide character */
int u8_toucs(uint32_t *dest, int32_t sz, const char *src, int32_t srcsz);

/* the opposite conversion */
int u8_toutf8(char *dest, int32_t sz, uint32_t *src, int32_t srcsz);

/* single character to UTF-8 */
int u8_wc_toutf8(char *dest, uint32_t ch);

/* character number to byte offset */
int u8_offset(char *str, int32_t charnum);

/* byte offset to character number */
int u8_charnum(char *s, int32_t offset);

/* return next character, updating an index variable */
uint32_t u8_nextchar(const char *s, int32_t *i);

/* copies num_chars characters from src to dest, return bytes written */
int u8_strncpy (char *dest, const char* src, int num_chars);

/* copy num_bytes maximum bytes from src to dest, but always stop at the last possible utf8 character boundary;
 return number of bytes copied
 */
int u8_strnbcpy (char *dest, const char* src, int num_bytes);

/* copy single utf8 character of up to num_bytes bytes large, only if num_bytes is large enough;
  return number of bytes copied
 */
int u8_charcpy (char *dest, const char *src, int num_bytes);

/* move to next character */
void u8_inc(const char *s, int32_t *i);

/* move to previous character */
void u8_dec(const char *s, int32_t *i);

/* assuming src points to the character after a backslash, read an
   escape sequence, storing the result in dest and returning the number of
   input characters processed */
int u8_read_escape_sequence(const char *src, uint32_t *dest);

/* given a wide character, convert it to an ASCII escape sequence stored in
   buf, where buf is "sz" bytes. returns the number of characters output. */
int u8_escape_wchar(char *buf, int32_t sz, uint32_t ch);

/* convert a string "src" containing escape sequences to UTF-8 */
int u8_unescape(char *buf, int32_t sz, const char *src);

/* convert UTF-8 "src" to ASCII with escape sequences.
   if escape_quotes is nonzero, quote characters will be preceded by
   backslashes as well. */
int u8_escape(char *buf, int32_t sz, const char *src, int32_t escape_quotes);

/* utility predicates used by the above */
int octal_digit(char c);
int hex_digit(char c);

/* return a pointer to the first occurrence of ch in s, or NULL if not
   found. character index of found character returned in *charn. */
char *u8_strchr(char *s, uint32_t ch, int32_t *charn);

/* same as the above, but searches a buffer of a given size instead of
   a NUL-terminated string. */
char *u8_memchr(char *s, uint32_t ch, size_t sz, int32_t *charn);

/* count the number of characters in a UTF-8 string */
int u8_strlen(const char *s);

int u8_is_locale_utf8(char *locale);

/* printf where the format string and arguments may be in UTF-8.
   you can avoid this function and just use ordinary printf() if the current
   locale is UTF-8. */
int u8_vprintf(char *fmt, va_list ap);
int u8_printf(char *fmt, ...);

// validate utf8 string
// returns 1 if valid, 0 otherwise
int u8_valid (const char  *str,
        int max_len,
        const char **end);

int
u8_tolower (const signed char *c, int l, char *out);

int
u8_toupper (const signed char *c, int l, char *out);

int
u8_strcasecmp (const char *a, const char *b);

const char *
utfcasestr (const char *s1, const char *s2);

// s2 must be lowercase
const char *
utfcasestr_fast (const char *s1, const char *s2);

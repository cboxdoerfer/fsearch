/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <glib.h>
#include <string.h>
#include "string_utils.h"

bool
fs_str_is_empty (const char *str)
{
    // query is considered empty if:
    // - fist character is null terminator
    // - or it has only space characters
    while (*str != '\0') {
        if (!isspace (*str)) {
            return false;
        }
        str++;
    }
    return true;
}

int
fs_str_is_regex (const char *str)
{
    char regex_chars[] = {
        '$',
        '(',
        ')',
        '*',
        '+',
        '.',
        '?',
        '[',
        '\\',
        '^',
        '{',
        '|',
        '\0'
    };

    return (strpbrk(str, regex_chars) != NULL);
}

bool
fs_str_utf8_has_upper (char *str)
{
    char *p = str;
    if (!g_utf8_validate (p, -1, NULL)) {
        return false;
    }
    while (p && *p != '\0') {
        gunichar c = g_utf8_get_char (p);
        if (g_unichar_isupper (c)) {
            return true;
        }
        p = g_utf8_next_char (p);
    }
    return false;
}

bool
fs_str_has_upper (const char *strc)
{
    assert (strc != NULL);
    const char *ptr = strc;
    while (*ptr != '\0') {
        if (isupper (*ptr)) {
            return true;
        }
        ptr++;
    }
    return false;
}

char *
fs_str_copy (char *dest, char *end, const char *src)
{
    char *ptr = dest;
    while (ptr != end && *src != '\0') {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return ptr;
}


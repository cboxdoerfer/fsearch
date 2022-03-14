/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

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

#define G_LOG_DOMAIN "fsearch-string-utils"

#include "fsearch_string_utils.h"
#include <ctype.h>
#include <glib.h>
#include <string.h>

bool
fs_str_is_empty(const char *str) {
    // query is considered empty if:
    // - fist character is null terminator
    // - or it has only space characters
    g_assert(str);
    while (*str != '\0') {
        if (!isspace(*str)) {
            return false;
        }
        str++;
    }
    return true;
}

bool
fs_str_case_is_ascii(const char *str) {
    g_assert(str);
    const gssize str_len = (gssize)strlen(str);
    if (str_len == 0) {
        return true;
    }
    g_autofree char *down = g_utf8_strdown(str, str_len);
    g_autofree char *up = g_utf8_strup(str, str_len);

    if (g_str_is_ascii(down) && g_str_is_ascii(up)) {
        return true;
    }
    else {
        g_debug("[non_ascii_string] \"%s\" (down: \"%s\", up: \"%s\")", str, down, up);
        return false;
    }
}

bool
fs_str_utf8_has_upper(const char *str) {
    g_assert(str);
    char *p = (char *)str;
    if (!g_utf8_validate(p, -1, NULL)) {
        return false;
    }
    while (p && *p != '\0') {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isupper(c)) {
            return true;
        }
        p = g_utf8_next_char(p);
    }
    return false;
}

bool
fs_str_has_upper(const char *str) {
    g_assert(str);
    const char *ptr = str;
    while (*ptr != '\0') {
        if (isupper(*ptr)) {
            return true;
        }
        ptr++;
    }
    return false;
}

const char *
fs_str_get_extension(const char *str) {
    const char *ext = strrchr(str, '.');
    if (!ext || ext == str || ext[1] == '\0') {
        // filename has no dot
        // OR filename starts with dot (i.e. hidden file)
        // OR filename ends with dot
        return "";
    }
    return ext + 1;
}

char *
fs_str_convert_wildcard_to_regex_expression(const char *str) {
    g_assert(str);

    GString *regex_epxression = g_string_sized_new(strlen(str));
    g_string_append_c(regex_epxression, '^');
    const char *s = str;
    while (*s != '\0') {
        switch (*s) {
        case '.':
        case '^':
        case '$':
        case '+':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '\\':
        case '|':
            g_string_append_c(regex_epxression, '\\');
            g_string_append_c(regex_epxression, *s);
            break;
        case '*':
            g_string_append_c(regex_epxression, '.');
            g_string_append_c(regex_epxression, '*');
            break;
        case '?':
            g_string_append_c(regex_epxression, '.');
            break;
        default:
            g_string_append_c(regex_epxression, *s);
            break;
        }
        s++;
    }
    g_string_append_c(regex_epxression, '$');

    return g_string_free(regex_epxression, FALSE);
}

bool
fs_str_starts_with_range(char *str, char **end_ptr) {
    g_assert(str);
    g_assert(end_ptr);
    if (g_str_has_prefix(str, "..")) {
        *end_ptr = str + 2;
        return true;
    }
    else if (g_str_has_prefix(str, "-")) {
        *end_ptr = str + 1;
        return true;
    }
    *end_ptr = str;
    return false;
}

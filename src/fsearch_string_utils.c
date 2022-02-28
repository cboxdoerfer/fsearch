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
#include <assert.h>
#include <ctype.h>
#include <glib.h>
#include <string.h>

bool
fs_str_is_empty(const char *str) {
    // query is considered empty if:
    // - fist character is null terminator
    // - or it has only space characters
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
    const gssize str_len = (gssize)strlen(str);
    if (str_len == 0) {
        return true;
    }
    char *down = g_utf8_strdown(str, str_len);
    char *up = g_utf8_strup(str, str_len);

    bool ret = false;
    if (g_str_is_ascii(down) && g_str_is_ascii(up)) {
        ret = true;
    }
    else {
        g_debug("[non_ascii_string] \"%s\" (down: \"%s\", up: \"%s\")", str, down, up);
        ret = false;
    }

    g_clear_pointer(&down, g_free);
    g_clear_pointer(&up, g_free);
    return ret;
}

bool
fs_str_utf8_has_upper(const char *str) {
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
fs_str_has_upper(const char *strc) {
    assert(strc != NULL);
    const char *ptr = strc;
    while (*ptr != '\0') {
        if (isupper(*ptr)) {
            return true;
        }
        ptr++;
    }
    return false;
}

const char *
fs_str_get_extension(const char *file_name) {
    const char *ext = strrchr(file_name, '.');
    if (!ext || ext == file_name || ext[1] == '\0') {
        // filename has no dot
        // OR filename starts with dot (i.e. hidden file)
        // OR filename ends with dot
        return "";
    }
    return ext + 1;
}

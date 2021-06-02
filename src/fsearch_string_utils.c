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
fs_str_is_utf8(const char *str) {
    char *normalized = g_utf8_normalize(str, -1, G_NORMALIZE_DEFAULT);
    if (normalized == NULL) {
        return false;
    }
    char *down = g_utf8_strdown(normalized, -1);
    char *up = g_utf8_strup(normalized, -1);

    assert(down != NULL);
    assert(up != NULL);

    size_t str_len = strlen(str);
    size_t up_str_len = strlen(up);
    size_t down_str_len = strlen(down);
    size_t up_len = g_utf8_strlen(up, -1);
    size_t down_len = g_utf8_strlen(down, -1);

    g_free(down);
    g_free(up);
    g_free(normalized);
    down = NULL;
    up = NULL;

    if (str_len != up_len || str_len != down_len || up_str_len != up_len || down_str_len != down_len) {
        return true;
    }
    return false;
}

int
fs_str_is_regex(const char *str) {
    char regex_chars[] = {'$', '(', ')', '*', '+', '.', '?', '[', '\\', '^', '{', '|', '\0'};

    return (strpbrk(str, regex_chars) != NULL);
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
        return NULL;
    }
    return ext + 1;
}

char *
fs_str_copy(char *dest, char *end, const char *src) {
    char *ptr = dest;
    while (ptr != end && *src != '\0') {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return ptr;
}

static bool
is_nul(char p) {
    return p == '\0' ? true : false;
}

static char *
consume_space(char *str, bool *eos) {
    while (true) {
        if (is_nul(*str)) {
            *eos = true;
            return str;
        }
        if (*str == ' ') {
            str++;
            continue;
        }
        *eos = false;
        return str;
    }
}

static char *
consume_escape(char *str, char **dest, bool *eos) {
    char *d = *dest;
    if (is_nul(*str)) {
        *eos = true;
        return str;
    }

    *d = *str;
    d++;

    *dest = d;
    return str + 1;
}

char **
fs_str_split(const char *src) {
    if (!src) {
        return NULL;
    }

    GPtrArray *new = g_ptr_array_new();
    // Duplicate input string to make sure destination is large enough
    char *dest = g_strdup(src);
    char *s = dest;
    char *d = dest;
    bool inside_quotation_marks = false;
    bool eos = false;
    while (!eos) {
        switch (*s) {
        case '\0':
            eos = true;
            break;
        case '\\':
            s = consume_escape(s + 1, &d, &eos);
            break;
        case '"':
            s++;
            inside_quotation_marks = inside_quotation_marks ? false : true;
            break;
        case ' ':
            if (inside_quotation_marks) {
                *d = *s;
                d++;
                s++;
                break;
            }
            // split at space
            *d = '\0';
            d = dest;
            if (strlen(dest) > 0) {
                g_ptr_array_add(new, g_strdup(dest));
            }
            s = consume_space(s + 1, &eos);
            break;
        default:
            *d = *s;
            d++;
            s++;
            break;
        }
    }
    *d = '\0';
    if (strlen(dest) > 0) {
        g_ptr_array_add(new, g_strdup(dest));
    }

    // make sure last element is NULL
    g_ptr_array_add(new, NULL);

    g_free(dest);

    return (char **)g_ptr_array_free(new, FALSE);
}

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

#pragma once
#include <stdbool.h>
#include <unistd.h>

bool
fs_str_is_empty(const char *str);

bool
fs_str_has_upper(const char *str);

const char *
fs_str_get_extension(const char *str);

bool
fs_str_utf8_has_upper(const char *str);

// Detect if str is pure ascii characters in both its lower and upper case form.
bool
fs_str_case_is_ascii(const char *str);

// Converts a wildcard expression to a regular expression, i.e.
// `*` becomes `.*`
// `?` becomes `.`
// and properly escapes other valid regular expression tokens
char *
fs_str_convert_wildcard_to_regex_expression(const char *str);

// Detect if str starts with a range identifier (i.e. `..` or `-`).
// At success end_ptr will point to the first character after the range.
// If no range was detected end_ptr will point to str.
bool
fs_str_starts_with_range(char *str, char **end_ptr);

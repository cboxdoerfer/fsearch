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
fsearch_string_is_empty(const char *str);

bool
fsearch_string_has_upper(const char *str);

const char *
fsearch_string_get_extension(const char *str);

bool
fsearch_string_utf8_has_upper(const char *str);

// Detect if str is pure ascii characters in both its lower and upper case form.
bool
fsearch_string_is_ascii_icase(const char *str);

bool
fsearch_string_has_wildcards(const char *str);

// Converts a wildcard expression to a regular expression, i.e.
// `*` becomes `.*`
// `?` becomes `.`
// and properly escapes other valid regular expression tokens
char *
fsearch_string_convert_wildcard_to_regex_expression(const char *str);

// Detect if str starts with a interval identifier (i.e. `..` or `-`).
// At success end_ptr will point to the first character after the interval.
// If no interval was detected end_ptr will point to str.
bool
fsearch_string_starts_with_interval(char *str, char **end_ptr);

bool
fsearch_string_starts_with_date_interval(char *str, char **end_ptr);
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

int
fs_str_is_regex(const char *str);

bool
fs_str_is_empty(const char *str);

bool
fs_str_has_upper(const char *str);

bool
fs_str_utf8_has_upper(const char *str);

char *
fs_str_copy(char *dest, char *end, const char *src);

char **
fs_str_split(const char *str);

bool
fs_str_is_utf8(const char *str);


/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include <stdbool.h>

typedef enum FsearchQueryFlags {
    QUERY_FLAG_MATCH_CASE = 1 << 0,
    QUERY_FLAG_AUTO_MATCH_CASE = 1 << 1,
    QUERY_FLAG_REGEX = 1 << 2,
    QUERY_FLAG_SEARCH_IN_PATH = 1 << 3,
    QUERY_FLAG_AUTO_SEARCH_IN_PATH = 1 << 4,
    QUERY_FLAG_FILES_ONLY = 1 << 5,
    QUERY_FLAG_FOLDERS_ONLY = 1 << 6,
    QUERY_FLAG_EXACT_MATCH = 1 << 7,
} FsearchQueryFlags;
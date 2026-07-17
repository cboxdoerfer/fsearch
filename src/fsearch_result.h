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

typedef enum {
    FSEARCH_RESULT_SUCCESS,
    FSEARCH_RESULT_FAILED,
    FSEARCH_RESULT_DB_FILE_OUTDATED,
    FSEARCH_RESULT_DB_FILE_MALFORMED,
    FSEARCH_RESULT_DB_BUSY,
    FSEARCH_RESULT_DB_UNKNOWN_SEARCH_VIEW,
    FSEARCH_RESULT_DB_ENTRY_NOT_FOUND,
} FsearchResult;
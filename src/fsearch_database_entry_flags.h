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

typedef enum FsearchDatabaseEntryFlags {
    FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER = 1 << 0,
    FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FILE = 1 << 1,
    FSEARCH_DATABASE_ENTRY_FLAG_MARKED = 1 << 2,
    FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_INOTIFY = 1 << 3,
    FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FANOTIFY = 1 << 4,
    FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FAILED = 1 << 5,
} FsearchDatabaseEntryFlags;
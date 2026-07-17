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
    FSEARCH_DATABASE_SCAN_REASON_UNKNOWN = 0,
    FSEARCH_DATABASE_SCAN_REASON_MANUAL = 1,
    FSEARCH_DATABASE_SCAN_REASON_SCHEDULED = 2,
    FSEARCH_DATABASE_SCAN_REASON_LAUNCH = 3,
    FSEARCH_DATABASE_SCAN_REASON_MONITOR_DROP = 4,
    FSEARCH_DATABASE_SCAN_REASON_CONFIG_CHANGE = 5,
    NUM_FSEARCH_DATABASE_SCAN_REASONS
} FsearchDatabaseScanReason;

typedef enum {
    FSEARCH_DATABASE_REBUILD_UNKNOWN = 0,
    FSEARCH_DATABASE_REBUILD_FRESH_INSTALL = 1,
    FSEARCH_DATABASE_REBUILD_FORMAT_UPGRADE = 2,
    FSEARCH_DATABASE_REBUILD_CORRUPTION_RECOVERY = 3,
    FSEARCH_DATABASE_REBUILD_USER_CLEARED = 4,
    NUM_DATABASE_REBUILD_REASONS
} FsearchDatabaseRebuildReason;
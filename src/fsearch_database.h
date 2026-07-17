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

#include <gio/gio.h>

#include "fsearch_database_entry_info.h"
#include "fsearch_database_info.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_work.h"
#include "fsearch_result.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE fsearch_database_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabase, fsearch_database, FSEARCH, DATABASE, GObject)

typedef void
(*FsearchDatabaseForeachFunc)(FsearchDatabaseEntry *entry, gpointer user_data);

void
fsearch_database_queue_work(FsearchDatabase *self, FsearchDatabaseWork *work);

// Cancels the most recently queued scan (SCAN/RESCAN/RESCAN_INDEX), however it was triggered.
// No-op if none is pending. Only the filesystem walk can be aborted, not applying its results.
void
fsearch_database_cancel_scan(FsearchDatabase *self);

FsearchResult
fsearch_database_try_get_search_info(FsearchDatabase *self, uint32_t view_id, FsearchDatabaseSearchInfo **info_out);

FsearchResult
fsearch_database_try_get_database_info(FsearchDatabase *self, FsearchDatabaseInfo * *info_out);

FsearchResult
fsearch_database_rescan_blocking(FsearchDatabase *self);

void
fsearch_database_selection_foreach(FsearchDatabase *self,
                                   uint32_t view_id,
                                   FsearchDatabaseForeachFunc func,
                                   gpointer user_data);

FsearchResult
fsearch_database_try_get_item_info(FsearchDatabase *self,
                                   uint32_t view_id,
                                   uint32_t idx,
                                   FsearchDatabaseEntryInfoFlags flags,
                                   FsearchDatabaseEntryInfo **info_out);

FsearchDatabase *
fsearch_database_new(GFile *file,
                     FsearchDatabaseIncludeManager *include_manager,
                     FsearchDatabaseExcludeManager *exclude_manager);

G_END_DECLS
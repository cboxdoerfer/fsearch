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
*/

#pragma once

#include "fsearch_array.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_query.h"
#include "fsearch_selection_type.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <stdint.h>

typedef struct FsearchDatabaseWork FsearchDatabaseWork;

typedef enum FsearchDatabaseWorkKind {
    FSEARCH_DATABASE_WORK_LOAD_FROM_FILE,
    FSEARCH_DATABASE_WORK_RESCAN,
    FSEARCH_DATABASE_WORK_RESCAN_INDEX,
    FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED,
    FSEARCH_DATABASE_WORK_SAVE_TO_FILE,
    FSEARCH_DATABASE_WORK_SCAN,
    FSEARCH_DATABASE_WORK_SCAN_FINISHED,
    FSEARCH_DATABASE_WORK_SEARCH,
    FSEARCH_DATABASE_WORK_SORT,
    FSEARCH_DATABASE_WORK_GET_ITEM_INFO,
    FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED,
    FSEARCH_DATABASE_WORK_MODIFY_SELECTION,
    FSEARCH_DATABASE_WORK_QUIT,
    NUM_FSEARCH_DATABASE_WORK_KINDS,
} FsearchDatabaseWorkKind;

FsearchDatabaseWork *
fsearch_database_work_ref(FsearchDatabaseWork *work);

void
fsearch_database_work_unref(FsearchDatabaseWork *work);

FsearchDatabaseWork *
fsearch_database_work_new_quit(void);

FsearchDatabaseWork *
fsearch_database_work_new_rescan(void);

FsearchDatabaseWork *
fsearch_database_work_new_rescan_index(const char *root_path);

FsearchDatabaseWork *
fsearch_database_work_new_rescan_index_finished(FsearchDatabaseIndex *new_index, GCancellable *cancellable);

const char *
fsearch_database_work_rescan_index_get_path(FsearchDatabaseWork *work);

FsearchDatabaseIndex *
fsearch_database_work_rescan_index_finished_get_index(FsearchDatabaseWork *work);

DynamicArray *
fsearch_database_work_notify_items_removed_get_item_paths(FsearchDatabaseWork *work);

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexPropertyFlags flags);

FsearchDatabaseWork *
fsearch_database_work_new_scan_finished(void *index_store,
                                        void *(*index_ref_func)(void *),
                                        void (*index_free_func)(void *),
                                        GCancellable *cancellable);

FsearchDatabaseWork *
fsearch_database_work_new_modify_selection(guint view_id,
                                           FsearchSelectionType selection_type,
                                           int32_t idx_1,
                                           int32_t idx_2);

FsearchDatabaseWork *
fsearch_database_work_new_search(guint view_id,
                                 FsearchQuery *query,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type);

FsearchDatabaseWork *
fsearch_database_work_new_sort(guint view_id, FsearchDatabaseIndexProperty sort_order, GtkSortType sort_type);

FsearchDatabaseWork *
fsearch_database_work_new_get_item_info(guint view_id, guint index, FsearchDatabaseEntryInfoFlags flags);

FsearchDatabaseWork *
fsearch_database_work_new_notify_items_removed(DynamicArray *item_paths);

FsearchDatabaseWork *
fsearch_database_work_new_load(void);

FsearchDatabaseWork *
fsearch_database_work_new_save(void);

FsearchDatabaseWorkKind
fsearch_database_work_get_kind(FsearchDatabaseWork *work);

GCancellable *
fsearch_database_work_get_cancellable(FsearchDatabaseWork *work);

void
fsearch_database_work_cancel(FsearchDatabaseWork *work);

guint
fsearch_database_work_get_view_id(FsearchDatabaseWork *work);

int32_t
fsearch_database_work_modify_selection_get_start_idx(FsearchDatabaseWork *work);

int32_t
fsearch_database_work_modify_selection_get_end_idx(FsearchDatabaseWork *work);

FsearchSelectionType
fsearch_database_work_modify_selection_get_type(FsearchDatabaseWork *work);

FsearchQuery *
fsearch_database_work_search_get_query(FsearchDatabaseWork *work);

FsearchDatabaseIndexProperty
fsearch_database_work_search_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_search_get_sort_type(FsearchDatabaseWork *work);

FsearchDatabaseIndexProperty
fsearch_database_work_sort_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_sort_get_sort_type(FsearchDatabaseWork *work);

void *
fsearch_database_work_scan_finished_get_index_store(FsearchDatabaseWork *work);

FsearchDatabaseIncludeManager *
fsearch_database_work_scan_get_include_manager(FsearchDatabaseWork *work);

FsearchDatabaseExcludeManager *
fsearch_database_work_scan_get_exclude_manager(FsearchDatabaseWork *work);

FsearchDatabaseIndexPropertyFlags
fsearch_database_work_scan_get_flags(FsearchDatabaseWork *work);

guint
fsearch_database_work_item_info_get_index(FsearchDatabaseWork *work);

FsearchDatabaseEntryInfoFlags
fsearch_database_work_item_info_get_flags(FsearchDatabaseWork *work);

const char *
fsearch_database_work_to_string(FsearchDatabaseWork *work);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseWork, fsearch_database_work_unref)
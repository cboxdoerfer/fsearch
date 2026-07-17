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

#include "fsearch_database_work.h"

#include "fsearch_array.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_query.h"
#include "fsearch_selection_type.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <stdint.h>
#include <stdlib.h>

struct FsearchDatabaseWork {
    FsearchDatabaseWorkKind kind;

    union {
        // FSEARCH_DATABASE_WORK_SCAN
        struct {
            FsearchDatabaseIncludeManager *include_manager;
            FsearchDatabaseExcludeManager *exclude_manager;
            FsearchDatabaseIndexPropertyFlags index_flags;
        };

        // FSEARCH_DATABASE_WORK_SCAN_FINISHED
        struct {
            void *index_store;
            void *(*index_store_ref_func)(void *);
            void (*index_store_free_func)(void *);
        };

        // FSEARCH_DATABASE_WORK_SEARCH
        struct {
            FsearchQuery *query;
            FsearchDatabaseIndexProperty sort_order;
            GtkSortType sort_type;
        };

        // FSEARCH_DATABASE_WORK_GET_ITEM_INFO
        struct {
            guint idx;
            FsearchDatabaseEntryInfoFlags entry_info_flags;
        };

        // FSEARCH_DATABASE_WORK_MODIFY_SELECTION
        struct {
            FsearchSelectionType selection_type;
            int32_t idx_1;
            int32_t idx_2;
        };

        // FSEARCH_DATABASE_WORK_RESCAN_INDEX
        struct {
            GString *root_path;
        };

        // FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED
        struct {
            FsearchDatabaseIndex *rescan_new_index;
        };
        // FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED
        struct {
            DynamicArray *item_paths;
        };
    };

    guint view_id;

    GCancellable *cancellable;

    volatile gint ref_count;
};

static FsearchDatabaseWork *
work_new(void) {
    FsearchDatabaseWork *work = calloc(1, sizeof(FsearchDatabaseWork));
    g_assert(work);

    work->cancellable = g_cancellable_new();

    work->ref_count = 1;

    return work;
}

static void
work_free(FsearchDatabaseWork *work) {
    switch (work->kind) {
    case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
    case FSEARCH_DATABASE_WORK_GET_ITEM_INFO:
    case FSEARCH_DATABASE_WORK_RESCAN:
    case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
    case FSEARCH_DATABASE_WORK_SORT:
    case FSEARCH_DATABASE_WORK_MODIFY_SELECTION:
    case FSEARCH_DATABASE_WORK_QUIT:
        break;
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX:
        g_string_free(g_steal_pointer(&work->root_path), TRUE);
    case FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED:
        g_clear_pointer(&work->item_paths, darray_unref);
        break;
    case FSEARCH_DATABASE_WORK_SCAN:
        g_clear_object(&work->include_manager);
        g_clear_object(&work->exclude_manager);
        break;
    case FSEARCH_DATABASE_WORK_SCAN_FINISHED:
        g_clear_pointer(&work->index_store, work->index_store_free_func);
    case FSEARCH_DATABASE_WORK_SEARCH:
        g_clear_pointer(&work->query, fsearch_query_unref);
        break;
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED:
        g_clear_pointer(&work->rescan_new_index, fsearch_database_index_unref);
        break;
    case NUM_FSEARCH_DATABASE_WORK_KINDS:
        g_assert_not_reached();
    }

    g_clear_object(&work->cancellable);

    g_clear_pointer(&work, free);
}

FsearchDatabaseWork *
fsearch_database_work_ref(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&work->ref_count) > 0, NULL);

    g_atomic_int_inc(&work->ref_count);

    return work;
}

void
fsearch_database_work_unref(FsearchDatabaseWork *work) {
    g_return_if_fail(work != NULL);
    g_return_if_fail(g_atomic_int_get(&work->ref_count) > 0);

    if (g_atomic_int_dec_and_test(&work->ref_count)) {
        g_clear_pointer(&work, work_free);
    }
}

FsearchDatabaseWork *
fsearch_database_work_new_quit(void) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_QUIT;
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_rescan(void) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_RESCAN;
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_rescan_index(const char *root_path) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_RESCAN_INDEX;
    work->root_path = g_string_new(root_path);
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_rescan_index_finished(FsearchDatabaseIndex *new_index, GCancellable *cancellable) {
    g_return_val_if_fail(new_index, NULL);
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED;
    work->rescan_new_index = fsearch_database_index_ref(new_index);
    if (cancellable) {
        // Carry forward the cancellable of the work item that requested this rescan
        g_set_object(&work->cancellable, cancellable);
    }
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SCAN;
    work->include_manager = g_object_ref(include_manager);
    work->exclude_manager = g_object_ref(exclude_manager);
    work->index_flags = flags;
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_scan_finished(void *index_store,
                                        void *(*index_ref_func)(void *),
                                        void (*index_free_func)(void *),
                                        GCancellable *cancellable) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SCAN_FINISHED;
    work->index_store = index_ref_func(index_store);
    work->index_store_ref_func = index_ref_func;
    work->index_store_free_func = index_free_func;
    if (cancellable) {
        g_set_object(&work->cancellable, cancellable);
    }
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_modify_selection(guint view_id, FsearchSelectionType selection_type, int32_t idx_1, int32_t idx_2) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_MODIFY_SELECTION;
    work->view_id = view_id;
    work->selection_type = selection_type;
    work->idx_1 = idx_1;
    work->idx_2 = idx_2;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_search(guint view_id,
                                 FsearchQuery *query,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type) {
    g_return_val_if_fail(query, NULL);

    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SEARCH;
    work->view_id = view_id;
    work->sort_order = sort_order;
    work->sort_type = sort_type;
    work->query = fsearch_query_ref(query);

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_sort(guint view_id, FsearchDatabaseIndexProperty sort_order, GtkSortType sort_type) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SORT;
    work->view_id = view_id;
    work->sort_order = sort_order;
    work->sort_type = sort_type;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_get_item_info(guint view_id, guint idx, FsearchDatabaseEntryInfoFlags flags) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_GET_ITEM_INFO;
    work->entry_info_flags = flags;
    work->idx = idx;
    work->view_id = view_id;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_notify_items_removed(DynamicArray *item_paths) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED;
    work->item_paths = darray_ref(item_paths);

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_load(void) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_LOAD_FROM_FILE;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_save(void) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SAVE_TO_FILE;

    return work;
}

guint
fsearch_database_work_get_view_id(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH || work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION
                             || work->kind == FSEARCH_DATABASE_WORK_SORT
                             || work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO,
                         0);
    return work->view_id;
}

FsearchDatabaseWorkKind
fsearch_database_work_get_kind(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_FSEARCH_DATABASE_WORK_KINDS);
    return work->kind;
}

GCancellable *
fsearch_database_work_get_cancellable(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    return g_object_ref(work->cancellable);
}

void
fsearch_database_work_cancel(FsearchDatabaseWork *work) {
    g_return_if_fail(work);
    g_cancellable_cancel(work->cancellable);
}

FsearchQuery *
fsearch_database_work_search_get_query(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, NULL);
    return fsearch_query_ref(work->query);
}

FsearchDatabaseIndexProperty
fsearch_database_work_search_get_sort_order(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_DATABASE_INDEX_PROPERTIES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, NUM_DATABASE_INDEX_PROPERTIES);
    return work->sort_order;
}

GtkSortType
fsearch_database_work_search_get_sort_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, GTK_SORT_ASCENDING);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, GTK_SORT_ASCENDING);
    return work->sort_type;
}

FsearchDatabaseIndexProperty
fsearch_database_work_sort_get_sort_order(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_DATABASE_INDEX_PROPERTIES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SORT, NUM_DATABASE_INDEX_PROPERTIES);
    return work->sort_order;
}

GtkSortType
fsearch_database_work_sort_get_sort_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, GTK_SORT_ASCENDING);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SORT, GTK_SORT_ASCENDING);
    return work->sort_type;
}

void *
fsearch_database_work_scan_finished_get_index_store(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN_FINISHED, NULL);
    return work->index_store_ref_func(work->index_store);
}

FsearchDatabaseIncludeManager *
fsearch_database_work_scan_get_include_manager(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, NULL);
    return g_object_ref(work->include_manager);
}

FsearchDatabaseExcludeManager *
fsearch_database_work_scan_get_exclude_manager(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, NULL);
    return g_object_ref(work->exclude_manager);
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_work_scan_get_flags(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, 0);
    return work->index_flags;
}

guint
fsearch_database_work_item_info_get_index(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO, 0);
    return work->idx;
}

FsearchDatabaseEntryInfoFlags
fsearch_database_work_item_info_get_flags(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO, 0);
    return work->entry_info_flags;
}

int32_t
fsearch_database_work_modify_selection_get_start_idx(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, 0);
    return work->idx_1;
}

int32_t
fsearch_database_work_modify_selection_get_end_idx(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, 0);
    return work->idx_2;
}

FsearchSelectionType
fsearch_database_work_modify_selection_get_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_FSEARCH_SELECTION_TYPES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, NUM_FSEARCH_SELECTION_TYPES);
    return work->selection_type;
}

const char *
fsearch_database_work_rescan_index_get_path(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_RESCAN_INDEX, 0);
    return work->root_path->str;
}

FsearchDatabaseIndex *
fsearch_database_work_rescan_index_finished_get_index(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED, NULL);
    return fsearch_database_index_ref(work->rescan_new_index);
}

DynamicArray *
fsearch_database_work_notify_items_removed_get_item_paths(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED, NULL);
    return darray_ref(work->item_paths);
}

const char *
fsearch_database_work_to_string(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, "NULL");

    switch (work->kind) {
    case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
        return "LOAD_FROM_FILE";
    case FSEARCH_DATABASE_WORK_RESCAN:
        return "RESCAN";
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX:
        return "RESCAN_INDEX";
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED:
        return "RESCAN_INDEX_FINISHED";
    case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
        return "SAVE_TO_FILE";
    case FSEARCH_DATABASE_WORK_SCAN:
        return "SCAN";
    case FSEARCH_DATABASE_WORK_SCAN_FINISHED:
        return "SCAN_FINISHED";
    case FSEARCH_DATABASE_WORK_SEARCH:
        return "SEARCH";
    case FSEARCH_DATABASE_WORK_SORT:
        return "SORT";
    case FSEARCH_DATABASE_WORK_GET_ITEM_INFO:
        return "GET_ITEM_INFO";
    case FSEARCH_DATABASE_WORK_MODIFY_SELECTION:
        return "MODIFY_SELECTION";
    case FSEARCH_DATABASE_WORK_QUIT:
        return "QUIT";
    default:
        return "UNKNOWN";
    }
}
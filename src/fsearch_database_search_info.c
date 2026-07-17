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

#include "fsearch_database_search_info.h"

struct _FsearchDatabaseSearchInfo {
    FsearchQuery *query;
    uint32_t id;
    uint32_t num_files;
    uint32_t num_folders;
    uint32_t num_files_selected;
    uint32_t num_folders_selected;
    GtkSortType sort_type;
    FsearchDatabaseIndexProperty sort_order;
    bool is_complete;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseSearchInfo,
                    fsearch_database_search_info,
                    fsearch_database_search_info_ref,
                    fsearch_database_search_info_unref)

FsearchDatabaseSearchInfo *
fsearch_database_search_info_ref(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&info->ref_count) > 0, NULL);

    g_atomic_int_inc(&info->ref_count);

    return info;
}

void
fsearch_database_search_info_unref(FsearchDatabaseSearchInfo *info) {
    g_return_if_fail(info != NULL);
    g_return_if_fail(g_atomic_int_get(&info->ref_count) > 0);

    if (g_atomic_int_dec_and_test(&info->ref_count)) {
        g_clear_pointer(&info->query, fsearch_query_unref);
        g_clear_pointer(&info, g_free);
    }
}

FsearchDatabaseSearchInfo *
fsearch_database_search_info_new(uint32_t id,
                                 FsearchQuery *query,
                                 uint32_t num_files,
                                 uint32_t num_folders,
                                 uint32_t num_files_selected,
                                 uint32_t num_folders_selected,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type,
                                 bool is_complete) {
    FsearchDatabaseSearchInfo *info = calloc(1, sizeof(FsearchDatabaseSearchInfo));
    g_assert(info);

    info->id = id;
    info->query = fsearch_query_ref(query);
    info->num_files = num_files;
    info->num_folders = num_folders;
    info->num_files_selected = num_files_selected;
    info->num_folders_selected = num_folders_selected;
    info->sort_order = sort_order;
    info->sort_type = sort_type;
    info->is_complete = is_complete;

    info->ref_count = 1;

    return info;
}

uint32_t
fsearch_database_search_info_get_id(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->id;
}

uint32_t
fsearch_database_search_info_get_num_files(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files;
}

uint32_t
fsearch_database_search_info_get_num_folders(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_folders;
}

uint32_t
fsearch_database_search_info_get_num_files_selected(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files_selected;
}

uint32_t
fsearch_database_search_info_get_num_folders_selected(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_folders_selected;
}

uint32_t
fsearch_database_search_info_get_num_entries(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files + info->num_folders;
}

uint32_t
fsearch_database_search_info_get_num_entries_selected(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files_selected + info->num_folders_selected;
}

FsearchDatabaseIndexProperty
fsearch_database_search_info_get_sort_order(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->sort_order;
}

GtkSortType
fsearch_database_search_info_get_sort_type(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->sort_type;
}

FsearchQuery *
fsearch_database_search_info_get_query(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, NULL);
    return fsearch_query_ref(info->query);
}

bool
fsearch_database_search_info_get_is_complete(FsearchDatabaseSearchInfo *info) {
    g_return_val_if_fail(info, true);
    return info->is_complete;
}
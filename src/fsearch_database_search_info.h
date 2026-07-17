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

#include <gtk/gtk.h>
#include <stdbool.h>

#include "fsearch_query.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_SEARCH_INFO (fsearch_database_search_info_get_type())

typedef struct _FsearchDatabaseSearchInfo FsearchDatabaseSearchInfo;

GType
fsearch_database_search_info_get_type(void);

FsearchDatabaseSearchInfo *
fsearch_database_search_info_ref(FsearchDatabaseSearchInfo *info);

void
fsearch_database_search_info_unref(FsearchDatabaseSearchInfo *info);

FsearchDatabaseSearchInfo *
fsearch_database_search_info_new(uint32_t id,
                                 FsearchQuery *query,
                                 uint32_t num_files,
                                 uint32_t num_folders,
                                 uint32_t num_files_selected,
                                 uint32_t num_folders_selected,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type,
                                 bool is_complete);

uint32_t
fsearch_database_search_info_get_id(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_files(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_folders(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_files_selected(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_folders_selected(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_entries_selected(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_entries(FsearchDatabaseSearchInfo *info);

FsearchDatabaseIndexProperty
fsearch_database_search_info_get_sort_order(FsearchDatabaseSearchInfo *info);

GtkSortType
fsearch_database_search_info_get_sort_type(FsearchDatabaseSearchInfo *info);

FsearchQuery *
fsearch_database_search_info_get_query(FsearchDatabaseSearchInfo *info);

bool
fsearch_database_search_info_get_is_complete(FsearchDatabaseSearchInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseSearchInfo, fsearch_database_search_info_unref)

G_END_DECLS
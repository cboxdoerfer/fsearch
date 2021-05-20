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

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_filter.h"
#include "fsearch_query.h"
#include "fsearch_task.h"

#include <gio/gio.h>
#include <stdint.h>

typedef struct _DatabaseSearchResult {
    DynamicArray *files;
    DynamicArray *folders;

    volatile int ref_count;

    FsearchQuery *query;
} DatabaseSearchResult;

uint32_t
db_search_result_get_num_files(DatabaseSearchResult *result);

uint32_t
db_search_result_get_num_folders(DatabaseSearchResult *result);

FsearchQuery *
db_search_result_get_query(DatabaseSearchResult *result);

DatabaseSearchResult *
db_search_result_ref(DatabaseSearchResult *result);

void
db_search_result_unref(DatabaseSearchResult *result);

void
db_search_queue(FsearchTaskQueue *queue,
                FsearchQuery *query,
                FsearchTaskFinishedFunc finished_func,
                FsearchTaskCancelledFunc cancelled_func);

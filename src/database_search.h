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

#include "array.h"
#include "btree.h"
#include "fsearch_filter.h"
#include "fsearch_task.h"
#include "query.h"

#include <gio/gio.h>
#include <stdint.h>

typedef struct _DatabaseSearchResult {
    DynamicArray *entries;
    void *cb_data;
    uint32_t num_folders;
    uint32_t num_files;

    FsearchQuery *query;
} DatabaseSearchResult;

void
db_search_result_free(DatabaseSearchResult *result);

void
db_search_queue(FsearchTaskQueue *queue, FsearchQuery *query);

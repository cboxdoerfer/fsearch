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
#include "database.h"
#include "fsearch_filter.h"
#include "fsearch_thread_pool.h"
#include "query.h"
#include <stdint.h>

typedef struct _DatabaseSearch DatabaseSearch;
typedef struct _DatabaseSearchEntry DatabaseSearchEntry;

// search modes
enum {
    DB_SEARCH_MODE_NORMAL = 0,
    DB_SEARCH_MODE_REGEX = 1,
};

typedef struct _DatabaseSearchResult {
    FsearchDatabase *db;
    GPtrArray *results;
    void *cb_data;
    uint32_t num_folders;
    uint32_t num_files;
} DatabaseSearchResult;

struct _DatabaseSearch {
    GPtrArray *results;
    FsearchThreadPool *pool;

    GThread *search_thread;
    bool search_thread_terminate;
    GMutex query_mutex;
    GCond search_thread_start_cond;

    FsearchQuery *query_ctx;
    uint32_t num_folders;
    uint32_t num_files;
};

void
db_search_free(DatabaseSearch *search);

DatabaseSearch *
db_search_new(FsearchThreadPool *pool);

BTreeNode *
db_search_entry_get_node(DatabaseSearchEntry *entry);

uint32_t
db_search_entry_get_pos(DatabaseSearchEntry *entry);

void
db_search_entry_set_pos(DatabaseSearchEntry *entry, uint32_t pos);

void
db_search_results_clear(DatabaseSearch *search);

uint32_t
db_search_get_num_results(DatabaseSearch *search);

uint32_t
db_search_get_num_files(DatabaseSearch *search);

uint32_t
db_search_get_num_folders(DatabaseSearch *search);

GPtrArray *
db_search_get_results(DatabaseSearch *search);

void
db_search_remove_entry(DatabaseSearch *search, DatabaseSearchEntry *entry);

void
db_search_queue(DatabaseSearch *search, FsearchQuery *query);


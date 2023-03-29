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

#include <gio/gio.h>

typedef struct DatabaseSearchResult {
    DynamicArray *folders;
    DynamicArray *files;
    FsearchDatabaseIndexProperty sort_order;
} DatabaseSearchResult;

DatabaseSearchResult *
db_search_empty(DynamicArray *folders, DynamicArray *files);

DatabaseSearchResult *
db_search(FsearchQuery *q,
          FsearchThreadPool *pool,
          DynamicArray *folders,
          DynamicArray *files,
          GCancellable *cancellable);

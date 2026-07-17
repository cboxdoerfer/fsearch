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

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_index_properties.h"

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_CHUNKED_ARRAY (fsearch_database_chunked_array_get_type())

typedef struct _FsearchDatabaseChunkedArray FsearchDatabaseChunkedArray;

GType
fsearch_database_chunked_array_get_type(void);

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_new(DynamicArray *array,
                                   gboolean is_array_sorted,
                                   FsearchDatabaseSortOrderChain chain,
                                   FsearchDatabaseEntryType entry_type,
                                   GCancellable *cancellable,
                                   GDestroyNotify entry_free_func);

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_ref(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_unref(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_balance(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_insert(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

void
fsearch_database_chunked_array_insert_array(FsearchDatabaseChunkedArray *self, DynamicArray *array);

FsearchDatabaseEntry *
fsearch_database_chunked_array_steal(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
fsearch_database_chunked_array_find_slow(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

DynamicArray *
fsearch_database_chunked_array_steal_descendants(FsearchDatabaseChunkedArray *self,
                                                 FsearchDatabaseEntry *folder,
                                                 int32_t num_known_descendants);

uint32_t
fsearch_database_chunked_array_remove_marked_folders(FsearchDatabaseChunkedArray *self);

DynamicArray *
fsearch_database_chunked_array_steal_marked_folders(FsearchDatabaseChunkedArray *self);

FsearchDatabaseEntry *
fsearch_database_chunked_array_find(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
fsearch_database_chunked_array_get_entry(FsearchDatabaseChunkedArray *self, uint32_t idx);

uint32_t
fsearch_database_chunked_array_get_num_entries(FsearchDatabaseChunkedArray *self);

DynamicArray *
fsearch_database_chunked_array_get_chunks(FsearchDatabaseChunkedArray *self);

DynamicArray *
fsearch_database_chunked_array_get_joined(FsearchDatabaseChunkedArray *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseChunkedArray, fsearch_database_chunked_array_unref)

G_END_DECLS
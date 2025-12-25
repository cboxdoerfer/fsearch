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
#include "fsearch_database_index.h"
#include "fsearch_thread_pool.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchDatabase FsearchDatabase;

bool
db_register_view(FsearchDatabase *db, gpointer view);

bool
db_unregister_view(FsearchDatabase *db, gpointer view);

bool
db_load(FsearchDatabase *db, const char *path, void (*status_cb)(const char *));

bool
db_scan(FsearchDatabase *db, GCancellable *cancellable, void (*status_cb)(const char *));

FsearchDatabase *
db_ref(FsearchDatabase *db);

void
db_unref(FsearchDatabase *db);

FsearchDatabase *
db_new(GList *includes, GList *excludes, char **exclude_files, bool exclude_hidden);

bool
db_save(FsearchDatabase *db, const char *path);

time_t
db_get_timestamp(FsearchDatabase *db);

uint32_t
db_get_num_files(FsearchDatabase *db);

uint32_t
db_get_num_folders(FsearchDatabase *db);

uint32_t
db_get_num_entries(FsearchDatabase *db);

void
db_unlock(FsearchDatabase *db);

void
db_lock(FsearchDatabase *db);

bool
db_try_lock(FsearchDatabase *db);

DynamicArray *
db_get_folders_copy(FsearchDatabase *db);

DynamicArray *
db_get_files_copy(FsearchDatabase *db);

DynamicArray *
db_get_folders(FsearchDatabase *db);

DynamicArray *
db_get_files(FsearchDatabase *db);

FsearchThreadPool *
db_get_thread_pool(FsearchDatabase *db);

bool
db_has_entries_sorted_by_type(FsearchDatabase *db, FsearchDatabaseIndexType sort_type);

bool
db_get_entries_sorted(FsearchDatabase *db,
                      FsearchDatabaseIndexType requested_sort_type,
                      FsearchDatabaseIndexType *returned_sort_type,
                      DynamicArray **folders,
                      DynamicArray **files);

DynamicArray *
db_get_folders_sorted_copy(FsearchDatabase *db, FsearchDatabaseIndexType sort_type);

DynamicArray *
db_get_files_sorted_copy(FsearchDatabase *db, FsearchDatabaseIndexType sort_type);

DynamicArray *
db_get_folders_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type);

DynamicArray *
db_get_files_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type);

// --- Incremental update API for file monitoring ---

// Add a single file entry to the database
// Inserts into all sorted arrays, updates parent counts/sizes
// Returns the newly created entry, or NULL on failure
FsearchDatabaseEntry *
db_add_file(FsearchDatabase *db,
            FsearchDatabaseEntryFolder *parent,
            const char *name,
            off_t size,
            time_t mtime);

// Add a single folder entry to the database
// Returns the new folder entry for use as parent, or NULL on failure
FsearchDatabaseEntryFolder *
db_add_folder(FsearchDatabase *db,
              FsearchDatabaseEntryFolder *parent,
              const char *name,
              time_t mtime);

// Remove a file entry from the database
// Removes from all sorted arrays, updates parent counts/sizes
// Entry is returned to memory pool for reuse
bool
db_remove_file(FsearchDatabase *db, FsearchDatabaseEntry *entry);

// Remove a folder entry and all its children recursively
// All entries are returned to memory pool for reuse
bool
db_remove_folder(FsearchDatabase *db, FsearchDatabaseEntryFolder *folder);

// Update file entry metadata (size, mtime)
// Removes and re-inserts into sorted arrays where sort key changed
bool
db_update_file(FsearchDatabase *db,
               FsearchDatabaseEntry *entry,
               off_t new_size,
               time_t new_mtime);

// Find a folder entry by full path
// Returns NULL if not found
FsearchDatabaseEntryFolder *
db_find_folder_by_path(FsearchDatabase *db, const char *path);

// Find an entry (file or folder) by full path
// Returns NULL if not found
FsearchDatabaseEntry *
db_find_entry_by_path(FsearchDatabase *db, const char *path);

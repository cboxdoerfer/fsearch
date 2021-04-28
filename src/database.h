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
#include "fsearch_db_entry.h"
#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DATABASE_ENTRY_TYPE_NONE,
    DATABASE_ENTRY_TYPE_FOLDER,
    DATABASE_ENTRY_TYPE_FILE,
    NUM_DATABASE_ENTRY_TYPES,
} FsearchDatabaseEntryType;

typedef struct _FsearchDatabaseEntryFile FsearchDatabaseEntry;
typedef struct _FsearchDatabaseEntryFile FsearchDatabaseEntryFile;
typedef struct _FsearchDatabaseEntryFolder FsearchDatabaseEntryFolder;

typedef struct _FsearchDatabase FsearchDatabase;

bool
db_load(FsearchDatabase *db, const char *path, void (*status_cb)(const char *));

bool
db_scan(FsearchDatabase *db, GCancellable *cancellable, void (*status_cb)(const char *));

void
db_ref(FsearchDatabase *db);

void
db_unref(FsearchDatabase *db);

FsearchDatabase *
db_new(GList *includes, GList *excludes, char **exclude_files, bool exclude_hidden);

bool
db_save(FsearchDatabase *db);

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
db_get_folders(FsearchDatabase *db);

DynamicArray *
db_get_files(FsearchDatabase *db);

off_t
db_entry_get_size(FsearchDatabaseEntry *entry);

const char *
db_entry_get_name(FsearchDatabaseEntry *entry);

GString *
db_entry_get_path(FsearchDatabaseEntry *entry);

int32_t
db_entry_init_path(FsearchDatabaseEntry *entry, char *path, size_t path_len);

void
db_entry_append_path(FsearchDatabaseEntry *node, GString *str);

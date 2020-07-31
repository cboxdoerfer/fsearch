/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include "array.h"
#include "btree.h"

typedef struct _FsearchDatabase FsearchDatabase;

typedef struct _FsearchDatabaseNode FsearchDatabaseNode;

bool
db_load_from_file (FsearchDatabase *db,
                   const char *path,
                   void (*callback)(const char *));

bool
db_scan (FsearchDatabase *db, void (*callback)(const char *));

void
db_free (FsearchDatabase *db);

FsearchDatabase *
db_new (GList *includes, GList *excludes, char **exclude_files, bool exclude_hidden);

bool
db_save_locations (FsearchDatabase *db);

time_t
db_get_timestamp (FsearchDatabase *db);

uint32_t
db_get_num_entries (FsearchDatabase *db);

void
db_unlock (FsearchDatabase *db);

void
db_lock (FsearchDatabase *db);

bool
db_try_lock (FsearchDatabase *db);

DynamicArray *
db_get_entries (FsearchDatabase *db);

void
db_sort (FsearchDatabase *db);


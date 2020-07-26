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

typedef struct _Database Database;

typedef struct _DatabaseLocation DatabaseLocation;

void
db_location_free (DatabaseLocation *location);

bool
db_location_load (Database *db, const char *location_name);

bool
db_location_add (Database *db,
                 const char *location_name,
                 void (*callback)(const char *));

bool
db_load_from_file (Database *db,
                   const char *path,
                   void (*callback)(const char *));

bool
db_scan (Database *db, void (*callback)(const char *));

bool
db_location_remove (Database *db, const char *path);

bool
db_location_write_to_file (DatabaseLocation *location, const char *fname);

BTreeNode *
db_location_get_entries (DatabaseLocation *location);

void
db_free (Database *db);

Database *
db_new (GList *includes, GList *excludes, char **exclude_files, bool exclude_hidden);

gboolean
db_list_append_node (BTreeNode *node,
                     gpointer data);

void
db_update_sort_index (Database *db);

bool
db_save_locations (Database *db);

void
db_update_entries_list (Database *db);

void
db_build_initial_entries_list (Database *db);

time_t
db_get_timestamp (Database *db);

uint32_t
db_get_num_entries (Database *db);

void
db_unlock (Database *db);

void
db_lock (Database *db);

bool
db_try_lock (Database *db);

DynamicArray *
db_get_entries (Database *db);

void
db_sort (Database *db);

bool
db_clear (Database *db);

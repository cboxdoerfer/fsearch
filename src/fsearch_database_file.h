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

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"

#include <stdbool.h>

bool
fsearch_database_file_load(const char *file_path,
                           void (*status_cb)(const char *),
                           FsearchDatabaseIndexStore **store_out,
                           FsearchDatabaseIncludeManager *config_include_manager,
                           FsearchDatabaseExcludeManager *config_exclude_manager,
                           FsearchDatabaseIndexStoreEventFunc event_func,
                           void *event_func_user_data);

bool
fsearch_database_file_load_config(const char *file_path,
                                  FsearchDatabaseIncludeManager **include_manager_out,
                                  FsearchDatabaseExcludeManager **exclude_manager_out,
                                  FsearchDatabaseIndexPropertyFlags *flags_out);

bool
fsearch_database_file_save(FsearchDatabaseIndexStore *store, const char *file_path);
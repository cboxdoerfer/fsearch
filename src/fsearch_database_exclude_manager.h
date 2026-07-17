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
*/

#pragma once

#include <gio/gio.h>

#include "fsearch_database_exclude.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER fsearch_database_exclude_manager_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabaseExcludeManager,
                     fsearch_database_exclude_manager,
                     FSEARCH,
                     DATABASE_EXCLUDE_MANAGER,
                     GObject)

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new(void);

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new_with_defaults(void);

GPtrArray *
fsearch_database_exclude_manager_get_excludes(FsearchDatabaseExcludeManager *manager);

gboolean
fsearch_database_exclude_manager_get_exclude_hidden(FsearchDatabaseExcludeManager *self);

void
fsearch_database_exclude_manager_add(FsearchDatabaseExcludeManager *manager, FsearchDatabaseExclude *exclude);

void
fsearch_database_exclude_manager_set_exclude_hidden(FsearchDatabaseExcludeManager *self, gboolean exclude_hidden);

void
fsearch_database_exclude_manager_remove(FsearchDatabaseExcludeManager *manager, FsearchDatabaseExclude *exclude);

gboolean
fsearch_database_exclude_manager_excludes(FsearchDatabaseExcludeManager *manager,
                                          const char *path,
                                          const char *basename,
                                          gboolean is_dir);

gboolean
fsearch_database_exclude_manager_equal(FsearchDatabaseExcludeManager *m1, FsearchDatabaseExcludeManager *m2);

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_copy(FsearchDatabaseExcludeManager *self);

G_END_DECLS

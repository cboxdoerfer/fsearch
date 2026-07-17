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

#include "fsearch_array.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_INDEX (fsearch_database_index_get_type())

typedef struct _FsearchDatabaseIndex FsearchDatabaseIndex;

typedef void (*FsearchDatabaseIndexEventFunc)(FsearchDatabaseIndex *, FsearchDatabaseIndexEvent *event, gpointer);

GType
fsearch_database_index_get_type(void);

FsearchDatabaseIndex *
fsearch_database_index_ref(FsearchDatabaseIndex *self);

void
fsearch_database_index_unref(FsearchDatabaseIndex *self);

FsearchDatabaseIndex *
fsearch_database_index_new(FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           GMainContext *monitor_ctx,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer event_func_data);

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        DynamicArray *folders,
                                        DynamicArray *files,
                                        FsearchDatabaseIndexPropertyFlags flags);

void
fsearch_database_index_set_event_func(FsearchDatabaseIndex *self,
                                      FsearchDatabaseIndexEventFunc event_func,
                                      gpointer event_func_data);

FsearchDatabaseInclude *
fsearch_database_index_get_include(FsearchDatabaseIndex *self);

FsearchDatabaseExcludeManager *
fsearch_database_index_get_exclude_manager(FsearchDatabaseIndex *self);

DynamicArray *
fsearch_database_index_get_files(FsearchDatabaseIndex *self);

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self);

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_get_flags(FsearchDatabaseIndex *self);

const char *
fsearch_database_index_get_path(FsearchDatabaseIndex *self);

bool
fsearch_database_index_wants_root_reappear_poll(FsearchDatabaseIndex *self);

void
fsearch_database_index_lock(FsearchDatabaseIndex *self);

void
fsearch_database_index_unlock(FsearchDatabaseIndex *self);

bool
fsearch_database_index_scan(FsearchDatabaseIndex *self, GCancellable *cancellable);

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start);

gboolean
fsearch_database_index_process_events(FsearchDatabaseIndex *self);

bool
fsearch_database_index_has_pending_events(FsearchDatabaseIndex *self);

bool
fsearch_database_index_remove_path(FsearchDatabaseIndex *self, const char *path, bool *root_removed);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndex, fsearch_database_index_unref)
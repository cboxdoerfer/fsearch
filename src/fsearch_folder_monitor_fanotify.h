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

#include "fsearch_database_entry.h"

#include <glib.h>

typedef struct FsearchFolderMonitorFanotify FsearchFolderMonitorFanotify;

FsearchFolderMonitorFanotify *
fsearch_folder_monitor_fanotify_new(GMainContext *monitor_context, GAsyncQueue *event_queue);

void
fsearch_folder_monitor_fanotify_free(FsearchFolderMonitorFanotify *self);

bool
fsearch_folder_monitor_fanotify_watch(FsearchFolderMonitorFanotify *self,
                                      FsearchDatabaseEntry *folder,
                                      const char *path);

void
fsearch_folder_monitor_fanotify_unwatch(FsearchFolderMonitorFanotify *self, FsearchDatabaseEntry *folder);

FsearchDatabaseEntry *
fsearch_folder_monitor_fanotify_resolve(FsearchFolderMonitorFanotify *self, gpointer handle);
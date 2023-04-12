#pragma once

#include "fsearch_database_entry.h"

#include <glib.h>

typedef struct FsearchFolderMonitorFanotify FsearchFolderMonitorFanotify;

FsearchFolderMonitorFanotify *
fsearch_folder_monitor_fanotify_new(GMainContext *monitor_context, GAsyncQueue *event_queue);

void
fsearch_folder_monitor_fanotify_free(FsearchFolderMonitorFanotify *self);

bool
fsearch_folder_monitor_fanotify_watch(FsearchFolderMonitorFanotify *self, FsearchDatabaseEntry *folder, const char *path);

void
fsearch_folder_monitor_fanotify_unwatch(FsearchFolderMonitorFanotify *self, FsearchDatabaseEntry *folder);

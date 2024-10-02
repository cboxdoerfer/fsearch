#pragma once

#include "fsearch_database_entry.h"

#include <glib.h>

typedef struct FsearchFolderMonitorInotify FsearchFolderMonitorInotify;

FsearchFolderMonitorInotify *
fsearch_folder_monitor_inotify_new(GMainContext *monitor_context, GAsyncQueue *event_queue);

void
fsearch_folder_monitor_inotify_free(FsearchFolderMonitorInotify *self);

bool
fsearch_folder_monitor_inotify_watch(FsearchFolderMonitorInotify *self, FsearchDatabaseEntry *folder, const char *path);

void
fsearch_folder_monitor_inotify_unwatch(FsearchFolderMonitorInotify *self, FsearchDatabaseEntry *folder);

#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"

bool
db_scan_folder(const char *path,
               FsearchDatabaseEntry *parent,
               DynamicArray *folders,
               DynamicArray *files,
               FsearchDatabaseExcludeManager *exclude_manager,
               FsearchFolderMonitorFanotify *fanotify_monitor,
               FsearchFolderMonitorInotify *inotify_monitor,
               uint32_t index_id,
               bool one_file_system,
               GCancellable *cancellable,
               void (*status_cb)(const char *, gpointer),
               gpointer status_cb_data);

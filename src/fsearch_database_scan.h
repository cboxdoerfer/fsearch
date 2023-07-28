#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"
#include "fsearch_memory_pool.h"

bool
db_scan_folder(const char *path,
               FsearchDatabaseEntryFolder *parent,
               FsearchMemoryPool *folder_pool,
               FsearchMemoryPool *file_pool,
               DynamicArray *folders,
               DynamicArray *files,
               FsearchDatabaseExcludeManager *exclude_manager,
               FsearchFolderMonitorFanotify *fanotify_monitor,
               FsearchFolderMonitorInotify *inotify_monitor,
               uint32_t index_id,
               bool one_file_system,
               GCancellable *cancellable,
               void (*status_cb)(const char *));

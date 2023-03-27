#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_store.h"

bool
db_scan_folder(const char *path,
               FsearchDatabaseEntryFolder *parent,
               FsearchMemoryPool *folder_pool,
               FsearchMemoryPool *file_pool,
               DynamicArray *folders,
               DynamicArray *files,
               FsearchDatabaseExcludeManager *exclude_manager,
               GHashTable *handles,
               GHashTable *watch_descriptors,
               GMutex *monitor_lock,
               int32_t fanotify_fd,
               int32_t inotify_fd,
               uint32_t index_id,
               bool one_file_system,
               GCancellable *cancellable,
               void (*status_cb)(const char *));

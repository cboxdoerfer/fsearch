#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_store.h"

bool
db_scan_folder(FsearchDatabaseIndex *index,
               const char *path,
               FsearchDatabaseExcludeManager *exclude_manager,
               GCancellable *cancellable,
               void (*status_cb)(const char *));

FsearchDatabaseIndexStore *
db_scan2(FsearchDatabaseIncludeManager *include_manager,
         FsearchDatabaseExcludeManager *exclude_manager,
         FsearchDatabaseIndexPropertyFlags flags,
         GCancellable *cancellable,
         FsearchDatabaseIndexEventFunc event_func,
         gpointer user_data);
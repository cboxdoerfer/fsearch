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

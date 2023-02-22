#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_store.h"

bool
db_file_load(const char *path,
             void (*status_cb)(const char *),
             FsearchDatabaseIndexStore **store_out,
             FsearchDatabaseIncludeManager **include_manager_out,
             FsearchDatabaseExcludeManager **exclude_manager_out);

bool
db_file_save(FsearchDatabaseIndexStore *store, const char *path);

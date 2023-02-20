#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"

bool
db_file_load(const char *path,
             void (*status_cb)(const char *),
             FsearchDatabaseIncludeManager **includes_out,
             FsearchDatabaseExcludeManager **excludes_out,
             FsearchDatabaseIndex **index_out);

bool
db_file_save(FsearchDatabaseIncludeManager *includes,
             FsearchDatabaseExcludeManager *excludes,
             FsearchDatabaseIndex *index,
             const char *path);

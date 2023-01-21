#pragma once

#include "fsearch_database_index.h"

FsearchDatabaseIndex *
db_file_load(const char *path, void (*status_cb)(const char *));

bool
db_file_save(FsearchDatabaseIndex *index, const char *path);

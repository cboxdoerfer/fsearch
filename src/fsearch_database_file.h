#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"

#include <stdbool.h>

bool
fsearch_database_file_load(const char *file_path,
                           void (*status_cb)(const char *),
                           FsearchDatabaseIndexStore **store_out,
                           FsearchDatabaseIndexEventFunc event_func,
                           void *event_func_user_data);

bool
fsearch_database_file_load_config(const char *file_path,
                                  FsearchDatabaseIncludeManager **include_manager_out,
                                  FsearchDatabaseExcludeManager **exclude_manager_out,
                                  FsearchDatabaseIndexPropertyFlags *flags_out);

bool
fsearch_database_file_save(FsearchDatabaseIndexStore *store, const char *file_path);

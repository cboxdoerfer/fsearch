#pragma once

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_store.h"

FsearchDatabaseIndexStore *
db_scan2(FsearchDatabaseIncludeManager *include_manager,
         FsearchDatabaseExcludeManager *exclude_manager,
         FsearchDatabaseIndexPropertyFlags flags,
         GCancellable *cancellable);
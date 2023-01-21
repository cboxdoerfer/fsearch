#pragma once

#include "fsearch_database_index.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_exclude_manager.h"

FsearchDatabaseIndex *
db_scan2(FsearchDatabaseIncludeManager *include_manager,
         FsearchDatabaseExcludeManager *exclude_manager,
         FsearchDatabaseIndexFlags flags,
         GCancellable *cancellable);
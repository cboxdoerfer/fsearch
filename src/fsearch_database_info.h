#pragma once

#include <gtk/gtk.h>

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_INFO (fsearch_database_info_get_type())

typedef struct _FsearchDatabaseInfo FsearchDatabaseInfo;

GType
fsearch_database_info_get_type(void);

FsearchDatabaseInfo *
fsearch_database_info_ref(FsearchDatabaseInfo *info);

void
fsearch_database_info_unref(FsearchDatabaseInfo *info);

FsearchDatabaseInfo *
fsearch_database_info_new(FsearchDatabaseIncludeManager *includes,
                          FsearchDatabaseExcludeManager *excludes,
                          uint32_t num_files,
                          uint32_t num_folders);

uint32_t
fsearch_database_info_get_num_files(FsearchDatabaseInfo *info);

uint32_t
fsearch_database_info_get_num_folders(FsearchDatabaseInfo *info);

uint32_t
fsearch_database_info_get_num_entries(FsearchDatabaseInfo *info);

FsearchDatabaseIncludeManager *
fsearch_database_info_get_include_manager(FsearchDatabaseInfo *info);

FsearchDatabaseExcludeManager *
fsearch_database_info_get_exclude_manager(FsearchDatabaseInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseInfo, fsearch_database_info_unref)

G_END_DECLS

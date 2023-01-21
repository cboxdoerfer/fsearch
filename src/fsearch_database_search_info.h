#pragma once

#include <gtk/gtk.h>

#include "fsearch_database_index.h"

#include "fsearch_query.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_SEARCH_INFO (fsearch_database_search_info_get_type())

typedef struct _FsearchDatabaseSearchInfo FsearchDatabaseSearchInfo;

GType
fsearch_database_search_info_get_type(void);

FsearchDatabaseSearchInfo *
fsearch_database_search_info_ref(FsearchDatabaseSearchInfo *info);

void
fsearch_database_search_info_unref(FsearchDatabaseSearchInfo *info);

FsearchDatabaseSearchInfo *
fsearch_database_search_info_new(FsearchQuery *query,
                                 uint32_t num_files,
                                 uint32_t num_folders,
                                 FsearchDatabaseIndexType sort_order,
                                 GtkSortType sort_type);

uint32_t
fsearch_database_search_info_get_num_files(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_folders(FsearchDatabaseSearchInfo *info);

uint32_t
fsearch_database_search_info_get_num_entries(FsearchDatabaseSearchInfo *info);

FsearchDatabaseIndexType
fsearch_database_search_info_get_sort_order(FsearchDatabaseSearchInfo *info);

GtkSortType
fsearch_database_search_info_get_sort_type(FsearchDatabaseSearchInfo *info);

FsearchQuery *
fsearch_database_search_info_get_query(FsearchDatabaseSearchInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseSearchInfo, fsearch_database_search_info_unref)

G_END_DECLS

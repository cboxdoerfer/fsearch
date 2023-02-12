#pragma once

#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_query.h"

#include <glib.h>

typedef struct FsearchDatabaseWork FsearchDatabaseWork;
typedef struct FsearchDatabaseWorkResult FsearchDatabaseWorkResult;
typedef void (*FsearchDatabaseWorkCallback)(FsearchDatabaseWork *, FsearchDatabaseWorkResult *, gpointer);

typedef enum FsearchDatabaseWorkKind {
    FSEARCH_DATABASE_WORK_LOAD_FROM_FILE,
    FSEARCH_DATABASE_WORK_RESCAN,
    FSEARCH_DATABASE_WORK_SAVE_TO_FILE,
    FSEARCH_DATABASE_WORK_SCAN,
    FSEARCH_DATABASE_WORK_SEARCH,
    FSEARCH_DATABASE_WORK_SORT,
    FSEARCH_DATABASE_WORK_GET_ITEM_INFO,
    NUM_FSEARCH_DATABASE_WORK_KINDS,
} FsearchDatabaseWorkKind;

FsearchDatabaseWork *
fsearch_database_work_ref(FsearchDatabaseWork *work);

void
fsearch_database_work_unref(FsearchDatabaseWork *work);

FsearchDatabaseWork *
fsearch_database_work_new_rescan(FsearchDatabaseWorkCallback callback, gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexFlags flags,
                               FsearchDatabaseWorkCallback callback,
                               gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexFlags flags,
                               FsearchDatabaseWorkCallback callback,
                               gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_search(guint view_id,
                                 FsearchQuery *query,
                                 FsearchDatabaseIndexType sort_order,
                                 GtkSortType sort_type,
                                 FsearchDatabaseWorkCallback callback,
                                 gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_sort(guint view_id,
                               FsearchDatabaseIndexType sort_order,
                               GtkSortType sort_type,
                               FsearchDatabaseWorkCallback callback,
                               gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_get_item_info(gint view_id,
                                        guint index,
                                        FsearchDatabaseEntryInfoFlags flags,
                                        FsearchDatabaseWorkCallback callback,
                                        gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_load(FsearchDatabaseWorkCallback callback, gpointer callback_data);

FsearchDatabaseWork *
fsearch_database_work_new_save(FsearchDatabaseWorkCallback callback, gpointer callback_data);

FsearchDatabaseWorkKind
fsearch_database_work_get_kind(FsearchDatabaseWork *work);

FsearchQuery *
fsearch_database_work_search_get_query(FsearchDatabaseWork *work);

FsearchDatabaseIndexType
fsearch_database_work_search_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_search_get_sort_type(FsearchDatabaseWork *work);

guint
fsearch_database_work_search_get_view_id(FsearchDatabaseWork *work);

FsearchDatabaseIndexType
fsearch_database_work_sort_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_sort_get_sort_type(FsearchDatabaseWork *work);

guint
fsearch_database_work_sort_get_view_id(FsearchDatabaseWork *work);

FsearchDatabaseIncludeManager *
fsearch_database_work_scan_get_include_manager(FsearchDatabaseWork *work);

FsearchDatabaseExcludeManager *
fsearch_database_work_scan_get_exclude_manager(FsearchDatabaseWork *work);

FsearchDatabaseIndexFlags
fsearch_database_work_scan_get_flags(FsearchDatabaseWork *work);

gint
fsearch_database_work_item_info_get_view_id(FsearchDatabaseWork *work);

guint
fsearch_database_work_item_info_get_index(FsearchDatabaseWork *work);

FsearchDatabaseEntryInfoFlags
fsearch_database_work_item_info_get_flags(FsearchDatabaseWork *work);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseWork, fsearch_database_work_unref)

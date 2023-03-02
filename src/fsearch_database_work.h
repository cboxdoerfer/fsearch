#pragma once

#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_query.h"
#include "fsearch_selection_type.h"

#include <glib.h>

typedef struct FsearchDatabaseWork FsearchDatabaseWork;

typedef enum FsearchDatabaseWorkKind {
    FSEARCH_DATABASE_WORK_LOAD_FROM_FILE,
    FSEARCH_DATABASE_WORK_RESCAN,
    FSEARCH_DATABASE_WORK_SAVE_TO_FILE,
    FSEARCH_DATABASE_WORK_SCAN,
    FSEARCH_DATABASE_WORK_SEARCH,
    FSEARCH_DATABASE_WORK_SORT,
    FSEARCH_DATABASE_WORK_GET_ITEM_INFO,
    FSEARCH_DATABASE_WORK_MODIFY_SELECTION,
    FSEARCH_DATABASE_WORK_MONITOR_EVENT,
    NUM_FSEARCH_DATABASE_WORK_KINDS,
} FsearchDatabaseWorkKind;

FsearchDatabaseWork *
fsearch_database_work_ref(FsearchDatabaseWork *work);

void
fsearch_database_work_unref(FsearchDatabaseWork *work);

FsearchDatabaseWork *
fsearch_database_work_new_rescan(void);

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexPropertyFlags flags);

FsearchDatabaseWork *
fsearch_database_work_new_modify_selection(guint view_id, FsearchSelectionType selection_type, int32_t idx_1, int32_t idx_2);

FsearchDatabaseWork *
fsearch_database_work_new_search(guint view_id,
                                 FsearchQuery *query,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type);

FsearchDatabaseWork *
fsearch_database_work_new_sort(guint view_id, FsearchDatabaseIndexProperty sort_order, GtkSortType sort_type);

FsearchDatabaseWork *
fsearch_database_work_new_get_item_info(guint view_id, guint index, FsearchDatabaseEntryInfoFlags flags);

FsearchDatabaseWork *
fsearch_database_work_new_load(void);

FsearchDatabaseWork *
fsearch_database_work_new_save(void);

FsearchDatabaseWork *
fsearch_database_work_new_monitor_event(FsearchDatabaseIndex *index,
                                        FsearchDatabaseIndexEventKind event_kind,
                                        FsearchDatabaseEntry *entry_1,
                                        FsearchDatabaseEntry *entry_2,
                                        GString *path,
                                        int32_t watch_descriptor);

FsearchDatabaseWorkKind
fsearch_database_work_get_kind(FsearchDatabaseWork *work);

GCancellable *
fsearch_database_work_get_cancellable(FsearchDatabaseWork *work);

void
fsearch_database_work_cancel(FsearchDatabaseWork *work);

guint
fsearch_database_work_get_view_id(FsearchDatabaseWork *work);

int32_t
fsearch_database_work_modify_selection_get_start_idx(FsearchDatabaseWork *work);

int32_t
fsearch_database_work_modify_selection_get_end_idx(FsearchDatabaseWork *work);

FsearchSelectionType
fsearch_database_work_modify_selection_get_type(FsearchDatabaseWork *work);

FsearchQuery *
fsearch_database_work_search_get_query(FsearchDatabaseWork *work);

FsearchDatabaseIndexProperty
fsearch_database_work_search_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_search_get_sort_type(FsearchDatabaseWork *work);

FsearchDatabaseIndexProperty
fsearch_database_work_sort_get_sort_order(FsearchDatabaseWork *work);

GtkSortType
fsearch_database_work_sort_get_sort_type(FsearchDatabaseWork *work);

FsearchDatabaseIncludeManager *
fsearch_database_work_scan_get_include_manager(FsearchDatabaseWork *work);

FsearchDatabaseExcludeManager *
fsearch_database_work_scan_get_exclude_manager(FsearchDatabaseWork *work);

FsearchDatabaseIndexPropertyFlags
fsearch_database_work_scan_get_flags(FsearchDatabaseWork *work);

guint
fsearch_database_work_item_info_get_index(FsearchDatabaseWork *work);

FsearchDatabaseEntryInfoFlags
fsearch_database_work_item_info_get_flags(FsearchDatabaseWork *work);

FsearchDatabaseIndexEventKind
fsearch_database_work_monitor_event_get_kind(FsearchDatabaseWork *work);

int32_t
fsearch_database_work_monitor_event_get_watch_descriptor(FsearchDatabaseWork *work);

GString *
fsearch_database_work_monitor_event_get_path(FsearchDatabaseWork *work);

FsearchDatabaseEntry *
fsearch_database_work_monitor_event_get_entry_1(FsearchDatabaseWork *work);

FsearchDatabaseEntry *
fsearch_database_work_monitor_event_get_entry_2(FsearchDatabaseWork *work);

FsearchDatabaseIndex *
fsearch_database_work_monitor_event_get_index(FsearchDatabaseWork *work);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseWork, fsearch_database_work_unref)

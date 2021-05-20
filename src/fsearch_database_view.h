#pragma once

#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_filter.h"
#include "fsearch_query.h"
#include "fsearch_query_flags.h"

typedef struct FsearchDatabaseView FsearchDatabaseView;

typedef void (*FsearchDatabaseViewNotifyFunc)(FsearchDatabaseView *view, gpointer user_data);

void
db_view_unref(FsearchDatabaseView *view);

FsearchDatabaseView *
db_view_ref(FsearchDatabaseView *view);

FsearchDatabaseView *
db_view_new(const char *query_text,
            FsearchQueryFlags flags,
            FsearchFilter *filter,
            FsearchDatabaseIndexType sort_order,
            FsearchDatabaseViewNotifyFunc view_changed_func,
            FsearchDatabaseViewNotifyFunc selection_changed_func,
            FsearchDatabaseViewNotifyFunc search_started_func,
            FsearchDatabaseViewNotifyFunc search_finished_func,
            FsearchDatabaseViewNotifyFunc sort_started_func,
            FsearchDatabaseViewNotifyFunc sort_finished_func,
            gpointer user_data);

void
db_view_set_thread_pool(FsearchDatabaseView *view, FsearchThreadPool *pool);

void
db_view_set_filter(FsearchDatabaseView *view, FsearchFilter *filter);

void
db_view_set_query_flags(FsearchDatabaseView *view, FsearchQueryFlags query_flags);

void
db_view_set_query_text(FsearchDatabaseView *view, const char *query_text);

void
db_view_set_sort_order(FsearchDatabaseView *view, FsearchDatabaseIndexType sort_order);

uint32_t
db_view_get_num_folders(FsearchDatabaseView *view);

uint32_t
db_view_get_num_files(FsearchDatabaseView *view);

uint32_t
db_view_get_num_entries(FsearchDatabaseView *view);

FsearchDatabaseIndexType
db_view_get_sort_order(FsearchDatabaseView *view);

void
db_view_register(FsearchDatabase *db, FsearchDatabaseView *view);

void
db_view_unregister(FsearchDatabaseView *view);

FsearchDatabaseEntry *
db_view_get_entry(FsearchDatabaseView *view, uint32_t idx);

FsearchQueryFlags
db_view_get_query_flags(FsearchDatabaseView *view);

FsearchQuery *
db_view_get_query(FsearchDatabaseView *view);

void
db_view_select_toggle(FsearchDatabaseView *view, uint32_t idx);

void
db_view_select(FsearchDatabaseView *view, uint32_t idx);

bool
db_view_is_selected(FsearchDatabaseView *view, uint32_t idx);

void
db_view_select_range(FsearchDatabaseView *view, uint32_t start_idx, uint32_t end_idx);

void
db_view_select_all(FsearchDatabaseView *view);

void
db_view_unselect_all(FsearchDatabaseView *view);

void
db_view_invert_selection(FsearchDatabaseView *view);

uint32_t
db_view_get_num_selected(FsearchDatabaseView *view);

void
db_view_selection_for_each(FsearchDatabaseView *view, GHFunc func, gpointer user_data);

void
db_view_unlock(FsearchDatabaseView *view);

void
db_view_lock(FsearchDatabaseView *view);

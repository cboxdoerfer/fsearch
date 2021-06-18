#pragma once

#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_filter.h"
#include "fsearch_query.h"
#include "fsearch_query_flags.h"

typedef enum {
    DATABASE_VIEW_NOTIFY_CONTENT_CHANGED,
    DATABASE_VIEW_NOTIFY_SELECTION_CHANGED,
    DATABASE_VIEW_NOTIFY_SEARCH_STARTED,
    DATABASE_VIEW_NOTIFY_SEARCH_FINISHED,
    DATABASE_VIEW_NOTIFY_SORT_STARTED,
    DATABASE_VIEW_NOTIFY_SORT_FINISHED,
} FsearchDatabaseViewNotify;

typedef struct FsearchDatabaseView FsearchDatabaseView;

typedef void (*FsearchDatabaseViewNotifyFunc)(FsearchDatabaseView *view,
                                              FsearchDatabaseViewNotify id,
                                              gpointer user_data);

void
db_view_unref(FsearchDatabaseView *view);

FsearchDatabaseView *
db_view_ref(FsearchDatabaseView *view);

FsearchDatabaseView *
db_view_new(const char *query_text,
            FsearchQueryFlags flags,
            FsearchFilter *filter,
            FsearchDatabaseIndexType sort_order,
            FsearchDatabaseViewNotifyFunc notify_func,
            gpointer notify_func_data);

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

// NOTE: Getters are not thread save, they need to be wrapped with db_view_lock/db_view_unlock
uint32_t
db_view_get_num_folders(FsearchDatabaseView *view);

uint32_t
db_view_get_num_files(FsearchDatabaseView *view);

uint32_t
db_view_get_num_entries(FsearchDatabaseView *view);

FsearchDatabaseIndexType
db_view_get_sort_order(FsearchDatabaseView *view);

GString *
db_view_entry_get_path_for_idx(FsearchDatabaseView *view, uint32_t idx);

GString *
db_view_entry_get_path_full_for_idx(FsearchDatabaseView *view, uint32_t idx);

void
db_view_entry_append_path_for_idx(FsearchDatabaseView *view, uint32_t idx, GString *str);

time_t
db_view_entry_get_mtime_for_idx(FsearchDatabaseView *view, uint32_t idx);

off_t
db_view_entry_get_size_for_idx(FsearchDatabaseView *view, uint32_t idx);

char *
db_view_entry_get_extension_for_idx(FsearchDatabaseView *view, uint32_t idx);

GString *
db_view_entry_get_name_for_idx(FsearchDatabaseView *view, uint32_t idx);

GString *
db_view_entry_get_name_raw_for_idx(FsearchDatabaseView *view, uint32_t idx);

int32_t
db_view_entry_get_parent_for_idx(FsearchDatabaseView *view, uint32_t idx);

FsearchDatabaseEntryType
db_view_entry_get_type_for_idx(FsearchDatabaseView *view, uint32_t idx);

void
db_view_register(FsearchDatabase *db, FsearchDatabaseView *view);

void
db_view_unregister(FsearchDatabaseView *view);

FsearchQueryFlags
db_view_get_query_flags(FsearchDatabaseView *view);

FsearchQuery *
db_view_get_query(FsearchDatabaseView *view);

// NOTE: Selection handlers are thread safe
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

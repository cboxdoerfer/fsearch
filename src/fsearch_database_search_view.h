#pragma once

#include "fsearch_array.h"
#include "fsearch_query.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_search_info.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gmacros.h>
#include <gtk/gtkenums.h>
#include <stdbool.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct FsearchDatabaseSearchView FsearchDatabaseSearchView;

FsearchDatabaseSearchView *
fsearch_database_search_view_new(uint32_t id,
                                 FsearchQuery *query,
                                 DynamicArray *files,
                                 DynamicArray *folders,
                                 GHashTable *old_selection,
                                 FsearchDatabaseIndexProperty sort_order,
                                 FsearchDatabaseIndexProperty secondary_sort_order,
                                 GtkSortType sort_type);

void
fsearch_database_search_view_free(FsearchDatabaseSearchView *view);

// Manipulation
void
fsearch_database_search_view_add(FsearchDatabaseSearchView *view, DynamicArray *files, DynamicArray *folders);

void
fsearch_database_search_view_remove(FsearchDatabaseSearchView *view, DynamicArray *files, DynamicArray *folders);

void
fsearch_database_search_view_sort(FsearchDatabaseSearchView *view,
                                  DynamicArray *files_fast_sorted,
                                  DynamicArray *folders_fast_sorted,
                                  FsearchDatabaseIndexProperty sort_order,
                                  GtkSortType sort_type,
                                  GCancellable *cancellable);

// Selection handling
bool
fsearch_database_search_view_is_selected(FsearchDatabaseSearchView *view, FsearchDatabaseEntry *entry);
void
fsearch_database_search_view_select_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx);
void
fsearch_database_search_view_toggle_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx);
void
fsearch_database_search_view_select_all(FsearchDatabaseSearchView *view);
void
fsearch_database_search_view_invert_selection(FsearchDatabaseSearchView *view);
void
fsearch_database_search_view_clear_selection(FsearchDatabaseSearchView *view);
void
fsearch_database_search_view_selection_foreach(FsearchDatabaseSearchView *view, GHFunc func, gpointer user_data);

// Getters
FsearchDatabaseSearchInfo *
fsearch_database_search_view_get_info(FsearchDatabaseSearchView *view);

FsearchDatabaseEntry *
fsearch_database_search_view_get_entry_for_idx(FsearchDatabaseSearchView *view, uint32_t idx);

FsearchQuery *
fsearch_database_search_view_get_query(FsearchDatabaseSearchView *view);

G_END_DECLS
#pragma once

#include "fsearch_array.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_search_info.h"
#include "fsearch_query.h"
#include "fsearch_selection_type.h"

#include <glib/gmacros.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtkenums.h>
#include <stdbool.h>
#include <stdint.h>

G_BEGIN_DECLS

// Forward declaration
typedef struct FsearchDatabaseSearchView FsearchDatabaseSearchView;

typedef struct FsearchDatabaseIndexStore FsearchDatabaseIndexStore;

typedef enum {
    FSEARCH_DATABASE_INDEX_STORE_EVENT_CONTENT_CHANGED,
    FSEARCH_DATABASE_INDEX_STORE_EVENT_PROGRESS,
    FSEARCH_DATABASE_INDEX_STORE_EVENT_VIEW_CHANGED,
    NUM_FSEARCH_DATABASE_STORE_EVENTS,
} FsearchDatabaseIndexStoreEventKind;

typedef void
(*FsearchDatabaseIndexStoreEventFunc)(FsearchDatabaseIndexStore *store,
                                      FsearchDatabaseIndexStoreEventKind kind,
                                      gpointer data,
                                      gpointer user_data);

// Object management
FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIncludeManager *include_manager,
                                 FsearchDatabaseExcludeManager *exclude_manager,
                                 FsearchDatabaseIndexPropertyFlags flags,
                                 FsearchDatabaseIndexStoreEventFunc event_func,
                                 gpointer event_func_data);

FsearchDatabaseIndexStore *
fsearch_database_index_store_new_with_content(GPtrArray *indices,
                                              DynamicArray **files,
                                              DynamicArray **folders,
                                              FsearchDatabaseIncludeManager *include_manager,
                                              FsearchDatabaseExcludeManager *exclude_manager,
                                              FsearchDatabaseIndexPropertyFlags flags,
                                              FsearchDatabaseIndexStoreEventFunc event_func,
                                              gpointer event_func_data);

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *store);
void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *store);

// Lifecycle

void
fsearch_database_index_store_start(FsearchDatabaseIndexStore *store, GCancellable *cancellable);
void
fsearch_database_index_store_start_monitoring(FsearchDatabaseIndexStore *store);

FsearchDatabaseIndex *
fsearch_database_index_store_create_index_for_rescan(FsearchDatabaseIndexStore *store,
                                                     uint32_t index_id);

bool
fsearch_database_index_store_replace_index(FsearchDatabaseIndexStore *store,
                                           FsearchDatabaseIndex *new_index);

// Getters
FsearchDatabaseChunkedArray *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *store,
                                       FsearchDatabaseIndexProperty sort_order);

FsearchDatabaseChunkedArray *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *store,
                                         FsearchDatabaseIndexProperty sort_order);

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *store);

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *store);

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *store);

FsearchDatabaseIncludeManager *
fsearch_database_index_store_get_include_manager(FsearchDatabaseIndexStore *store);

FsearchDatabaseExcludeManager *
fsearch_database_index_store_get_exclude_manager(FsearchDatabaseIndexStore *store);

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *store);

FsearchDatabaseSearchView *
fsearch_database_index_store_get_search_view(FsearchDatabaseIndexStore *store, uint32_t view_id);

FsearchDatabaseEntryInfo *
fsearch_database_index_store_get_entry_info(FsearchDatabaseIndexStore *store,
                                            uint32_t idx,
                                            uint32_t id,
                                            FsearchDatabaseEntryInfoFlags flags);

FsearchDatabaseSearchInfo *
fsearch_database_index_store_get_search_info(FsearchDatabaseIndexStore *store, uint32_t id);

bool
fsearch_database_index_store_has_chunks(FsearchDatabaseIndexStore *store,
                                        FsearchDatabaseChunkedArray *chunks);

// Manipulation
GMutexLocker *
fsearch_database_index_store_get_locker(FsearchDatabaseIndexStore *store);

gboolean
fsearch_database_index_store_trylock(FsearchDatabaseIndexStore *store);

void
fsearch_database_index_store_lock(FsearchDatabaseIndexStore *store);

void
fsearch_database_index_store_unlock(FsearchDatabaseIndexStore *store);

void
fsearch_database_index_store_sort_results(FsearchDatabaseIndexStore *store,
                                          uint32_t id,
                                          FsearchDatabaseIndexProperty sort_order,
                                          GtkSortType sort_type,
                                          GCancellable *cancellable);
bool
fsearch_database_index_store_search(FsearchDatabaseIndexStore *store,
                                    uint32_t id,
                                    FsearchQuery *query,
                                    FsearchDatabaseIndexProperty sort_order,
                                    GtkSortType sort_type,
                                    GCancellable *cancellable);

void
fsearch_database_index_store_modify_selection(FsearchDatabaseIndexStore *store,
                                              uint32_t view_id,
                                              FsearchSelectionType type,
                                              int32_t start_idx,
                                              int32_t end_idx);

void
fsearch_database_index_store_selection_foreach(FsearchDatabaseIndexStore *store,
                                               uint32_t view_id,
                                               GHFunc func,
                                               gpointer user_data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndexStore, fsearch_database_index_store_unref)

G_END_DECLS
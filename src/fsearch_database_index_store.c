#define G_LOG_DOMAIN "fsearch-database-index-store"

#include "fsearch_database_index_store.h"

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_query.h"
#include "fsearch_query_match_data.h"

#include <glib.h>
#include <glibconfig.h>
#include <glib/gmacros.h>
#include <gio/gio.h>
#include <gobject/gobject.h>
#include <gtk/gtkenums.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define THRESHOLD_FOR_PARALLEL_SEARCH 1000

typedef struct {
    GThread *thread;
    GMainLoop *loop;
    GMainContext *ctx;
} FsearchDatabaseThreadContext;

struct FsearchDatabaseIndexStore {
    // Array of FsearchDatabaseIndex's
    GPtrArray *indices;

    // Hash table to all search results
    GHashTable *search_results;

    // Sorted "lists" of all entries in `indices`
    FsearchDatabaseEntriesContainer *file_container[NUM_DATABASE_INDEX_PROPERTIES];
    FsearchDatabaseEntriesContainer *folder_container[NUM_DATABASE_INDEX_PROPERTIES];

    // Include/Exclude configuration
    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    GThreadPool *worker_pool;
    GAsyncQueue *worker_pool_collect_queue;

    // Gets called on every FsearchDatabaseIndex event
    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_func_data;

    // Stores which properties have been indexed
    FsearchDatabaseIndexPropertyFlags flags;

    // Shared thread where all indices can listen for file system change events and queue them for being processed later
    FsearchDatabaseThreadContext monitor;
    // Shared thread where all indices can process file system change events
    FsearchDatabaseThreadContext worker;

    bool is_sorted;
    bool running;

    GMutex mutex;

    volatile gint ref_count;
};

static gboolean
thread_quit_func(GMainLoop *loop) {
    g_return_val_if_fail(loop, G_SOURCE_REMOVE);

    g_main_loop_quit(loop);

    return G_SOURCE_REMOVE;
}

static void
thread_func(GMainContext *ctx, GMainLoop *loop) {
    g_main_context_push_thread_default(ctx);
    g_main_loop_run(loop);
    g_main_context_pop_thread_default(ctx);
    g_main_loop_unref(loop);
}

static gpointer
index_store_worker_thread_func(gpointer user_data) {
    const FsearchDatabaseIndexStore *store = user_data;
    g_return_val_if_fail(store, NULL);

    thread_func(store->worker.ctx, store->worker.loop);

    return NULL;
}

static gpointer
index_store_monitor_thread_func(gpointer user_data) {
    const FsearchDatabaseIndexStore *store = user_data;
    g_return_val_if_fail(store, NULL);

    thread_func(store->monitor.ctx, store->monitor.loop);

    return NULL;
}

static void
index_store_sorted_entries_free(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (store->file_container[i]) {
            g_clear_pointer(&store->file_container[i], fsearch_database_entries_container_unref);
        }
        if (store->folder_container[i]) {
            g_clear_pointer(&store->folder_container[i], fsearch_database_entries_container_unref);
        }
    }
}

static void
index_store_unlock_all_indices(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(store->indices, i);
        fsearch_database_index_unlock(index_stored);
    }
}

static void
index_store_lock_all_indices(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(store->indices, i);
        fsearch_database_index_lock(index_stored);
    }
}

static bool
index_store_flags_equal(const FsearchDatabaseIndexStore *store, FsearchDatabaseIndexPropertyFlags flags) {
    g_assert(store);

    const FsearchDatabaseIndexPropertyFlags store_flags = store->flags;
    return (store_flags & flags) == store_flags;
}

static bool
index_store_has_index_with_same_id(const FsearchDatabaseIndexStore *store, FsearchDatabaseIndex *index) {
    g_assert(store);
    g_assert(index);

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(store->indices, i);
        if (fsearch_database_index_get_id(index_stored) == fsearch_database_index_get_id(index)) {
            return true;
        }
    }
    return false;
}

static void
index_store_search_worker(FsearchQuery *query,
                          DynamicArray *entries,
                          DynamicArray *results,
                          int32_t thread_id,
                          uint32_t start_idx,
                          uint32_t end_idx,
                          GCancellable *cancellable) {
    g_assert(entries);
    g_assert(start_idx <= end_idx);
    g_assert(end_idx < darray_get_num_items(entries));

    FsearchQueryMatchData *match_data = fsearch_query_match_data_new(NULL, NULL);

    fsearch_query_match_data_set_thread_id(match_data, thread_id);

    for (uint32_t i = start_idx; i <= end_idx; i++) {
        if (G_UNLIKELY(g_cancellable_is_cancelled(cancellable))) {
            break;
        }
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        fsearch_query_match_data_set_entry(match_data, entry);
        if (fsearch_query_match(query, match_data)) {
            darray_add_item(results, entry);
        }
    }
    g_clear_pointer(&match_data, fsearch_query_match_data_free);

}

static void
index_store_remove_from_store_worker(FsearchDatabaseEntriesContainer *container, DynamicArray *entries) {
    g_return_if_fail(container);
    g_return_if_fail(entries);

    for (uint32_t j = 0; j < darray_get_num_items(entries); ++j) {
        FsearchDatabaseEntry *entry = darray_get_item(entries, j);
        if (!fsearch_database_entries_container_steal(container, entry)) {
            g_debug("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
        }
    }
}

typedef enum {
    INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH = 0,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_ENTRIES,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_ENTRIES,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_TO_RESULTS,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_FROM_RESULTS,
    NUM_INDEX_STORE_WORKER_POOL_DATA_TYPES,
} IndexStoreWorkerPoolDataType;

typedef struct {
    IndexStoreWorkerPoolDataType type;

    union {
        struct {
            FsearchQuery *query;
            GCancellable *cancellable;
            DynamicArray *in;
            DynamicArray *out;
            uint32_t in_start_idx;
            uint32_t in_end_idx;
            int32_t thread_id;
        } search;

        struct {
            FsearchDatabaseEntriesContainer *container;
            DynamicArray *entries;
        } update_store;

        struct {
            FsearchDatabaseSearchView *view;
            DynamicArray *files;
            DynamicArray *folders;
        } update_results;
    };
} IndexStoreWorkerPoolData;

static void
index_store_worker_pool_func(gpointer pool_data, gpointer user_data) {
    FsearchDatabaseIndexStore *store = user_data;
    g_return_if_fail(store);

    IndexStoreWorkerPoolData *data = pool_data;
    g_return_if_fail(data);

    switch (data->type) {
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH: {
        index_store_search_worker(data->search.query,
                                  data->search.in,
                                  data->search.out,
                                  data->search.thread_id,
                                  data->search.in_start_idx,
                                  data->search.in_end_idx,
                                  data->search.cancellable);
        g_async_queue_push(store->worker_pool_collect_queue, data);
        break;
    }
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_ENTRIES: {
        fsearch_database_entries_container_insert_array(data->update_store.container, data->update_store.entries);
        g_async_queue_push(store->worker_pool_collect_queue, data);
        break;
    }
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_ENTRIES: {
        index_store_remove_from_store_worker(data->update_store.container, data->update_store.entries);
        g_async_queue_push(store->worker_pool_collect_queue, data);
        break;
    }
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_TO_RESULTS: {
        fsearch_database_search_view_add(data->update_results.view,
                                         data->update_results.files,
                                         data->update_results.folders);
        g_async_queue_push(store->worker_pool_collect_queue, data);
        break;
    }
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_FROM_RESULTS: {
        fsearch_database_search_view_remove(data->update_results.view,
                                            data->update_results.files,
                                            data->update_results.folders);
        g_async_queue_push(store->worker_pool_collect_queue, data);
        break;
    }
    default:
        g_assert_not_reached();
        break;
    }
}

static void
index_store_free(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    if (store->monitor.loop) {
        g_main_context_invoke_full(store->monitor.ctx,
                                   G_PRIORITY_HIGH,
                                   (GSourceFunc)thread_quit_func,
                                   store->monitor.loop,
                                   NULL);
    }
    if (store->monitor.thread) {
        g_thread_join(store->monitor.thread);
    }
    g_clear_pointer(&store->monitor.ctx, g_main_context_unref);

    if (store->worker.loop) {
        g_main_context_invoke_full(store->worker.ctx,
                                   G_PRIORITY_HIGH,
                                   (GSourceFunc)thread_quit_func,
                                   store->worker.loop,
                                   NULL);
    }
    if (store->worker.thread) {
        g_thread_join(store->worker.thread);
    }
    g_clear_pointer(&store->worker.ctx, g_main_context_unref);

    g_thread_pool_free(g_steal_pointer(&store->worker_pool), FALSE, FALSE);
    g_clear_pointer(&store->worker_pool_collect_queue, g_async_queue_unref);

    g_clear_pointer(&store->search_results, g_hash_table_unref);
    index_store_sorted_entries_free(store);
    g_clear_pointer(&store->indices, g_ptr_array_unref);
    g_clear_object(&store->include_manager);
    g_clear_object(&store->exclude_manager);

    g_mutex_clear(&store->mutex);

    g_slice_free(FsearchDatabaseIndexStore, store);
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIncludeManager *include_manager,
                                 FsearchDatabaseExcludeManager *exclude_manager,
                                 FsearchDatabaseIndexPropertyFlags flags,
                                 FsearchDatabaseIndexEventFunc event_func,
                                 gpointer event_func_data) {
    FsearchDatabaseIndexStore *store = g_slice_new0(FsearchDatabaseIndexStore);

    store->indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    store->search_results =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)fsearch_database_search_view_free);

    store->flags = flags;
    store->is_sorted = false;
    store->running = false;

    store->include_manager = g_object_ref(include_manager);
    store->exclude_manager = g_object_ref(exclude_manager);

    store->event_func = event_func;
    store->event_func_data = event_func_data;

    store->worker_pool = g_thread_pool_new(index_store_worker_pool_func, store, g_get_num_processors(), TRUE, NULL);
    store->worker_pool_collect_queue = g_async_queue_new();

    store->monitor.ctx = g_main_context_new();
    store->monitor.loop = g_main_loop_new(store->monitor.ctx, FALSE);
    store->monitor.thread = g_thread_new("FsearchDatabaseIndexStoreMonitor", index_store_monitor_thread_func, store);

    store->worker.ctx = g_main_context_new();
    store->worker.loop = g_main_loop_new(store->worker.ctx, FALSE);
    store->worker.thread = g_thread_new("FsearchDatabaseIndexStoreWorker", index_store_worker_thread_func, store);

    g_mutex_init(&store->mutex);

    store->ref_count = 1;

    return store;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new_with_content(GPtrArray *indices,
                                              DynamicArray **files,
                                              DynamicArray **folders,
                                              FsearchDatabaseIncludeManager *include_manager,
                                              FsearchDatabaseExcludeManager *exclude_manager,
                                              FsearchDatabaseIndexPropertyFlags flags,
                                              FsearchDatabaseIndexEventFunc event_func,
                                              gpointer event_func_data) {
    FsearchDatabaseIndexStore *store = fsearch_database_index_store_new(include_manager,
                                                                        exclude_manager,
                                                                        flags,
                                                                        event_func,
                                                                        event_func_data);

    g_clear_pointer(&store->indices, g_ptr_array_unref);
    store->indices = g_ptr_array_ref(indices);

    for (uint32_t i = DATABASE_INDEX_PROPERTY_NAME; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        DynamicArray *s_files = files[i];
        DynamicArray *s_folders = folders[i];
        if (s_folders && s_files) {
            store->folder_container[i] = fsearch_database_entries_container_new(
                s_folders,
                TRUE,
                i,
                DATABASE_INDEX_PROPERTY_NONE,
                DATABASE_ENTRY_TYPE_FOLDER,
                NULL,
                NULL);
            store->file_container[i] = fsearch_database_entries_container_new(
                s_files,
                TRUE,
                i,
                DATABASE_INDEX_PROPERTY_NONE,
                DATABASE_ENTRY_TYPE_FILE,
                NULL,
                NULL);
        }
    }

    store->is_sorted = true;
    store->running = true;

    return store;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store != NULL, NULL);
    g_return_val_if_fail(store->ref_count > 0, NULL);

    g_atomic_int_inc(&store->ref_count);

    return store;
}

void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store != NULL);
    g_return_if_fail(store->ref_count > 0);

    if (g_atomic_int_dec_and_test(&store->ref_count)) {
        g_clear_pointer(&store, index_store_free);
    }
}

/* Lifecycle */
void
fsearch_database_index_store_start(FsearchDatabaseIndexStore *store, GCancellable *cancellable) {
    g_return_if_fail(store);
    if (store->running) {
        return;
    }

    g_autoptr(GPtrArray) indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(store->include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        g_autoptr(FsearchDatabaseIndex) index = fsearch_database_index_new(fsearch_database_include_get_id(include),
                                                                           include,
                                                                           store->exclude_manager,
                                                                           store->flags,
                                                                           store->worker.ctx,
                                                                           store->monitor.ctx,
                                                                           store->event_func,
                                                                           store->event_func_data);
        if (index && fsearch_database_index_scan(index, cancellable)) {
            g_ptr_array_add(indices, g_steal_pointer(&index));
        }
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    g_autoptr(DynamicArray) store_files = darray_new(1024);
    g_autoptr(DynamicArray) store_folders = darray_new(1024);
    for (uint32_t i = 0; i < indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(indices, i);

        if (index_store_has_index_with_same_id(store, index)
            || !index_store_flags_equal(store, fsearch_database_index_get_flags(index))) {
            continue;
        }
        g_ptr_array_add(store->indices, fsearch_database_index_ref(index));
        fsearch_database_index_lock(index);
        g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);
        g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
        darray_add_array(store_files, files);
        darray_add_array(store_folders, folders);

        fsearch_database_index_unlock(index);

        store->is_sorted = false;
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    //signal_emit(db, SIGNAL_DATABASE_PROGRESS, g_strdup(_("Sorting…")), NULL, 1, (GDestroyNotify)free, NULL);

    index_store_lock_all_indices(store);
    store->folder_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable,
                                               NULL);
    store->file_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable,
                                               NULL);
    store->folder_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable,
                                               NULL);
    store->file_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable,
                                               NULL);
    store->folder_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable,
                                               NULL);
    store->file_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable,
                                               NULL);
    store->folder_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable,
                                               NULL);
    store->file_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable,
                                               NULL);
    store->folder_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable,
                                               NULL);
    store->file_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable,
                                               NULL);
    store->is_sorted = true;
    index_store_unlock_all_indices(store);

    if (g_cancellable_is_cancelled(cancellable)) {
        index_store_sorted_entries_free(store);
        g_ptr_array_remove_range(store->indices, 0, store->indices->len);
        return;
    }

    store->running = true;

    return;
}

void
fsearch_database_index_store_start_monitoring(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);
    index_store_lock_all_indices(store);

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        fsearch_database_index_start_monitoring(index, true);
    }

    index_store_unlock_all_indices(store);
}

GMutexLocker *
fsearch_database_index_store_get_locker(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, NULL);
    return g_mutex_locker_new(&store->mutex);
}

gboolean
fsearch_database_index_store_trylock(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, FALSE);
    return g_mutex_trylock(&store->mutex);
}

void
fsearch_database_index_store_lock(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);
    g_mutex_lock(&store->mutex);
}

void
fsearch_database_index_store_unlock(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);
    g_mutex_unlock(&store->mutex);
}

/* Data Accessors */
FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *store,
                                       FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(store, NULL);
    g_return_val_if_fail(store->is_sorted, NULL);

    return store->file_container[sort_order]
               ? fsearch_database_entries_container_ref(store->file_container[sort_order])
               : NULL;
}

FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *store,
                                         FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(store, NULL);
    g_return_val_if_fail(store->is_sorted, NULL);

    return store->folder_container[sort_order]
               ? fsearch_database_entries_container_ref(store->folder_container[sort_order])
               : NULL;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, DATABASE_INDEX_PROPERTY_FLAG_NONE);
    return store->flags;
}

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    return store->file_container[DATABASE_INDEX_PROPERTY_NAME]
               ? fsearch_database_entries_container_get_num_entries(store->file_container[DATABASE_INDEX_PROPERTY_NAME])
               : 0;
}

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    return store->folder_container[DATABASE_INDEX_PROPERTY_NAME]
               ? fsearch_database_entries_container_get_num_entries(
                   store->folder_container[DATABASE_INDEX_PROPERTY_NAME])
               : 0;
}

FsearchDatabaseIncludeManager *
fsearch_database_index_store_get_include_manager(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, NULL);
    return g_object_ref(store->include_manager);
}

FsearchDatabaseExcludeManager *
fsearch_database_index_store_get_exclude_manager(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, NULL);
    return g_object_ref(store->exclude_manager);
}

FsearchDatabaseSearchView *
fsearch_database_index_store_get_search_view(FsearchDatabaseIndexStore *store, uint32_t view_id) {
    g_return_val_if_fail(store, NULL);
    g_return_val_if_fail(store->search_results, NULL);
    return g_hash_table_lookup(store->search_results, GUINT_TO_POINTER(view_id));
}

FsearchDatabaseEntryInfo *
fsearch_database_index_store_get_entry_info(FsearchDatabaseIndexStore *store,
                                            uint32_t idx,
                                            uint32_t id,
                                            FsearchDatabaseEntryInfoFlags flags) {
    g_return_val_if_fail(store, NULL);

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, id);
    if (!view) {
        return NULL;
    }

    FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, idx);
    if (!entry) {
        return NULL;
    }

    g_autoptr(FsearchQuery) query = fsearch_database_search_view_get_query(view);

    return fsearch_database_entry_info_new(entry,
                                           query,
                                           idx,
                                           fsearch_database_search_view_is_selected(view, entry),
                                           flags);
}

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    uint32_t num_fast_sort_indices = 0;
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (store->folder_container[i] && store->file_container[i]) {
            num_fast_sort_indices++;
        }
    }

    return num_fast_sort_indices;
}

FsearchDatabaseSearchInfo *
fsearch_database_index_store_get_search_info(FsearchDatabaseIndexStore *store, uint32_t id) {
    g_return_val_if_fail(store, NULL);
    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, id);
    if (!view) {
        return NULL;
    }
    return fsearch_database_search_view_get_info(view);
}

static void
add_search_info(gpointer key, gpointer value, gpointer user_data) {
    GPtrArray *infos = user_data;
    FsearchDatabaseSearchView *view = value;
    g_ptr_array_add(infos, fsearch_database_search_view_get_info(view));
}

GPtrArray *
fsearch_database_index_store_get_search_infos(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, NULL);
    g_return_val_if_fail(store->search_results, NULL);

    g_autoptr(GPtrArray) infos = g_ptr_array_new_full(g_hash_table_size(store->search_results),
                                                      (GDestroyNotify)fsearch_database_search_info_unref);
    g_hash_table_foreach(store->search_results, (GHFunc)add_search_info, infos);
    return g_steal_pointer(&infos);
}

typedef struct {
    FsearchDatabaseIndexStore *store;
    DynamicArray *folders;
    DynamicArray *files;
    uint32_t *num_workers;
} FsearchDatabaseIndexStoreAddRemoveContext;

static void
search_view_results_add_cb(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseIndexStoreAddRemoveContext *ctx = user_data;
    g_return_if_fail(ctx);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_TO_RESULTS;
    pool_data->update_results.view = view;
    pool_data->update_results.files = ctx->files;
    pool_data->update_results.folders = ctx->folders;

    (*ctx->num_workers)++;

    g_thread_pool_push(ctx->store->worker_pool, pool_data, NULL);
}

static void
add_entries(FsearchDatabaseEntriesContainer *container, DynamicArray *entries, GThreadPool *pool, uint32_t *num_workers) {
    g_return_if_fail(container);
    g_return_if_fail(entries);
    g_return_if_fail(pool);
    g_return_if_fail(num_workers);

    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_ADD_ENTRIES;
    pool_data->update_store.container = container;
    pool_data->update_store.entries = entries;

    *num_workers += 1;

    g_thread_pool_push(pool, pool_data, NULL);
}

void
fsearch_database_index_store_add_entries(FsearchDatabaseIndexStore *store, DynamicArray *files, DynamicArray *folders) {
    g_return_if_fail(store);

    uint32_t num_workers = 0;

    FsearchDatabaseIndexStoreAddRemoveContext ctx = {
        .store = store,
        .folders = folders,
        .files = files,
        .num_workers = &num_workers,
    };

    g_hash_table_foreach(store->search_results, search_view_results_add_cb, &ctx);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (files && store->file_container[i]) {
            add_entries(store->file_container[i], files, store->worker_pool, &num_workers);
        }
        if (folders && store->folder_container[i]) {
            add_entries(store->folder_container[i], folders, store->worker_pool, &num_workers);
        }
    }

    uint32_t collected_wrokers = 0;
    while (collected_wrokers < num_workers) {
        g_autofree IndexStoreWorkerPoolData *pool_data = g_async_queue_pop(store->worker_pool_collect_queue);
        g_assert_nonnull(pool_data);
        collected_wrokers++;
    }
}

static void
remove_entries(FsearchDatabaseEntriesContainer *container, DynamicArray *entries, GThreadPool *pool, uint32_t *num_workers) {
    g_return_if_fail(container);
    g_return_if_fail(entries);
    g_return_if_fail(pool);
    g_return_if_fail(num_workers);

    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_ENTRIES;
    pool_data->update_store.container = container;
    pool_data->update_store.entries = entries;

    *num_workers += 1;

    g_thread_pool_push(pool, pool_data, NULL);
}

static void
search_view_results_remove_cb(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseIndexStoreAddRemoveContext *ctx = user_data;
    g_return_if_fail(ctx);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_REMOVE_FROM_RESULTS;
    pool_data->update_results.view = view;
    pool_data->update_results.files = ctx->files;
    pool_data->update_results.folders = ctx->folders;

    (*ctx->num_workers)++;

    g_thread_pool_push(ctx->store->worker_pool, pool_data, NULL);
}

void
fsearch_database_index_store_remove_entries(FsearchDatabaseIndexStore *store,
                                            DynamicArray *files,
                                            DynamicArray *folders) {
    g_return_if_fail(store);

    uint32_t num_workers = 0;

    FsearchDatabaseIndexStoreAddRemoveContext ctx = {
        .store = store,
        .folders = folders,
        .files = files,
        .num_workers = &num_workers,
    };

    g_hash_table_foreach(store->search_results, search_view_results_remove_cb, &ctx);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (files && store->file_container[i]) {
            remove_entries(store->file_container[i], files, store->worker_pool, &num_workers);
        }
        if (folders && store->folder_container[i]) {
            remove_entries(store->folder_container[i], folders, store->worker_pool, &num_workers);
        }
    }

    uint32_t collected_wrokers = 0;
    while (collected_wrokers < num_workers) {
        g_autofree IndexStoreWorkerPoolData *pool_data = g_async_queue_pop(store->worker_pool_collect_queue);
        g_assert_nonnull(pool_data);
        collected_wrokers++;
    }

}

void
fsearch_database_index_store_sort_results(FsearchDatabaseIndexStore *store,
                                          uint32_t id,
                                          FsearchDatabaseIndexProperty sort_order,
                                          GtkSortType sort_type,
                                          GCancellable *cancellable) {
    g_return_if_fail(store);
    g_return_if_fail(store->search_results);

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, id);
    if (!view) {
        return;
    }

    g_autoptr(FsearchDatabaseEntriesContainer) files_fast_sort_index = fsearch_database_index_store_get_files(
        store,
        sort_order);
    g_autoptr(FsearchDatabaseEntriesContainer) folders_fast_sort_index =
        fsearch_database_index_store_get_folders(
            store,
            sort_order);

    g_autoptr(DynamicArray) files_fast_sorted = NULL;
    g_autoptr(DynamicArray) folders_fast_sorted = NULL;
    if (files_fast_sort_index && folders_fast_sort_index) {
        files_fast_sorted = fsearch_database_entries_container_get_joined(files_fast_sort_index);
        folders_fast_sorted = fsearch_database_entries_container_get_joined(folders_fast_sort_index);
    }
    fsearch_database_search_view_sort(view,
                                      files_fast_sorted,
                                      folders_fast_sorted,
                                      sort_order,
                                      sort_type,
                                      cancellable);
}

static DynamicArray *
collect_search_results(IndexStoreWorkerPoolData *data_array[], uint32_t data_array_len) {
    uint32_t num_entries_found = 0;
    for (uint32_t i = 0; i < data_array_len; ++i) {
        IndexStoreWorkerPoolData *data = data_array[i];
        num_entries_found += darray_get_num_items(data->search.out);
    }
    g_autoptr(DynamicArray) search_entries = darray_new(num_entries_found);

    for (uint32_t i = 0; i < data_array_len; ++i) {
        IndexStoreWorkerPoolData *data = data_array[i];
        darray_add_array(search_entries, data->search.out);

        g_clear_pointer(&data->search.out, darray_unref);
        g_clear_pointer(&data, free);
    }

    return g_steal_pointer(&search_entries);
}

static inline uint32_t
sub_or_zero_u32(uint32_t a, uint32_t b) {
    return a > b ? a - b : 0;
}

static DynamicArray *
search_entries(FsearchQuery *query,
               DynamicArray *in,
               GThreadPool *pool,
               GAsyncQueue *collect_queue,
               GCancellable *cancellable) {
    const uint32_t num_entries = darray_get_num_items(in);
    const uint32_t num_threads = (num_entries < THRESHOLD_FOR_PARALLEL_SEARCH || query->wants_single_threaded_search)
                                     ? 1
                                     : g_thread_pool_get_num_threads(pool);
    const uint32_t num_items_per_thread = num_entries / num_threads;
    IndexStoreWorkerPoolData *pool_data[num_threads + 1] = {};

    uint32_t start_pos = 0;
    uint32_t end_pos = sub_or_zero_u32(num_items_per_thread, 1);
    const uint32_t last_end_pos = sub_or_zero_u32(num_entries, 1);

    for (uint32_t i = 0; i < num_threads; ++i) {
        pool_data[i] = g_new0(IndexStoreWorkerPoolData, 1);
        pool_data[i]->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH;
        pool_data[i]->search.in = in;
        pool_data[i]->search.query = query;
        pool_data[i]->search.cancellable = cancellable;
        pool_data[i]->search.thread_id = (int32_t)i;
        pool_data[i]->search.in_start_idx = start_pos;
        pool_data[i]->search.in_end_idx = i == num_threads - 1 ? last_end_pos : end_pos;
        pool_data[i]->search.out = darray_new(end_pos - start_pos + 1);
        g_thread_pool_push(pool, pool_data[i], NULL);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;
    }

    uint32_t num_threads_collected = 0;
    while (num_threads_collected < num_threads) {
        g_async_queue_pop(collect_queue);
        num_threads_collected++;
    }

    return collect_search_results(pool_data, num_threads);
}

bool
fsearch_database_index_store_search(FsearchDatabaseIndexStore *store,
                                    uint32_t id,
                                    FsearchQuery *query,
                                    FsearchDatabaseIndexProperty sort_order,
                                    GtkSortType sort_type,
                                    GCancellable *cancellable) {
    g_return_val_if_fail(store, false);
    g_return_val_if_fail(store->search_results, false);

    g_autoptr(FsearchDatabaseEntriesContainer) file_container = fsearch_database_index_store_get_files(
        store,
        sort_order);
    g_autoptr(FsearchDatabaseEntriesContainer) folder_container = fsearch_database_index_store_get_folders(
        store,
        sort_order);

    if (!file_container && !folder_container) {
        sort_order = DATABASE_INDEX_PROPERTY_NAME;
        file_container = fsearch_database_index_store_get_files(store, sort_order);
        folder_container = fsearch_database_index_store_get_folders(store, sort_order);
    }

    g_autoptr(DynamicArray) files = fsearch_database_entries_container_get_joined(file_container);
    g_autoptr(DynamicArray) folders = fsearch_database_entries_container_get_joined(folder_container);

    const bool matches_everything = fsearch_query_matches_everything(query);
    g_autoptr(DynamicArray) found_files = matches_everything
                                              ? g_steal_pointer(&files)
                                              : search_entries(query,
                                                               files,
                                                               store->worker_pool,
                                                               store->worker_pool_collect_queue,
                                                               cancellable);
    g_autoptr(DynamicArray) found_folders = matches_everything
                                                ? g_steal_pointer(&folders)
                                                : search_entries(query,
                                                                 folders,
                                                                 store->worker_pool,
                                                                 store->worker_pool_collect_queue,
                                                                 cancellable);

    if (found_files || found_folders) {
        // After searching the secondary sort order will always be NONE, because we only search in pre-sorted indexes
        FsearchDatabaseSearchView *view = fsearch_database_search_view_new(id,
                                                                           query,
                                                                           found_files,
                                                                           found_folders,
                                                                           NULL,
                                                                           sort_order,
                                                                           DATABASE_INDEX_PROPERTY_NONE,
                                                                           sort_type);
        g_hash_table_insert(store->search_results, GUINT_TO_POINTER(id), view);

        return true;
    }

    return false;
}

void
fsearch_database_index_store_modify_selection(FsearchDatabaseIndexStore *store,
                                              uint32_t view_id,
                                              FsearchSelectionType type,
                                              int32_t start_idx,
                                              int32_t end_idx) {
    g_return_if_fail(store);
    g_return_if_fail(store->search_results);

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, view_id);
    if (!view) {
        return;
    }

    switch (type) {
    case FSEARCH_SELECTION_TYPE_CLEAR:
        fsearch_database_search_view_clear_selection(view);
        break;
    case FSEARCH_SELECTION_TYPE_ALL:
        fsearch_database_search_view_select_all(view);
        break;
    case FSEARCH_SELECTION_TYPE_INVERT:
        fsearch_database_search_view_invert_selection(view);
        break;
    case FSEARCH_SELECTION_TYPE_SELECT:
        fsearch_database_search_view_select_range(view, start_idx, start_idx);
        break;
    case FSEARCH_SELECTION_TYPE_TOGGLE:
        fsearch_database_search_view_toggle_range(view, start_idx, start_idx);
        break;
    case FSEARCH_SELECTION_TYPE_SELECT_RANGE:
        fsearch_database_search_view_select_range(view, start_idx, end_idx);
        break;
    case FSEARCH_SELECTION_TYPE_TOGGLE_RANGE:
        fsearch_database_search_view_toggle_range(view, start_idx, end_idx);
        break;
    case NUM_FSEARCH_SELECTION_TYPES:
        g_assert_not_reached();
    }
}

void
fsearch_database_index_store_selection_foreach(FsearchDatabaseIndexStore *store,
                                               uint32_t view_id,
                                               GHFunc func,
                                               gpointer user_data) {
    g_return_if_fail(store);
    g_return_if_fail(store->search_results);

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, view_id);
    if (!view) {
        return;
    }
    fsearch_database_search_view_selection_foreach(view, func, user_data);
}
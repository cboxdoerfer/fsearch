#define G_LOG_DOMAIN "fsearch-database-index-store"

#include "fsearch_database_index_store.h"

#include "fsearch_array.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_database_sort.h"
#include "fsearch_query.h"
#include "fsearch_query_match_data.h"
#include "fsearch_selection_type.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gobject/gobject.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THRESHOLD_FOR_PARALLEL_SEARCH 1000

typedef struct {
    GThread *thread;
    GMainLoop *loop;
    GMainContext *ctx;
    GSource *event_source;
} FsearchDatabaseThreadContext;

struct FsearchDatabaseIndexStore {
    // Array of FsearchDatabaseIndex's
    GPtrArray *indices;

    // Hash table to all search results
    GHashTable *search_results;

    // Sorted "lists" of all entries in `indices`
    FsearchDatabaseChunkedArray *file_chunks[NUM_DATABASE_INDEX_PROPERTIES];
    FsearchDatabaseChunkedArray *folder_chunks[NUM_DATABASE_INDEX_PROPERTIES];

    // Include/Exclude configuration
    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    GThreadPool *worker_pool;
    GAsyncQueue *worker_pool_collect_queue;

    // Gets called on every FsearchDatabaseIndex event
    FsearchDatabaseIndexStoreEventFunc event_func;
    gpointer event_func_data;

    // Stores which properties have been indexed
    FsearchDatabaseIndexPropertyFlags flags;

    // Shared thread where all indices can listen for file system change events and queue them for being processed later
    FsearchDatabaseThreadContext monitor;
    // Shared thread where all indices can process file system change events
    FsearchDatabaseThreadContext worker;
    GSource *worker_index_root_reappear_poll_source;

    bool is_sorted;
    bool running;

    GMutex mutex;

    volatile gint ref_count;
};

typedef enum {
    INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH = 0,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_STORE,
    INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_RESULTS,
    NUM_INDEX_STORE_WORKER_POOL_DATA_TYPES,
} IndexStoreWorkerPoolDataType;

typedef struct {
    FsearchQuery *query;
    GCancellable *cancellable;
    DynamicArray *chunks;
    DynamicArray **results;
    volatile gint next_chunk_idx;
} IndexStoreSearchContext;

typedef struct {
    IndexStoreWorkerPoolDataType type;

    union {
        struct {
            IndexStoreSearchContext *ctx;
            int32_t thread_id;
        } search;

        struct {
            FsearchDatabaseChunkedArray *chunks;
            DynamicArray *entries;
            bool marked;
            bool add;
        } update_store;

        struct {
            FsearchDatabaseSearchView *view;
            DynamicArray *files;
            DynamicArray *folders;
            FsearchDatabaseIndexPropertyFlags affected_sort_orders;
            bool marked;
            bool add;
        } update_results;
    };
} IndexStoreWorkerPoolData;

typedef struct {
    FsearchDatabaseIndexStore *store;
    DynamicArray *folders;
    DynamicArray *files;
    uint32_t *num_workers;
    FsearchDatabaseIndexPropertyFlags affected_sort_orders;
    bool marked;
    bool add;
} IndexStoreAddRemoveContext;

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
        if (store->file_chunks[i]) {
            g_clear_pointer(&store->file_chunks[i], fsearch_database_chunked_array_unref);
        }
        if (store->folder_chunks[i]) {
            g_clear_pointer(&store->folder_chunks[i], fsearch_database_chunked_array_unref);
        }
    }
}

static void
index_store_free_entries(DynamicArray *files, DynamicArray *folders) {
    // Files first: they reference their parent folders
    for (uint32_t i = 0; files && i < darray_get_num_items(files); ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(files, i);
        g_clear_pointer(&entry, db_entry_free_no_unparent);
    }
    for (uint32_t i = 0; folders && i < darray_get_num_items(folders); ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(folders, i);
        g_clear_pointer(&entry, db_entry_free_no_unparent);
    }
}

// Indices must be detached before, their monitors still reference the folders
static void
index_store_all_entries_free(FsearchDatabaseIndexStore *store) {
    FsearchDatabaseChunkedArray *file_chunks = store->file_chunks[DATABASE_INDEX_PROPERTY_PATH];
    FsearchDatabaseChunkedArray *folder_chunks = store->folder_chunks[DATABASE_INDEX_PROPERTY_PATH];
    g_autoptr(DynamicArray) files = file_chunks ? fsearch_database_chunked_array_get_joined(file_chunks) : NULL;
    g_autoptr(DynamicArray) folders = folder_chunks ? fsearch_database_chunked_array_get_joined(folder_chunks) : NULL;
    index_store_free_entries(files, folders);
}

static bool
index_store_flags_equal(const FsearchDatabaseIndexStore *store, FsearchDatabaseIndexPropertyFlags flags) {
    g_assert(store);

    const FsearchDatabaseIndexPropertyFlags store_flags = store->flags;
    return (store_flags & flags) == store_flags;
}

static bool
index_store_has_index_with_same_path(const FsearchDatabaseIndexStore *store,
                                     FsearchDatabaseIndex *index,
                                     uint32_t *index_idx_out) {
    g_assert(store);
    g_assert(index);

    const char *index_path = fsearch_database_index_get_path(index);
    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(store->indices, i);
        const char *index_stored_path = fsearch_database_index_get_path(index_stored);
        if (g_strcmp0(index_path, index_stored_path) == 0) {
            if (index_idx_out) {
                *index_idx_out = i;
            }
            return true;
        }
    }
    return false;
}

static void
index_store_search_worker(IndexStoreSearchContext *ctx, int32_t thread_id) {
    g_assert(ctx);

    FsearchQueryMatchData *match_data = fsearch_query_match_data_new(NULL, NULL);

    fsearch_query_match_data_set_thread_id(match_data, thread_id);

    const uint32_t num_chunks = darray_get_num_items(ctx->chunks);
    while (true) {
        const uint32_t chunk_idx = (uint32_t)g_atomic_int_add(&ctx->next_chunk_idx, 1);
        if (chunk_idx >= num_chunks || G_UNLIKELY(g_cancellable_is_cancelled(ctx->cancellable))) {
            break;
        }
        DynamicArray *chunk = darray_get_item(ctx->chunks, chunk_idx);
        const uint32_t num_entries = darray_get_num_items(chunk);
        DynamicArray *results = darray_new(MIN(num_entries, 128));
        for (uint32_t i = 0; i < num_entries; i++) {
            if (G_UNLIKELY(g_cancellable_is_cancelled(ctx->cancellable))) {
                break;
            }
            FsearchDatabaseEntry *entry = darray_get_item(chunk, i);
            fsearch_query_match_data_set_entry(match_data, entry);
            if (fsearch_query_match(ctx->query, match_data)) {
                darray_add_item(results, entry);
            }
        }
        ctx->results[chunk_idx] = results;
    }
    g_clear_pointer(&match_data, fsearch_query_match_data_free);
}

static void
index_store_remove_from_store_worker(FsearchDatabaseChunkedArray *chunks, DynamicArray *entries, bool marked) {
    g_return_if_fail(chunks);
    g_return_if_fail(entries);
    const uint32_t num_entries = darray_get_num_items(entries);
    const uint32_t num_total = fsearch_database_chunked_array_get_num_entries(chunks);
    // Bulk removal scans the whole array once (O(num_total)), so the crossover with per-entry
    // steal scales with the array size, not a fixed count. Benchmarks put it near num_total/100
    // across sort orders
    if (marked && num_entries > num_total / 100) {
        // When removing lots of entries, its usually more efficient to walk the entire list of entries and remove
        // marked ones in bulk
        // Mismatches are reported by remove_marked_folders itself
        fsearch_database_chunked_array_remove_marked_folders(chunks, (int32_t)num_entries);
    }
    else {
        // When there are only few entries to be removed, we avoid iterating over the entire list of entires
        // and instead remove them one by one with binary search lookups
        for (uint32_t j = 0; j < num_entries; ++j) {
            FsearchDatabaseEntry *entry = darray_get_item(entries, j);
            if (G_UNLIKELY(!fsearch_database_chunked_array_steal(chunks, entry))) {
                if (!fsearch_database_chunked_array_find_slow(chunks, entry)) {
                    g_warning("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
                    g_assert_not_reached();
                }
                else {
                    g_warning("store: sort order corrupted: %s", db_entry_get_name_raw_for_display(entry));
                    g_assert_not_reached();
                }
            }
        }
    }
}

static void
index_store_enqueue(IndexStoreAddRemoveContext *ctx, IndexStoreWorkerPoolData *pool_data) {
    (*ctx->num_workers)++;
    g_thread_pool_push(ctx->store->worker_pool, pool_data, NULL);
}

static void
index_store_enqueue_update_results_cb(gpointer key, gpointer value, gpointer user_data) {
    IndexStoreAddRemoveContext *ctx = user_data;
    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_RESULTS;
    pool_data->update_results.view = view;
    pool_data->update_results.files = ctx->files;
    pool_data->update_results.folders = ctx->folders;
    pool_data->update_results.affected_sort_orders = ctx->affected_sort_orders;
    pool_data->update_results.marked = ctx->marked;
    pool_data->update_results.add = ctx->add;
    index_store_enqueue(ctx, pool_data);
}

static void
index_store_enqueue_update_store(IndexStoreAddRemoveContext *ctx,
                                 FsearchDatabaseChunkedArray *chunks,
                                 DynamicArray *entries) {
    if (!chunks || !entries) {
        return;
    }
    IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
    pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_STORE;
    pool_data->update_store.chunks = chunks;
    pool_data->update_store.entries = entries;
    pool_data->update_store.marked = ctx->marked;
    pool_data->update_store.add = ctx->add;
    index_store_enqueue(ctx, pool_data);
}

// Applies the change to every affected sorted array and search view in parallel and waits for all of them
static void
index_store_update_entries_locked(FsearchDatabaseIndexStore *store,
                                  DynamicArray *files,
                                  DynamicArray *folders,
                                  FsearchDatabaseIndexPropertyFlags affected_sort_orders,
                                  bool marked,
                                  bool add) {
    g_return_if_fail(store);

    uint32_t num_workers = 0;
    IndexStoreAddRemoveContext ctx = {
        .store = store,
        .folders = folders,
        .files = files,
        .affected_sort_orders = affected_sort_orders,
        .marked = marked,
        .add = add,
        .num_workers = &num_workers,
    };

    g_hash_table_foreach(store->search_results, index_store_enqueue_update_results_cb, &ctx);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (fsearch_database_index_property_is_set(affected_sort_orders, i)) {
            index_store_enqueue_update_store(&ctx, store->file_chunks[i], files);
            index_store_enqueue_update_store(&ctx, store->folder_chunks[i], folders);
        }
    }

    for (uint32_t i = 0; i < num_workers; ++i) {
        g_free(g_async_queue_pop(store->worker_pool_collect_queue));
    }
}

void
fsearch_database_index_store_add_entries_locked(FsearchDatabaseIndexStore *store,
                                                DynamicArray *files,
                                                DynamicArray *folders,
                                                FsearchDatabaseIndexPropertyFlags affected_sort_orders) {
    index_store_update_entries_locked(store, files, folders, affected_sort_orders, false, true);
}

void
fsearch_database_index_store_remove_entries_locked(FsearchDatabaseIndexStore *store,
                                                   DynamicArray *files,
                                                   DynamicArray *folders,
                                                   FsearchDatabaseIndexPropertyFlags affected_sort_orders,
                                                   bool marked) {
    index_store_update_entries_locked(store, files, folders, affected_sort_orders, marked, false);
}

FsearchDatabaseChunkedArray *
fsearch_database_index_store_peek_path_sorted_locked(FsearchDatabaseIndexStore *store, FsearchDatabaseEntryType type) {
    g_return_val_if_fail(store, NULL);
    return type == DATABASE_ENTRY_TYPE_FOLDER ? store->folder_chunks[DATABASE_INDEX_PROPERTY_PATH]
                                              : store->file_chunks[DATABASE_INDEX_PROPERTY_PATH];
}

void
fsearch_database_index_store_report_progress(FsearchDatabaseIndexStore *store, const char *path) {
    g_return_if_fail(store);
    if (store->event_func) {
        store->event_func(store, FSEARCH_DATABASE_INDEX_STORE_EVENT_PROGRESS, g_strdup(path), store->event_func_data);
    }
}

static void
index_store_worker_pool_func(gpointer pool_data, gpointer user_data) {
    FsearchDatabaseIndexStore *store = user_data;
    g_return_if_fail(store);

    IndexStoreWorkerPoolData *data = pool_data;
    g_return_if_fail(data);

    switch (data->type) {
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH:
        index_store_search_worker(data->search.ctx, data->search.thread_id);
        break;
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_STORE:
        if (data->update_store.add) {
            fsearch_database_chunked_array_insert_array(data->update_store.chunks, data->update_store.entries);
        }
        else {
            index_store_remove_from_store_worker(data->update_store.chunks,
                                                 data->update_store.entries,
                                                 data->update_store.marked);
        }
        break;
    case INDEX_STORE_WORKER_POOL_DATA_TYPE_UPDATE_RESULTS:
        if (data->update_results.add) {
            fsearch_database_search_view_add(data->update_results.view,
                                             data->update_results.files,
                                             data->update_results.folders,
                                             data->update_results.affected_sort_orders);
        }
        else {
            fsearch_database_search_view_remove(data->update_results.view,
                                                data->update_results.files,
                                                data->update_results.folders,
                                                data->update_results.affected_sort_orders,
                                                data->update_results.marked);
        }
        break;
    default:
        g_assert_not_reached();
    }
    g_async_queue_push(store->worker_pool_collect_queue, data);
}

typedef struct {
    FsearchDatabaseIndexStoreEventFunc event_func;
    gpointer event_func_data;
    FsearchDatabaseIndexStore *store;
} IndexStoreUpdateViewsContext;

static void
index_store_view_changed_cb(gpointer key, gpointer value, gpointer user_data) {
    IndexStoreUpdateViewsContext *ctx = user_data;
    FsearchDatabaseSearchView *view = value;

    ctx->event_func(ctx->store,
                    FSEARCH_DATABASE_INDEX_STORE_EVENT_VIEW_CHANGED,
                    fsearch_database_search_view_get_info(view),
                    ctx->event_func_data);
}

static void
index_store_update_all_search_views_locked(FsearchDatabaseIndexStore *store) {
    // store->mutex must already be held by the caller
    g_return_if_fail(store);
    g_return_if_fail(store->search_results);

    if (!store->event_func) {
        return;
    }

    IndexStoreUpdateViewsContext ctx = {
        .event_func = store->event_func,
        .event_func_data = store->event_func_data,
        .store = store,
    };
    g_hash_table_foreach(store->search_results, index_store_view_changed_cb, &ctx);
}

static void
index_store_content_changed(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);
    index_store_update_all_search_views_locked(store);
    if (store->event_func) {
        store->event_func(store, FSEARCH_DATABASE_INDEX_STORE_EVENT_CONTENT_CHANGED, NULL, store->event_func_data);
    }
}

static gboolean
index_store_proces_events_cb(gpointer data) {
    FsearchDatabaseIndexStore *store = data;
    g_return_val_if_fail(store, G_SOURCE_REMOVE);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);

    if (!store->running) {
        // store isn't fully started yet
        return G_SOURCE_CONTINUE;
    }

    gboolean has_pending = FALSE;
    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        g_return_val_if_fail(index, G_SOURCE_REMOVE);
        has_pending |= fsearch_database_index_has_pending_events(index);
    }

    // Notify the UI for a potential slow index update
    if (has_pending && store->event_func) {
        store->event_func(store, FSEARCH_DATABASE_INDEX_STORE_EVENT_APPLY_STARTED, NULL, store->event_func_data);
    }

    gboolean store_was_updated = FALSE;
    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        g_return_val_if_fail(index, G_SOURCE_REMOVE);
        store_was_updated |= fsearch_database_index_process_events(index);
    }

    if (store_was_updated) {
        index_store_content_changed(store);
    }

    if (has_pending && store->event_func) {
        store->event_func(store, FSEARCH_DATABASE_INDEX_STORE_EVENT_APPLY_FINISHED, NULL, store->event_func_data);
    }

    return G_SOURCE_CONTINUE;
}

static gboolean
index_store_root_reappear_poll_cb(gpointer user_data) {
    FsearchDatabaseIndexStore *store = user_data;

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);

    if (!store->running) {
        return G_SOURCE_CONTINUE;
    }

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        if (!index) {
            continue;
        }

        if (!fsearch_database_index_wants_root_reappear_poll(index)) {
            continue;
        }

        const char *index_path = fsearch_database_index_get_path(index);
        if (!g_file_test(index_path, G_FILE_TEST_IS_DIR)) {
            continue;
        }
        g_debug("[index-%d] root folder reappeared, rescanning: %s", i, index_path);
        if (fsearch_database_index_scan(index, NULL)) {
            g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
            g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);

            fsearch_database_index_start_monitoring(index, true);

            fsearch_database_index_store_add_entries_locked(store, files, folders, DATABASE_INDEX_PROPERTY_FLAG_ALL);
            fsearch_database_index_attach(index, store);
            index_store_content_changed(store);
        }
    }

    return G_SOURCE_CONTINUE;
}

static void
index_store_free(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    if (store->worker_index_root_reappear_poll_source) {
        g_source_destroy(store->worker_index_root_reappear_poll_source);
        g_clear_pointer(&store->worker_index_root_reappear_poll_source, g_source_unref);
    }

    if (store->worker.event_source) {
        g_source_destroy(store->worker.event_source);
        g_clear_pointer(&store->worker.event_source, g_source_unref);
    }

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

    // 1. Make sure the indices are down, then free the entries they handed over
    for (uint32_t i = 0; store->indices && i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        fsearch_database_index_detach(index);
    }
    g_clear_pointer(&store->search_results, g_hash_table_unref);
    index_store_all_entries_free(store);
    g_clear_pointer(&store->indices, g_ptr_array_unref);
    index_store_sorted_entries_free(store);
    g_clear_object(&store->include_manager);
    g_clear_object(&store->exclude_manager);

    // Wait for tasks to finish must be TRUE since the worker threads might be using the worker_pool_collect_queue
    // Hence, make sure to unref the queue only after the pool has been terminated
    g_thread_pool_free(g_steal_pointer(&store->worker_pool), FALSE, TRUE);
    g_clear_pointer(&store->worker_pool_collect_queue, g_async_queue_unref);

    // Only stop the monitor and worker threads after the indices have been freed, since they rely on them when freeing
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

    g_mutex_clear(&store->mutex);

    g_free(store);
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIncludeManager *include_manager,
                                 FsearchDatabaseExcludeManager *exclude_manager,
                                 FsearchDatabaseIndexPropertyFlags flags,
                                 FsearchDatabaseIndexStoreEventFunc event_func,
                                 gpointer event_func_data) {
    FsearchDatabaseIndexStore *store = g_new0(FsearchDatabaseIndexStore, 1);

    // Must be initialized before any thread/source below can lock it.
    g_mutex_init(&store->mutex);
    store->ref_count = 1;

    store->indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    store->search_results = g_hash_table_new_full(g_direct_hash,
                                                  g_direct_equal,
                                                  NULL,
                                                  (GDestroyNotify)fsearch_database_search_view_free);

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
    store->worker.event_source = g_timeout_source_new_seconds(1);
    g_source_set_priority(store->worker.event_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback(store->worker.event_source, (GSourceFunc)index_store_proces_events_cb, store, NULL);
    g_source_attach(store->worker.event_source, store->worker.ctx);

    store->worker_index_root_reappear_poll_source = g_timeout_source_new_seconds(5);
    g_source_set_priority(store->worker_index_root_reappear_poll_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback(store->worker_index_root_reappear_poll_source, index_store_root_reappear_poll_cb, store, NULL);
    g_source_attach(store->worker_index_root_reappear_poll_source, store->worker.ctx);

    return store;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new_with_content(DynamicArray **files,
                                              DynamicArray **folders,
                                              FsearchDatabaseIncludeManager *include_manager,
                                              FsearchDatabaseExcludeManager *exclude_manager,
                                              FsearchDatabaseIndexPropertyFlags flags,
                                              FsearchDatabaseIndexStoreEventFunc event_func,
                                              gpointer event_func_data) {
    FsearchDatabaseIndexStore *store = fsearch_database_index_store_new(include_manager,
                                                                        exclude_manager,
                                                                        flags,
                                                                        event_func,
                                                                        event_func_data);

    // One index per include, active or not, since the loaded entries cover all of them
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    g_autoptr(GPtrArray) sorted_includes = g_ptr_array_copy(includes, (GCopyFunc)fsearch_database_include_ref, NULL);
    g_ptr_array_sort(sorted_includes, fsearch_database_include_compare);
    for (uint32_t i = 0; i < sorted_includes->len; ++i) {
        g_ptr_array_add(store->indices,
                        fsearch_database_index_new(g_ptr_array_index(sorted_includes, i),
                                                   exclude_manager,
                                                   flags,
                                                   store->monitor.ctx,
                                                   store));
    }

    for (uint32_t i = DATABASE_INDEX_PROPERTY_NAME; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        DynamicArray *s_files = files[i];
        DynamicArray *s_folders = folders[i];
        if (s_folders && s_files) {
            store->folder_chunks[i] = fsearch_database_chunked_array_new(s_folders,
                                                                         TRUE,
                                                                         fsearch_database_sort_order_chain_for_property(i),
                                                                         DATABASE_ENTRY_TYPE_FOLDER,
                                                                         NULL,
                                                                         NULL);
            store->file_chunks[i] = fsearch_database_chunked_array_new(s_files,
                                                                       TRUE,
                                                                       fsearch_database_sort_order_chain_for_property(i),
                                                                       DATABASE_ENTRY_TYPE_FILE,
                                                                       NULL,
                                                                       NULL);
        }
    }

    if (!store->folder_chunks[DATABASE_INDEX_PROPERTY_PATH] || !store->file_chunks[DATABASE_INDEX_PROPERTY_PATH]) {
        // Indices rely on the path order for lookups
        g_debug("[index_store] no path sorted entries, sorting them now");
        g_autoptr(DynamicArray) s_folders = NULL;
        g_autoptr(DynamicArray) s_files = NULL;
        for (uint32_t i = DATABASE_INDEX_PROPERTY_NAME; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
            if (folders[i] && files[i]) {
                s_folders = darray_copy(folders[i]);
                s_files = darray_copy(files[i]);
                break;
            }
        }
        const FsearchDatabaseSortOrderChain chain = fsearch_database_sort_order_chain_for_property(
            DATABASE_INDEX_PROPERTY_PATH);
        g_clear_pointer(&store->folder_chunks[DATABASE_INDEX_PROPERTY_PATH], fsearch_database_chunked_array_unref);
        g_clear_pointer(&store->file_chunks[DATABASE_INDEX_PROPERTY_PATH], fsearch_database_chunked_array_unref);
        store->folder_chunks[DATABASE_INDEX_PROPERTY_PATH] = fsearch_database_chunked_array_new(
            s_folders ? s_folders : (s_folders = darray_new(0)),
            FALSE,
            chain,
            DATABASE_ENTRY_TYPE_FOLDER,
            NULL,
            NULL);
        store->file_chunks[DATABASE_INDEX_PROPERTY_PATH] = fsearch_database_chunked_array_new(
            s_files ? s_files : (s_files = darray_new(0)),
            FALSE,
            chain,
            DATABASE_ENTRY_TYPE_FILE,
            NULL,
            NULL);
    }

    store->is_sorted = true;
    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        fsearch_database_index_attach(index, store);
    }
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);
    store->running = true;

    return store;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&store->ref_count) > 0, NULL);

    g_atomic_int_inc(&store->ref_count);

    return store;
}

void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store != NULL);
    g_return_if_fail(g_atomic_int_get(&store->ref_count) > 0);

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

    // Initialize the working array so that in case the scan was canceled, the indices are destroyed
    g_autoptr(GPtrArray) indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(store->include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        if (!fsearch_database_include_get_active(include)) {
            continue;
        }
        g_autoptr(FsearchDatabaseIndex) index = fsearch_database_index_new(include,
                                                                           store->exclude_manager,
                                                                           store->flags,
                                                                           store->monitor.ctx,
                                                                           store);
        fsearch_database_index_scan(index, cancellable);
        g_ptr_array_add(indices, g_steal_pointer(&index));
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    g_autoptr(DynamicArray) store_files = darray_new(1024);
    g_autoptr(DynamicArray) store_folders = darray_new(1024);
    while (indices->len > 0) {
        // Steal index so free func isn't called and we get ownership of index.
        FsearchDatabaseIndex *index = g_ptr_array_steal_index(indices, 0);
        if (!index) {
            // In practice there shouldn't be a NULL element in between valid indices. Still, if it happens, we can
            // jump to the next iteration
            continue;
        }

        if (index_store_has_index_with_same_path(store, index, NULL)
            || !index_store_flags_equal(store, fsearch_database_index_get_flags(index))) {
            // We don't need that index: free it
            g_clear_pointer(&index, fsearch_database_index_unref);
            continue;
        }
        g_ptr_array_add(store->indices, index);
        g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);
        g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
        if (files) {
            darray_add_array(store_files, files);
        }
        if (folders) {
            darray_add_array(store_folders, folders);
        }

        store->is_sorted = false;
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    // Notify the database that sorting started
    if (store->event_func) {
        store->event_func(store,
                          FSEARCH_DATABASE_INDEX_STORE_EVENT_PROGRESS,
                          g_strdup(_("Sorting…")),
                          store->event_func_data);
    }

    static const FsearchDatabaseIndexProperty sort_orders[] = {
        DATABASE_INDEX_PROPERTY_NAME,
        DATABASE_INDEX_PROPERTY_PATH,
        DATABASE_INDEX_PROPERTY_SIZE,
        DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
        DATABASE_INDEX_PROPERTY_EXTENSION,
    };
    for (uint32_t i = 0; i < G_N_ELEMENTS(sort_orders); ++i) {
        const FsearchDatabaseIndexProperty property = sort_orders[i];
        const FsearchDatabaseSortOrderChain chain = fsearch_database_sort_order_chain_for_property(property);
        store->folder_chunks[property] = fsearch_database_chunked_array_new(store_folders,
                                                                            FALSE,
                                                                            chain,
                                                                            DATABASE_ENTRY_TYPE_FOLDER,
                                                                            cancellable,
                                                                            NULL);
        store->file_chunks[property] = fsearch_database_chunked_array_new(store_files,
                                                                          FALSE,
                                                                          chain,
                                                                          DATABASE_ENTRY_TYPE_FILE,
                                                                          cancellable,
                                                                          NULL);
    }
    store->is_sorted = true;

    if (g_cancellable_is_cancelled(cancellable)) {
        // Not attached yet, so the indices still own and free their entries
        index_store_sorted_entries_free(store);
        g_ptr_array_remove_range(store->indices, 0, store->indices->len);
        return;
    }

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        fsearch_database_index_attach(g_ptr_array_index(store->indices, i), store);
    }

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);
    store->running = true;

    return;
}

bool
fsearch_database_index_store_is_running(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, false);
    return store->running;
}

void
fsearch_database_index_store_start_monitoring(FsearchDatabaseIndexStore *store) {
    g_return_if_fail(store);

    for (uint32_t i = 0; i < store->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, i);
        fsearch_database_index_start_monitoring(index, true);
    }
}

FsearchDatabaseIndex *
fsearch_database_index_store_create_index_for_rescan(FsearchDatabaseIndexStore *store, const char *path) {
    g_return_val_if_fail(store, NULL);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);

    FsearchDatabaseIndex *old_index = NULL;
    for (uint32_t i = 0; i < store->indices->len; i++) {
        FsearchDatabaseIndex *idx = g_ptr_array_index(store->indices, i);
        const char *index_path = fsearch_database_index_get_path(idx);
        if (g_strcmp0(index_path, path) == 0) {
            old_index = idx;
            break;
        }
    }

    if (!old_index) {
        g_warning("[index-store] create_index_for_rescan: index missing: %s", path);
        return NULL;
    }

    g_autoptr(FsearchDatabaseInclude) include = fsearch_database_index_get_include(old_index);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_index_get_exclude_manager(old_index);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_index_get_flags(old_index);

    return fsearch_database_index_new(include, exclude_manager, flags, store->monitor.ctx, store);
}

bool
fsearch_database_index_store_replace_index(FsearchDatabaseIndexStore *store, FsearchDatabaseIndex *new_index) {
    g_return_val_if_fail(store, false);
    g_return_val_if_fail(new_index, false);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&store->mutex);
    g_assert_nonnull(locker);

    // Find the old index.
    uint32_t old_idx_pos = 0;
    if (!index_store_has_index_with_same_path(store, new_index, &old_idx_pos)) {
        g_warning("[index-store] replace_index: no index with path %s", fsearch_database_index_get_path(new_index));
        return false;
    }

    g_autoptr(FsearchDatabaseIndex) old_index = g_ptr_array_steal_index(store->indices, old_idx_pos);

    // 1. Stop monitoring on the old index so no new filesystem events are queued.
    fsearch_database_index_start_monitoring(old_index, false);

    // 2. Remove the old index, marked so the store can drop its entries in bulk
    g_autoptr(DynamicArray) old_files = fsearch_database_index_get_files(old_index);
    g_autoptr(DynamicArray) old_folders = fsearch_database_index_get_folders(old_index);
    for (uint32_t i = 0; i < darray_get_num_items(old_folders); ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(old_folders, i);
        db_entry_set_mark(entry, 1);
    }
    fsearch_database_index_store_remove_entries_locked(store, old_files, old_folders, DATABASE_INDEX_PROPERTY_FLAG_ALL, true);
    fsearch_database_index_detach(old_index);
    index_store_free_entries(old_files, old_folders);

    // 3. Add the new index
    g_ptr_array_add(store->indices, fsearch_database_index_ref(new_index));

    // TODO> Faster index insert
    // Assuming the new entries are sorted (otherwise sort them)
    // Similary to above it might be more efficient to not insert entries one by one
    // Instead we expand the destination by the num of entries to be added
    // and then join them from the end
    g_autoptr(DynamicArray) new_files = fsearch_database_index_get_files(new_index);
    g_autoptr(DynamicArray) new_folders = fsearch_database_index_get_folders(new_index);
    fsearch_database_index_store_add_entries_locked(store, new_files, new_folders, DATABASE_INDEX_PROPERTY_FLAG_ALL);
    fsearch_database_index_attach(new_index, store);

    // 4. Enable monitoring on the new index.
    fsearch_database_index_start_monitoring(new_index, true);

    // 5. Notify any open search views that their results may have changed.
    index_store_content_changed(store);

    return true;
}

void
fsearch_database_index_store_remove_paths_locked(FsearchDatabaseIndexStore *store,
                                                 DynamicArray *file_paths,
                                                 FsearchDatabaseRescanManager *rescan_manager) {
    g_return_if_fail(store);
    g_return_if_fail(file_paths);

    bool content_changed = false;
    const uint32_t num_file_paths = darray_get_num_items(file_paths);
    for (uint32_t i = 0; i < num_file_paths; ++i) {
        const char *path = darray_get_item(file_paths, i);

        for (uint32_t j = 0; j < store->indices->len; ++j) {
            FsearchDatabaseIndex *index = g_ptr_array_index(store->indices, j);
            g_autoptr(FsearchDatabaseInclude) include = fsearch_database_index_get_include(index);
            const char *root_path = fsearch_database_include_get_path(include);

            if (fsearch_database_include_get_monitored(include)) {
                continue;
            }

            // Optimization: Only try to remove if the path falls under this index's root
            if (g_str_has_prefix(path, root_path)) {
                bool root_removed = false;
                if (fsearch_database_index_remove_path_locked(index, path, &root_removed)) {
                    content_changed = true;
                }

                // Handle the offline edge case
                if (root_removed && rescan_manager) {
                    fsearch_database_rescan_manager_notify_index_offline(rescan_manager,
                                                                         fsearch_database_index_get_path(index));
                }
            }
        }
    }
    if (content_changed) {
        index_store_content_changed(store);
    }
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
FsearchDatabaseChunkedArray *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *store, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(store, NULL);

    if (!store->is_sorted) {
        return NULL;
    }

    return store->file_chunks[sort_order] ? fsearch_database_chunked_array_ref(store->file_chunks[sort_order]) : NULL;
}

FsearchDatabaseChunkedArray *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *store, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(store, NULL);
    if (!store->is_sorted) {
        return NULL;
    }

    return store->folder_chunks[sort_order] ? fsearch_database_chunked_array_ref(store->folder_chunks[sort_order]) : NULL;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, DATABASE_INDEX_PROPERTY_FLAG_NONE);
    return store->flags;
}

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    return store->file_chunks[DATABASE_INDEX_PROPERTY_NAME]
             ? fsearch_database_chunked_array_get_num_entries(store->file_chunks[DATABASE_INDEX_PROPERTY_NAME])
             : 0;
}

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    return store->folder_chunks[DATABASE_INDEX_PROPERTY_NAME]
             ? fsearch_database_chunked_array_get_num_entries(store->folder_chunks[DATABASE_INDEX_PROPERTY_NAME])
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

    return fsearch_database_entry_info_new(entry, query, idx, fsearch_database_search_view_is_selected(view, entry), flags);
}

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *store) {
    g_return_val_if_fail(store, 0);

    uint32_t num_fast_sort_indices = 0;
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (store->folder_chunks[i] && store->file_chunks[i]) {
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

void
fsearch_database_index_store_sort_results(FsearchDatabaseIndexStore *store,
                                          uint32_t id,
                                          FsearchDatabaseIndexProperty sort_order,
                                          FsearchSortDirection sort_direction,
                                          GCancellable *cancellable) {
    g_return_if_fail(store);
    g_return_if_fail(store->search_results);

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, id);
    if (!view) {
        return;
    }

    g_autoptr(FsearchDatabaseChunkedArray) files_fast_sort_index = fsearch_database_index_store_get_files(store,
                                                                                                          sort_order);
    g_autoptr(FsearchDatabaseChunkedArray) folders_fast_sort_index = fsearch_database_index_store_get_folders(store,
                                                                                                              sort_order);

    g_autoptr(DynamicArray) files_fast_sorted = NULL;
    g_autoptr(DynamicArray) folders_fast_sorted = NULL;
    if (files_fast_sort_index && folders_fast_sort_index) {
        files_fast_sorted = fsearch_database_chunked_array_get_joined(files_fast_sort_index);
        folders_fast_sorted = fsearch_database_chunked_array_get_joined(folders_fast_sort_index);
    }
    fsearch_database_search_view_sort(view, files_fast_sorted, folders_fast_sorted, sort_order, sort_direction, cancellable);
}

static FsearchDatabaseChunkedArray *
search_entries(FsearchQuery *query,
               FsearchDatabaseChunkedArray *in,
               FsearchDatabaseSortOrderChain chain,
               FsearchDatabaseEntryType entry_type,
               GThreadPool *pool,
               GAsyncQueue *collect_queue,
               GCancellable *cancellable) {
    g_autoptr(DynamicArray) chunks = fsearch_database_chunked_array_get_chunks(in);
    const uint32_t num_chunks = darray_get_num_items(chunks);

    if (fsearch_query_matches_everything(query)) {
        return fsearch_database_chunked_array_new_from_arrays(chunks, chain, entry_type);
    }

    const uint32_t num_entries = fsearch_database_chunked_array_get_num_entries(in);
    const uint32_t num_threads = (num_entries < THRESHOLD_FOR_PARALLEL_SEARCH || query->wants_single_threaded_search)
                                   ? 1
                                   : g_thread_pool_get_num_threads(pool);
    const uint32_t num_workers = MAX(1, MIN(num_threads, num_chunks));

    g_autofree DynamicArray **results = g_new0(DynamicArray *, MAX(1, num_chunks));
    IndexStoreSearchContext ctx = {
        .query = query,
        .cancellable = cancellable,
        .chunks = chunks,
        .results = results,
        .next_chunk_idx = 0,
    };

    for (uint32_t i = 0; i < num_workers; ++i) {
        IndexStoreWorkerPoolData *pool_data = g_new0(IndexStoreWorkerPoolData, 1);
        pool_data->type = INDEX_STORE_WORKER_POOL_DATA_TYPE_SEARCH;
        pool_data->search.ctx = &ctx;
        pool_data->search.thread_id = (int32_t)i;
        g_thread_pool_push(pool, pool_data, NULL);
    }
    for (uint32_t i = 0; i < num_workers; ++i) {
        g_free(g_async_queue_pop(collect_queue));
    }

    // Chunks skipped because of cancellation have no results
    g_autoptr(DynamicArray) result_arrays = darray_new_full(num_chunks, (GDestroyNotify)darray_unref);
    for (uint32_t i = 0; i < num_chunks; ++i) {
        if (results[i]) {
            darray_add_item(result_arrays, results[i]);
        }
    }
    return fsearch_database_chunked_array_new_from_arrays(result_arrays, chain, entry_type);
}

bool
fsearch_database_index_store_search(FsearchDatabaseIndexStore *store,
                                    uint32_t id,
                                    FsearchQuery *query,
                                    FsearchDatabaseIndexProperty sort_order,
                                    FsearchSortDirection sort_direction,
                                    GCancellable *cancellable) {
    g_return_val_if_fail(store, false);
    g_return_val_if_fail(store->search_results, false);

    g_autoptr(GTimer) timer = g_timer_new();

    g_autoptr(FsearchDatabaseChunkedArray) file_chunks = fsearch_database_index_store_get_files(store, sort_order);
    g_autoptr(FsearchDatabaseChunkedArray) folder_chunks = fsearch_database_index_store_get_folders(store, sort_order);

    if (!file_chunks && !folder_chunks) {
        g_debug("[index_store] no fast sort index for sort order %s, falling back to name",
                fsearch_database_index_property_to_string(sort_order));
        sort_order = DATABASE_INDEX_PROPERTY_NAME;
        file_chunks = fsearch_database_index_store_get_files(store, sort_order);
        folder_chunks = fsearch_database_index_store_get_folders(store, sort_order);
    }

    if (!file_chunks && !folder_chunks) {
        g_debug("[index_store] search skipped: store has no entries to search");
        return false;
    }

    const uint32_t num_searched = (file_chunks ? fsearch_database_chunked_array_get_num_entries(file_chunks) : 0)
                                + (folder_chunks ? fsearch_database_chunked_array_get_num_entries(folder_chunks) : 0);

    const FsearchDatabaseSortOrderChain chain = fsearch_database_sort_order_chain_for_property(sort_order);

    const bool matches_everything = fsearch_query_matches_everything(query);
    g_autoptr(FsearchDatabaseChunkedArray) found_files = NULL;
    if (file_chunks) {
        found_files = search_entries(query,
                                     file_chunks,
                                     chain,
                                     DATABASE_ENTRY_TYPE_FILE,
                                     store->worker_pool,
                                     store->worker_pool_collect_queue,
                                     cancellable);
    }
    g_autoptr(FsearchDatabaseChunkedArray) found_folders = NULL;
    if (folder_chunks) {
        found_folders = search_entries(query,
                                       folder_chunks,
                                       chain,
                                       DATABASE_ENTRY_TYPE_FOLDER,
                                       store->worker_pool,
                                       store->worker_pool_collect_queue,
                                       cancellable);
    }

    const uint32_t num_found_files = found_files ? fsearch_database_chunked_array_get_num_entries(found_files) : 0;
    const uint32_t num_found_folders = found_folders ? fsearch_database_chunked_array_get_num_entries(found_folders) : 0;
    const double search_time = g_timer_elapsed(timer, NULL);

    g_debug("[index_store] search \"%s\": %u of %u matched (%u folder%s, %u file%s) in %.3f ms%s%s",
            query->search_term ? query->search_term : "",
            num_found_folders + num_found_files,
            num_searched,
            num_found_folders,
            num_found_folders == 1 ? "" : "s",
            num_found_files,
            num_found_files == 1 ? "" : "s",
            search_time * 1000.0,
            matches_everything ? ", match-all" : "",
            g_cancellable_is_cancelled(cancellable) ? ", cancelled" : "");

    if (found_files || found_folders) {
        // If the search got cancelled partway through, found_files/found_folders only reflect
        // whatever was matched before that happened. We still install them (rather than
        // discarding the work), but mark the view as incomplete so callers can tell a partial
        // result set apart from a genuinely finished search.
        const bool is_complete = !g_cancellable_is_cancelled(cancellable);

        FsearchDatabaseSearchView *view = fsearch_database_search_view_new(id,
                                                                           query,
                                                                           g_steal_pointer(&found_files),
                                                                           g_steal_pointer(&found_folders),
                                                                           NULL,
                                                                           chain,
                                                                           sort_direction,
                                                                           is_complete);
        g_hash_table_insert(store->search_results, GUINT_TO_POINTER(id), view);

        return is_complete;
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
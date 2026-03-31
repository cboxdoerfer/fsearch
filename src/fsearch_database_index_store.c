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
#include "fsearch_database_search.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_query.h"
#include "fsearch_thread_pool.h"

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
    g_print("new content...\n");
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
                NULL);
            store->file_container[i] = fsearch_database_entries_container_new(
                s_files,
                TRUE,
                i,
                DATABASE_INDEX_PROPERTY_NONE,
                DATABASE_ENTRY_TYPE_FILE,
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

    //signal_emit(db, SIGNAL_DATABASE_PROGRESS, g_strdup(_("Sorting…")), NULL, 1, (GDestroyNotify)free, NULL);

    index_store_lock_all_indices(store);
    store->folder_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    store->file_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    store->folder_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    store->file_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    store->folder_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    store->file_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    store->folder_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    store->file_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    store->folder_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    store->file_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
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
    DynamicArray *folders;
    DynamicArray *files;
} FsearchDatabaseIndexStoreAddRemoveContext;

static void
search_view_results_add_cb(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseIndexStoreAddRemoveContext *ctx = user_data;
    g_return_if_fail(ctx);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    fsearch_database_search_view_add(view, ctx->files, ctx->folders);
}

void
fsearch_database_index_store_add_entries(FsearchDatabaseIndexStore *store, DynamicArray *files, DynamicArray *folders) {
    g_return_if_fail(store);

    /* Mutators (called by filesystem monitors) */
    FsearchDatabaseIndexStoreAddRemoveContext ctx = {
        .folders = folders,
        .files = files,
    };

    g_hash_table_foreach(store->search_results, search_view_results_add_cb, &ctx);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (files && store->file_container[i]) {
            fsearch_database_entries_container_insert_array(store->file_container[i], files);
        }
        if (folders && store->folder_container[i]) {
            fsearch_database_entries_container_insert_array(store->folder_container[i], folders);
        }
    }
}

static void
remove_entries(FsearchDatabaseEntriesContainer **containers, DynamicArray *entries) {
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *container = containers[i];
        if (!container) {
            continue;
        }
        for (uint32_t j = 0; j < darray_get_num_items(entries); ++j) {
            FsearchDatabaseEntry *entry = darray_get_item(entries, j);
            if (!fsearch_database_entries_container_steal(container, entry)) {
                g_debug("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
            }
        }
    }
}

static void
search_view_results_remove_cb(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseIndexStoreAddRemoveContext *ctx = user_data;
    g_return_if_fail(ctx);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    fsearch_database_search_view_remove(view, ctx->files, ctx->folders);
}

void
fsearch_database_index_store_remove_entries(FsearchDatabaseIndexStore *store,
                                            DynamicArray *files,
                                            DynamicArray *folders) {
    g_return_if_fail(store);

    /* Mutators (called by filesystem monitors) */
    FsearchDatabaseIndexStoreAddRemoveContext ctx = {
        .folders = folders,
        .files = files,
    };

    g_hash_table_foreach(store->search_results, search_view_results_remove_cb, &ctx);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (files) {
            remove_entries(store->file_container, files);
        }
        if (folders) {
            remove_entries(store->folder_container, folders);
        }
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

bool
fsearch_database_index_store_search(FsearchDatabaseIndexStore *store,
                                    uint32_t id,
                                    FsearchQuery *query,
                                    FsearchDatabaseIndexProperty sort_order,
                                    GtkSortType sort_type,
                                    FsearchThreadPool *thread_pool,
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

    DatabaseSearchResult *search_result = fsearch_query_matches_everything(query)
                                              ? db_search_empty(folders, files)
                                              : db_search(query, thread_pool, folders, files, cancellable);
    if (search_result) {
        // After searching the secondary sort order will always be NONE, because we only search in pre-sorted indexes
        FsearchDatabaseSearchView *view = fsearch_database_search_view_new(id,
                                                                           query,
                                                                           search_result->files,
                                                                           search_result->folders,
                                                                           NULL,
                                                                           sort_order,
                                                                           DATABASE_INDEX_PROPERTY_NONE,
                                                                           sort_type);
        g_hash_table_insert(store->search_results, GUINT_TO_POINTER(id), view);

        g_clear_pointer(&search_result->files, darray_unref);
        g_clear_pointer(&search_result->folders, darray_unref);
        g_clear_pointer(&search_result, free);

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


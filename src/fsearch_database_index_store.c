#define G_LOG_DOMAIN "fsearch-database-index-store"

#include "fsearch_database_index_store.h"

#include <glib.h>

#include "fsearch_database_sort.h"

struct _FsearchDatabaseIndexStore {
    GPtrArray *indices;

    FsearchDatabaseEntriesContainer *file_container[NUM_DATABASE_INDEX_PROPERTIES];
    FsearchDatabaseEntriesContainer *folder_container[NUM_DATABASE_INDEX_PROPERTIES];

    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    FsearchDatabaseIndexPropertyFlags flags;

    struct {
        GThread *thread;
        GMainLoop *loop;
        GMainContext *ctx;
    } monitor;

    struct {
        GThread *thread;
        GMainLoop *loop;
        GMainContext *ctx;
    } worker;

    bool is_sorted;
    bool running;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndexStore,
                    fsearch_database_index_store,
                    fsearch_database_index_store_ref,
                    fsearch_database_index_store_unref)

static void
sorted_entries_free(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (self->file_container[i]) {
            g_clear_pointer(&self->file_container[i], fsearch_database_entries_container_unref);
        }
        if (self->folder_container[i]) {
            g_clear_pointer(&self->folder_container[i], fsearch_database_entries_container_unref);
        }
    }
}

static bool
has_flag(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
    g_assert(self);
    g_assert(index);

    const FsearchDatabaseIndexPropertyFlags store_flags = self->flags;
    const FsearchDatabaseIndexPropertyFlags index_flags = fsearch_database_index_get_flags(index);

    return (store_flags & index_flags) == store_flags;
}

static bool
index_store_has_index_with_same_id(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
    g_assert(self);
    g_assert(index);

    for (uint32_t i = 0; i < self->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(self->indices, i);
        if (fsearch_database_index_get_id(index_stored) == fsearch_database_index_get_id(index)) {
            return true;
        }
    }
    return false;
}

static void
lock_all_indices(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);

    for (uint32_t i = 0; i < self->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(self->indices, i);
        fsearch_database_index_lock(index_stored);
    }
}

static void
unlock_all_indices(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);

    for (uint32_t i = 0; i < self->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(self->indices, i);
        fsearch_database_index_unlock(index_stored);
    }
}

static gboolean
thread_quit(GMainLoop *loop) {
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
worker_thread_func(gpointer user_data) {
    FsearchDatabaseIndexStore *self = user_data;
    g_return_val_if_fail(self, NULL);

    thread_func(self->worker.ctx, self->worker.loop);

    return NULL;
}

static gpointer
monitor_thread_func(gpointer user_data) {
    FsearchDatabaseIndexStore *self = user_data;
    g_return_val_if_fail(self, NULL);

    thread_func(self->monitor.ctx, self->monitor.loop);

    return NULL;
}

static void
index_store_free(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);

    if (self->monitor.loop) {
        g_main_context_invoke_full(self->monitor.ctx, G_PRIORITY_HIGH, (GSourceFunc)thread_quit, self->monitor.loop, NULL);
    }
    if (self->monitor.thread) {
        g_thread_join(self->monitor.thread);
    }
    g_clear_pointer(&self->monitor.ctx, g_main_context_unref);

    if (self->worker.loop) {
        g_main_context_invoke_full(self->worker.ctx, G_PRIORITY_HIGH, (GSourceFunc)thread_quit, self->worker.loop, NULL);
    }
    if (self->worker.thread) {
        g_thread_join(self->worker.thread);
    }
    g_clear_pointer(&self->worker.ctx, g_main_context_unref);

    sorted_entries_free(self);
    g_clear_pointer(&self->indices, g_ptr_array_unref);
    g_clear_object(&self->include_manager);
    g_clear_object(&self->exclude_manager);

    g_slice_free(FsearchDatabaseIndexStore, self);
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIncludeManager *include_manager,
                                 FsearchDatabaseExcludeManager *exclude_manager,
                                 FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseIndexStore *self;
    self = g_slice_new0(FsearchDatabaseIndexStore);

    self->indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    self->flags = flags;
    self->is_sorted = false;
    self->running = false;

    self->include_manager = g_object_ref(include_manager);
    self->exclude_manager = g_object_ref(exclude_manager);

    self->monitor.ctx = g_main_context_new();
    self->monitor.loop = g_main_loop_new(self->monitor.ctx, FALSE);
    self->monitor.thread = g_thread_new("FsearchDatabaseIndexStoreMonitor", monitor_thread_func, self);

    self->worker.ctx = g_main_context_new();
    self->worker.loop = g_main_loop_new(self->worker.ctx, FALSE);
    self->worker.thread = g_thread_new("FsearchDatabaseIndexStoreWorker", worker_thread_func, self);

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self, index_store_free);
    }
}

// void
// fsearch_database_index_store_add(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
//     g_return_if_fail(self);
//     g_return_if_fail(index);
//     g_return_if_fail(!index_store_has_index_with_same_id(self, index));
//     g_return_if_fail(has_flag(self, index));
//
//     g_ptr_array_add(self->indices, fsearch_database_index_ref(index));
//
//     fsearch_database_index_lock(index);
//
//     fsearch_database_index_set_propagate_work(index, true);
//     g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);
//     g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
//     darray_add_array(self->files_sorted[DATABASE_INDEX_PROPERTY_NAME], files);
//     darray_add_array(self->folders_sorted[DATABASE_INDEX_PROPERTY_NAME], folders);
//
//     fsearch_database_index_unlock(index);
//
//     self->is_sorted = false;
// }

// void
// fsearch_database_index_store_add_sorted(FsearchDatabaseIndexStore *self,
//                                         FsearchDatabaseIndex *index,
//                                         GCancellable *cancellable) {
//     g_return_if_fail(self);
//     g_return_if_fail(index);
//     g_return_if_fail(!index_store_has_index_with_same_id(self, index));
//     g_return_if_fail(!has_flag(self, index));
//
//     //fsearch_database_index_store_add(self, index);
//
//     lock_all_indices(self);
//     self->is_sorted = fsearch_database_sort(self->files_sorted, self->folders_sorted, self->flags, cancellable);
//     unlock_all_indices(self);
// }
//
// void
// fsearch_database_index_store_sort(FsearchDatabaseIndexStore *self, GCancellable *cancellable) {
//     g_return_if_fail(self);
//     if (self->is_sorted) {
//         return;
//     }
//     lock_all_indices(self);
//     self->is_sorted = fsearch_database_sort(self->files_sorted, self->folders_sorted, self->flags, cancellable);
//     unlock_all_indices(self);
// }

bool
fsearch_database_index_store_has_container(FsearchDatabaseIndexStore *self, FsearchDatabaseEntriesContainer *container) {
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(container, false);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *c = self->folder_container[i];
        if (c == container) {
            return true;
        }
        c = self->file_container[i];
        if (c == container) {
            return true;
        }
    }
    return false;
}

FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(self->is_sorted, NULL);

    return self->file_container[sort_order] ? fsearch_database_entries_container_ref(self->file_container[sort_order])
                                            : NULL;
}

FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(self->is_sorted, NULL);

    return self->folder_container[sort_order]
             ? fsearch_database_entries_container_ref(self->folder_container[sort_order])
             : NULL;
}

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    uint32_t num_fast_sort_indices = 0;
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (self->folder_container[i] && self->file_container[i]) {
            num_fast_sort_indices++;
        }
    }

    return num_fast_sort_indices;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return self->flags;
}

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return self->file_container[DATABASE_INDEX_PROPERTY_NAME]
             ? fsearch_database_entries_container_get_num_entries(self->file_container[DATABASE_INDEX_PROPERTY_NAME])
             : 0;
}

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return self->folder_container[DATABASE_INDEX_PROPERTY_NAME]
             ? fsearch_database_entries_container_get_num_entries(self->folder_container[DATABASE_INDEX_PROPERTY_NAME])
             : 0;
}

void
fsearch_database_index_store_remove_entry(FsearchDatabaseIndexStore *self,
                                          FsearchDatabaseEntry *entry,
                                          FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(entry);
    g_return_if_fail(index);

    if (!g_ptr_array_find(self->indices, index, NULL)) {
        g_debug("[index_store_remove] index does not belong to index store; must be a bug");
        g_assert_not_reached();
    }

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *container = NULL;
        if (db_entry_is_folder(entry)) {
            container = self->folder_container[i];
        }
        else {
            container = self->file_container[i];
        }

        if (!container) {
            continue;
        }

        if (!fsearch_database_entries_container_steal(container, entry)) {
            g_debug("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
        }
    }
}

void
fsearch_database_index_store_remove_folders(FsearchDatabaseIndexStore *self,
                                            DynamicArray *folders,
                                            FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(folders);
    g_return_if_fail(index);

    if (!g_ptr_array_find(self->indices, index, NULL)) {
        g_debug("[index_store_remove] index does not belong to index store; must be a bug");
        g_assert_not_reached();
    }

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *container = self->folder_container[i];
        if (!container) {
            continue;
        }
        for (uint32_t j = 0; j < darray_get_num_items(folders); ++j) {
            FsearchDatabaseEntry *entry = darray_get_item(folders, j);
            if (!fsearch_database_entries_container_steal(container, entry)) {
                g_debug("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
            }
        }
    }
}

void
fsearch_database_index_store_remove_files(FsearchDatabaseIndexStore *self,
                                          DynamicArray *files,
                                          FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(files);
    g_return_if_fail(index);

    if (!g_ptr_array_find(self->indices, index, NULL)) {
        g_debug("[index_store_remove] index does not belong to index store; must be a bug");
        g_assert_not_reached();
    }

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *container = self->file_container[i];
        if (!container) {
            continue;
        }
        for (uint32_t j = 0; j < darray_get_num_items(files); ++j) {
            FsearchDatabaseEntry *entry = darray_get_item(files, j);
            if (!fsearch_database_entries_container_steal(container, entry)) {
                g_debug("store: failed to remove entry: %s", db_entry_get_name_raw_for_display(entry));
            }
        }
    }
}

void
fsearch_database_index_store_add_entry(FsearchDatabaseIndexStore *self,
                                       FsearchDatabaseEntry *entry,
                                       FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(entry);
    g_return_if_fail(index);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        FsearchDatabaseEntriesContainer *container = NULL;
        if (db_entry_is_folder(entry)) {
            container = self->folder_container[i];
        }
        else {
            container = self->file_container[i];
        }

        if (!container) {
            continue;
        }

        fsearch_database_entries_container_insert(container, entry);
    }
}

void
fsearch_database_index_store_start(FsearchDatabaseIndexStore *self,
                                   GCancellable *cancellable,
                                   FsearchDatabaseIndexEventFunc event_func,
                                   gpointer event_func_data) {
    g_return_if_fail(self);
    if (self->running) {
        return;
    }

    g_autoptr(GPtrArray) indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(self->include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        FsearchDatabaseIndex *index = fsearch_database_index_new(fsearch_database_include_get_id(include),
                                                                 include,
                                                                 self->exclude_manager,
                                                                 self->flags,
                                                                 self->worker.ctx,
                                                                 self->monitor.ctx,
                                                                 event_func,
                                                                 event_func_data);
        fsearch_database_index_scan(index, cancellable);
        if (index) {
            g_ptr_array_add(indices, index);
        }
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    g_autoptr(DynamicArray) store_files = darray_new(1024);
    g_autoptr(DynamicArray) store_folders = darray_new(1024);
    for (uint32_t i = 0; i < indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(indices, i);

        if (index_store_has_index_with_same_id(self, index) || !has_flag(self, index)) {
            continue;
        }
        g_ptr_array_add(self->indices, fsearch_database_index_ref(index));
        fsearch_database_index_lock(index);
        g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);
        g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
        darray_add_array(store_files, files);
        darray_add_array(store_folders, folders);

        fsearch_database_index_unlock(index);

        self->is_sorted = false;
    }

    lock_all_indices(self);
    self->folder_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    self->file_container[DATABASE_INDEX_PROPERTY_NAME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_NAME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    self->folder_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    self->file_container[DATABASE_INDEX_PROPERTY_PATH] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_PATH,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    self->folder_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    self->file_container[DATABASE_INDEX_PROPERTY_SIZE] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_SIZE,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    self->folder_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    self->file_container[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    self->folder_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_folders,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FOLDER,
                                               cancellable);
    self->file_container[DATABASE_INDEX_PROPERTY_EXTENSION] =
        fsearch_database_entries_container_new(store_files,
                                               FALSE,
                                               DATABASE_INDEX_PROPERTY_EXTENSION,
                                               DATABASE_INDEX_PROPERTY_NONE,
                                               DATABASE_ENTRY_TYPE_FILE,
                                               cancellable);
    self->is_sorted = true;
    unlock_all_indices(self);

    if (g_cancellable_is_cancelled(cancellable)) {
        sorted_entries_free(self);
        g_ptr_array_remove_range(self->indices, 0, self->indices->len);
        return;
    }

    self->running = true;

    return;
}

void
fsearch_database_index_store_start_monitoring(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);
    lock_all_indices(self);

    for (uint32_t i = 0; i < self->indices->len; ++i) {
        FsearchDatabaseIndex *index = g_ptr_array_index(self->indices, i);
        fsearch_database_index_start_monitoring(index, true);
    }

    unlock_all_indices(self);
}

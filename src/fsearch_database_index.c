#define G_LOG_DOMAIN "fsearch-database-index"

#include "fsearch_database_index.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_scan.h"
#include "fsearch_file_utils.h"
#include "fsearch_folder_monitor_event.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"
#include "fsearch_memory_pool.h"

#include <config.h>
#include <glib-unix.h>
#include <glib.h>

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

struct _FsearchDatabaseIndex {
    FsearchDatabaseInclude *include;
    FsearchDatabaseExcludeManager *exclude_manager;
    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;
    FsearchDatabaseEntriesContainer *folder_container;
    FsearchDatabaseEntriesContainer *file_container;

    FsearchDatabaseIndexPropertyFlags flags;

    GMainContext *monitor_ctx;
    FsearchFolderMonitorFanotify *fanotify_monitor;
    FsearchFolderMonitorInotify *inotify_monitor;

    GSource *event_source;
    GMainContext *worker_ctx;

    GAsyncQueue *event_queue;

    GMutex mutex;

    uint32_t id;

    gdouble max_process_time;

    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_func_data;

    volatile gint monitor;
    volatile gint initialized;

    volatile gint ref_count;
};

static uint32_t num_file_deletes = 0;
static uint32_t num_folder_deletes = 0;
static uint32_t num_file_creates = 0;
static uint32_t num_folder_creates = 0;
static uint32_t num_attrib_changes = 0;
static uint32_t num_descendant_counted = 0;

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex, fsearch_database_index, fsearch_database_index_ref, fsearch_database_index_unref)

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event);

static void
process_queued_events(FsearchDatabaseIndex *self);

static void
propagate_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexEventKind kind, DynamicArray *folders, DynamicArray *files) {
    if (!self->event_func) {
        return;
    }
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(kind, folders, files, NULL);
    self->event_func(self, event, self->event_func_data);
}

static void
process_queued_events(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    const int32_t num_events_queued = g_async_queue_length(self->event_queue);
    if (num_events_queued < 1) {
        return;
    }

    g_autoptr(GTimer) timer = g_timer_new();

    double last_time = 0.0;

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL);
    while (true) {
        FsearchFolderMonitorEvent *event = g_async_queue_try_pop(self->event_queue);
        if (!event) {
            break;
        }
        double elapsed = g_timer_elapsed(timer, NULL);
        if (elapsed - last_time > 0.2) {
            g_debug("interrupt event processing for a while...");
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL);
            last_time = elapsed;
            g_usleep(G_USEC_PER_SEC * 0.05);
            g_debug("continue event processing...");
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL);
        }
        process_event(self, event);
        g_clear_pointer(&event, fsearch_folder_monitor_event_free);
    }
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL);

    const double process_time = g_timer_elapsed(timer, NULL);
    self->max_process_time = MAX(process_time, self->max_process_time);
    g_debug("processed all events: %d (%d/%d %d/%d %d %d) in %fs (max: %fs)",
            num_events_queued,
            num_folder_creates,
            num_file_creates,
            num_folder_deletes,
            num_file_deletes,
            num_attrib_changes,
            num_descendant_counted,
            process_time,
            self->max_process_time);
    num_folder_deletes = 0;
    num_file_deletes = 0;
    num_folder_creates = 0;
    num_file_creates = 0;
    num_attrib_changes = 0;
    num_descendant_counted = 0;
}

static FsearchDatabaseEntry *
lookup_entry_for_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event, bool steal, bool expect_success) {
    g_return_val_if_fail(self, NULL);

    if (!event->watched_entry) {
        if (!expect_success) {
            g_debug("no entry for watch descriptor not in hash table!!!");
            return NULL;
        }
        else {
            g_assert_not_reached();
        }
    }

    // The dummy entry is used to mimic the entry we want to find.
    // It has the same name and parent (i.e. the watched directory)
    // and hence the same path. This means it will compare in the same way as the entry we're looking
    // for when it gets passed to the `db_entry_compare_entries_by_full_path` function.
    g_autofree FsearchDatabaseEntry *entry_tmp =
        db_entry_get_dummy_for_name_and_parent((FsearchDatabaseEntry *)event->watched_entry,
                                               event->name->str,
                                               event->is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);

    FsearchDatabaseEntriesContainer *container = event->is_dir ? self->folder_container : self->file_container;

    FsearchDatabaseEntry *entry = steal ? fsearch_database_entries_container_steal(container, entry_tmp)
                                        : fsearch_database_entries_container_find(container, entry_tmp);
    if (!entry && expect_success) {
        g_assert_not_reached();
    }
#if 0
    uint32_t idx = 0;
    if (darray_binary_search_with_data(array,
                                       entry_tmp,
                                       (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                                       NULL,
                                       &idx)) {
        if (index_out) {
            *index_out = idx;
        }
        entry = darray_get_item(array, idx);
    }
    else {
#if 0
        for (uint32_t i = 0; i < darray_get_num_items(array); ++i) {
            FsearchDatabaseEntry *e = darray_get_item(array, i);
            if (db_entry_compare_entries_by_path(&entry_tmp, &e) == 0) {
                g_assert_not_reached();
            }
        }
        g_debug("entry not found: %s", db_entry_get_name_raw_for_display(entry_tmp));
#endif
    }
#endif

    db_entry_destroy(entry_tmp);

    return entry;
}

static void
unwatch_folder(FsearchDatabaseIndex *self, FsearchDatabaseEntry *folder, FsearchFolderMonitorKind monitor_kind) {
    g_return_if_fail(self);
    g_return_if_fail(db_entry_is_folder(folder));
    g_return_if_fail(monitor_kind != FSEARCH_FOLDER_MONITOR_NONE);

    if (monitor_kind == FSEARCH_FOLDER_MONITOR_INOTIFY) {
#ifdef HAVE_INOTIFY
        fsearch_folder_monitor_inotify_unwatch(self->inotify_monitor, folder);
#endif
    }
    else if (monitor_kind == FSEARCH_FOLDER_MONITOR_FANOTIFY) {
#ifdef HAVE_FANOTIFY
        fsearch_folder_monitor_fanotify_unwatch(self->fanotify_monitor, folder);
#endif
    }
}

static void
process_create_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    off_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;

    FsearchDatabaseEntry *entry = NULL;
    g_autoptr(DynamicArray) folders = NULL;
    g_autoptr(DynamicArray) files = NULL;

    if (!fsearch_file_utils_get_info(event->path->str, &mtime, &size, &is_dir)) {
        return;
    }

    if (is_dir) {
        folders = darray_new(128);
        files = darray_new(128);
        if (db_scan_folder(event->path->str,
                           event->watched_entry,
                           self->folder_pool,
                           self->file_pool,
                           folders,
                           files,
                           self->exclude_manager,
                           self->fanotify_monitor,
                           self->inotify_monitor,
                           self->id,
                           fsearch_database_include_get_one_file_system(self->include),
                           NULL,
                           NULL,
                           NULL)) {
            for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
                FsearchDatabaseEntry *folder = darray_get_item(folders, i);
                fsearch_database_entries_container_insert(self->folder_container, folder);
                num_folder_creates++;
            }
            for (uint32_t i = 0; i < darray_get_num_items(files); ++i) {
                FsearchDatabaseEntry *file = darray_get_item(files, i);
                fsearch_database_entries_container_insert(self->file_container, file);
                num_file_creates++;
            }
        }
    }
    else {
        entry = fsearch_database_index_add_file(self, event->name->str, size, mtime, event->watched_entry);
        files = darray_new(1);
        darray_add_item(files, entry);
        num_file_creates++;
    }

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files);
}

static inline void
free_entry(FsearchMemoryPool *pool, FsearchDatabaseEntry *entry) {
    db_entry_set_parent(entry, NULL);
    fsearch_memory_pool_free(pool, entry, TRUE);
}

static void
process_delete_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    FsearchDatabaseEntry *entry = lookup_entry_for_event(self, event, true, false);
    if (!entry) {
        return;
    }

    // Deleting a file is simple:
    // 1. notify listeners about the deletion
    // 2. free the entry
    if (db_entry_is_file(entry)) {
        g_autoptr(DynamicArray) files = darray_new(1);
        darray_add_item(files, entry);
        propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, NULL, files);
        free_entry(self->file_pool, entry);
        return;
    }

    g_assert(db_entry_is_folder(entry));

    // Deleting a folder is more complex:
    // 1. Find and remove all its descendants from the index
    // 2. Notify listeners about the removal of all descendants and the folder
    // 3. Unparent, unwatch and free all entries

    g_autoptr(GTimer) timer = g_timer_new();
    FsearchDatabaseEntryFolder *folder_entry_to_remove = (FsearchDatabaseEntryFolder *)entry;

    g_autoptr(DynamicArray) folders =
        fsearch_database_entries_container_steal_descendants(self->folder_container, folder_entry_to_remove, -1);

    // It's worth to iterate over all folders to calculate the exact number of file descendants we must find,
    // because this means we can steal the files in huge chunks, which is much faster.
    uint32_t num_file_descendants = db_entry_folder_get_num_files(folder_entry_to_remove);
    for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
        FsearchDatabaseEntryFolder *folder_entry = darray_get_item(folders, i);
        num_file_descendants += db_entry_folder_get_num_files(folder_entry);
    }

    g_autoptr(DynamicArray) files = fsearch_database_entries_container_steal_descendants(self->file_container,
                                                                                         folder_entry_to_remove,
                                                                                         (int32_t)num_file_descendants);
    num_descendant_counted++;
    g_debug("found descendants in %f seconds", g_timer_elapsed(timer, NULL));

    // We also add the parent folder to the folder array
    if (!folders) {
        folders = darray_new(1);
    }
    darray_add_item(folders, folder_entry_to_remove);

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files);

    // Free all entries
    if (files) {
        for (uint32_t i = 0; i < darray_get_num_items(files); ++i) {
            FsearchDatabaseEntry *file = darray_get_item(files, i);
            free_entry(self->file_pool, file);
        }
        num_file_deletes += darray_get_num_items(files);
    }
    if (folders) {
        // First unwatch all folders. We can't free them in the same loop, because this will invalidate their paths,
        // which are needed in order un-watch them properly
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            unwatch_folder(self, folder, event->monitor_kind);
        }
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            free_entry(self->folder_pool, folder);
        }
        num_folder_deletes += darray_get_num_items(folders);
    }
}

static void
process_attrib_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    FsearchDatabaseEntry *entry = lookup_entry_for_event(self, event, false, false);
    if (!entry) {
        return;
    }

    off_t size = 0;
    time_t mtime = 0;

    off_t old_size = db_entry_get_size(entry);
    time_t old_mtime = db_entry_get_mtime(entry);

    bool is_dir = false;
    if (!fsearch_file_utils_get_info(event->path->str, &mtime, &size, &is_dir)) {
        return;
    }

    if (old_size == size && old_mtime == mtime) {
        return;
    }

    g_autoptr(DynamicArray) entries = darray_new(1);
    darray_add_item(entries, entry);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, is_dir ? entries : NULL, !is_dir ? entries : NULL);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_size(entry, size);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, is_dir ? entries : NULL, !is_dir ? entries : NULL);
    num_attrib_changes++;
}

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    if (!event->name) {
        return;
    }

    event->watched_entry = (FsearchDatabaseEntryFolder *)fsearch_database_entries_container_find(
        self->folder_container,
        (FsearchDatabaseEntry *)event->watched_entry_copy);
    if (!event->watched_entry) {
        g_debug("Watched entry no longer present!");
        return;
    }

    g_debug("[index-%d] %s: %s",
            db_entry_get_db_index((FsearchDatabaseEntry *)event->watched_entry),
            fsearch_folder_monitor_event_kind_to_string(event->event_kind),
            event->path ? event->path->str : "NULL");

    switch (event->event_kind) {
    case FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB:
        process_attrib_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM:
        process_delete_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO:
        if (!lookup_entry_for_event(self, event, false, false)) {
            process_create_event(self, event);
        }
        else {
            process_attrib_event(self, event);
        }
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE:
        process_delete_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_CREATE:
        process_create_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF:
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT:
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF:
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE:
        process_attrib_event(self, event);
        break;
    default:
        g_warning("Unhandled event (%d): ", event->event_kind);
    }
}

static gboolean
process_queued_events_cb(gpointer user_data) {
    g_return_val_if_fail(user_data, G_SOURCE_REMOVE);
    FsearchDatabaseIndex *self = user_data;

    // Assert that this function is running is the worker thread
    g_assert(g_main_context_is_owner(self->worker_ctx));

    // Don't process events until the monitoring was enabled and the index was initialized
    if (g_atomic_int_get(&self->monitor) == 0 || g_atomic_int_get(&self->initialized) == 0) {
        return G_SOURCE_CONTINUE;
    }

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    process_queued_events(self);

    return G_SOURCE_CONTINUE;
}

static void
index_free(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    g_clear_pointer(&self->worker_ctx, g_main_context_unref);

#ifdef HAVE_INOTIFY
    g_clear_pointer(&self->inotify_monitor, fsearch_folder_monitor_inotify_free);
#endif
#ifdef HAVE_FANOTIFY
    g_clear_pointer(&self->fanotify_monitor, fsearch_folder_monitor_fanotify_free);
#endif

    g_clear_pointer(&self->monitor_ctx, g_main_context_unref);

    if (self->event_source) {
        g_source_destroy(self->event_source);
    }
    g_clear_pointer(&self->event_source, g_source_unref);

    g_clear_pointer(&self->include, fsearch_database_include_unref);
    g_clear_object(&self->exclude_manager);

    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    g_clear_pointer(&self->file_container, fsearch_database_entries_container_unref);
    g_clear_pointer(&self->folder_container, fsearch_database_entries_container_unref);

    g_clear_pointer(&self->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&self->folder_pool, fsearch_memory_pool_free_pool);

    g_mutex_clear(&self->mutex);

    g_clear_pointer(&self, free);
}

FsearchDatabaseIndex *
fsearch_database_index_new(uint32_t id,
                           FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           GMainContext *worker_ctx,
                           GMainContext *monitor_ctx,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer event_func_data) {
    FsearchDatabaseIndex *self = calloc(1, sizeof(FsearchDatabaseIndex));
    g_assert(self);

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->event_queue = g_async_queue_new_full((GDestroyNotify)fsearch_folder_monitor_event_free);

    self->event_func = event_func;
    self->event_func_data = event_func_data;

    g_mutex_init(&self->mutex);

    self->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_file_entry(),
                                              (GDestroyNotify)db_entry_destroy);
    self->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                db_entry_get_sizeof_folder_entry(),
                                                (GDestroyNotify)db_entry_destroy);

    self->monitor_ctx = g_main_context_ref(monitor_ctx);

    if (fsearch_database_include_get_monitored(self->include)) {
#ifdef HAVE_FANOTIFY
        self->fanotify_monitor = fsearch_folder_monitor_fanotify_new(self->monitor_ctx, self->event_queue);
#endif
#ifdef HAVE_INOTIFY
        self->inotify_monitor = fsearch_folder_monitor_inotify_new(self->monitor_ctx, self->event_queue);
#endif
    }

    if (self->fanotify_monitor || self->inotify_monitor) {
        self->worker_ctx = g_main_context_ref(worker_ctx);
        self->event_source = g_timeout_source_new_seconds(1);
        g_source_set_priority(self->event_source, G_PRIORITY_DEFAULT_IDLE);
        g_source_set_callback(self->event_source, (GSourceFunc)process_queued_events_cb, self, NULL);
        g_source_attach(self->event_source, self->worker_ctx);
    }

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(uint32_t id,
                                        FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        FsearchMemoryPool *file_pool,
                                        FsearchMemoryPool *folder_pool,
                                        DynamicArray *files,
                                        DynamicArray *folders,
                                        FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseIndex *self = g_slice_new0(FsearchDatabaseIndex);
    g_assert(self);

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->file_pool = file_pool;
    self->folder_pool = folder_pool;

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_ref(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_index_unref(FsearchDatabaseIndex *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self, index_free);
    }
}

FsearchDatabaseInclude *
fsearch_database_index_get_include(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_include_ref(self->include);
}

FsearchDatabaseExcludeManager *
fsearch_database_index_get_exclude_manager(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return g_object_ref(self->exclude_manager);
}

DynamicArray *
fsearch_database_index_get_files(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_entries_container_get_joined(self->file_container);
}

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_entries_container_get_joined(self->folder_container);
}

uint32_t
fsearch_database_index_get_id(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->id;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_get_flags(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->flags;
}

bool
fsearch_database_index_get_one_file_system(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->include ? fsearch_database_include_get_one_file_system(self->include) : false;
}

FsearchDatabaseEntry *
fsearch_database_index_add_file(FsearchDatabaseIndex *self,
                                const char *name,
                                off_t size,
                                time_t mtime,
                                FsearchDatabaseEntryFolder *parent) {
    g_return_val_if_fail(self, NULL);

    FsearchDatabaseEntry *file_entry = fsearch_memory_pool_malloc(self->file_pool);
    db_entry_set_name(file_entry, name);
    db_entry_set_size(file_entry, size);
    db_entry_set_mtime(file_entry, mtime);
    db_entry_set_type(file_entry, DATABASE_ENTRY_TYPE_FILE);
    db_entry_set_parent(file_entry, parent);

    fsearch_database_entries_container_insert(self->file_container, file_entry);

    return file_entry;
}

void
fsearch_database_index_lock(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);
    g_mutex_lock(&self->mutex);
}

void
fsearch_database_index_unlock(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);
    g_mutex_unlock(&self->mutex);
}

static void
scan_status_cb(const char *path, gpointer user_data) {
    FsearchDatabaseIndex *self = user_data;
    if (!self->event_func) {
        return;
    }
    g_autoptr(FsearchDatabaseIndexEvent)
        event = fsearch_database_index_event_new(FSEARCH_DATABASE_INDEX_EVENT_SCANNING, NULL, NULL, path);
    self->event_func(self, event, self->event_func_data);
}

bool
fsearch_database_index_scan(FsearchDatabaseIndex *self, GCancellable *cancellable) {
    g_return_val_if_fail(self, false);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (g_atomic_int_get(&self->initialized) > 0) {
        return true;
    }

    g_autoptr(DynamicArray) files = darray_new(4096);
    g_autoptr(DynamicArray) folders = darray_new(4096);

    if (!db_scan_folder(fsearch_database_include_get_path(self->include),
                        NULL,
                        self->folder_pool,
                        self->file_pool,
                        folders,
                        files,
                        self->exclude_manager,
                        self->fanotify_monitor,
                        self->inotify_monitor,
                        self->id,
                        fsearch_database_include_get_one_file_system(self->include),
                        cancellable,
                        scan_status_cb,
                        self)) {
        return false;
    }

    darray_sort_multi_threaded(folders,
                               (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                               cancellable,
                               NULL);
    darray_sort_multi_threaded(files, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path, cancellable, NULL);

    self->file_container = fsearch_database_entries_container_new(files,
                                                                  TRUE,
                                                                  DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                                  DATABASE_INDEX_PROPERTY_NONE,
                                                                  DATABASE_ENTRY_TYPE_FILE,
                                                                  NULL);
    self->folder_container = fsearch_database_entries_container_new(folders,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                                    DATABASE_INDEX_PROPERTY_NONE,
                                                                    DATABASE_ENTRY_TYPE_FOLDER,
                                                                    NULL);

    g_atomic_int_set(&self->initialized, 1);

    return true;
}

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start) {
    g_return_if_fail(self);

    g_atomic_int_set(&self->monitor, start);
}

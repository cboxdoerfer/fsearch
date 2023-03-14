#define G_LOG_DOMAIN "fsearch-database-index"

#include "fsearch_database_index.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_scan.h"
#include "fsearch_file_utils.h"
#include "fsearch_memory_pool.h"

#include <glib-unix.h>
#include <glib.h>
#include <sys/inotify.h>

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

#define INOTIFY_FOLDER_MASK                                                                                            \
    (IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_UNMOUNT         \
     | IN_MOVE_SELF | IN_CLOSE_WRITE)

struct _FsearchDatabaseIndex {
    FsearchDatabaseInclude *include;
    FsearchDatabaseExcludeManager *exclude_manager;
    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;
    DynamicArray *files;
    DynamicArray *folders;

    FsearchDatabaseIndexPropertyFlags flags;

    GHashTable *watch_descriptors;
    int32_t inotify_fd;
    GSource *monitor_source;
    GMainContext *monitor_ctx;

    GSource *event_source;
    GMainContext *worker_ctx;

    GAsyncQueue *event_queue;
    GHashTable *pending_moves;

    bool propagate_work;

    GMutex mutex;

    uint32_t id;

    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_func_data;

    volatile gint initialized;

    volatile gint ref_count;
};

typedef struct {
    GString *name;
    GString *path;

    int32_t watch_descriptor;

    uint32_t cookie;
    uint32_t mask;
} FsearchDatabaseIndexMonitorEventContext;

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex, fsearch_database_index, fsearch_database_index_ref, fsearch_database_index_unref)

static void
handle_event(FsearchDatabaseIndex *self, int32_t wd, uint32_t mask, uint32_t cookie, const char *name, uint32_t name_len);

static void
handle_queued_events(FsearchDatabaseIndex *self);

static void
propagate_event(FsearchDatabaseIndex *self,
                FsearchDatabaseIndexEventKind kind,
                DynamicArray *folders,
                DynamicArray *files,
                FsearchDatabaseEntry *entry) {
    if (self->propagate_work && self->event_func) {
        g_autoptr(FsearchDatabaseIndexEvent)
            event = fsearch_database_index_event_new(FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files, entry);
        self->event_func(self, event, self->event_func_data);
    }
}

static void
monitor_event_context_free(FsearchDatabaseIndexMonitorEventContext *ctx) {
    if (ctx->name) {
        g_string_free(g_steal_pointer(&ctx->name), TRUE);
    }
    g_clear_pointer(&ctx, free);
}

static FsearchDatabaseIndexMonitorEventContext *
monitor_event_context_new(const char *name, int32_t wd, uint32_t mask, uint32_t cookie) {
    FsearchDatabaseIndexMonitorEventContext *ctx = calloc(1, sizeof(FsearchDatabaseIndexMonitorEventContext));
    g_assert(ctx);

    ctx->name = name ? g_string_new(name) : NULL;
    ctx->watch_descriptor = wd;
    ctx->cookie = cookie;
    ctx->mask = mask;

    return ctx;
}

static void
handle_queued_events(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    if (g_async_queue_length(self->event_queue) == 0) {
        return;
    }

    while (true) {
        FsearchDatabaseIndexMonitorEventContext *ctx = g_async_queue_try_pop(self->event_queue);
        if (!ctx) {
            break;
        }
        g_debug("handle queue event: %s", ctx->name ? ctx->name->str : "");
        handle_event(self,
                     ctx->watch_descriptor,
                     ctx->mask,
                     ctx->cookie,
                     ctx->name ? ctx->name->str : NULL,
                     ctx->name ? ctx->name->len : 0);
        g_clear_pointer(&ctx, monitor_event_context_free);
    }
}

static FsearchDatabaseEntry *
find_entry(FsearchDatabaseIndex *self, const char *name, int32_t wd, uint32_t mask) {
    g_return_val_if_fail(self, NULL);

    FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->watch_descriptors, GINT_TO_POINTER(wd));
    if (!watched_entry) {
        g_debug("no entry for watch descriptor not in hash table!!!");
        return NULL;
    }

    // This event belongs to a child of the watched directory, which we attempt to find right now in our
    // index:
    const bool is_dir = mask & IN_ISDIR ? true : false;

    // The dummy entry is used to mimic the entry we want to find.
    // It has the same name and parent (i.e. the watched directory)
    // and hence the same path. This means it will compare in the same way as the entry we're looking
    // for when it gets passed to the `db_entry_compare_entries_by_path` function.
    g_autofree FsearchDatabaseEntry *entry_tmp =
        db_entry_get_dummy_for_name_and_parent(watched_entry,
                                               name,
                                               is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);

    DynamicArray *array = is_dir ? self->folders : self->files;

    uint32_t idx = 0;
    FsearchDatabaseEntry *entry = NULL;
    if (darray_binary_search_with_data(array,
                                       entry_tmp,
                                       (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path,
                                       NULL,
                                       &idx)) {
        entry = darray_get_item(array, idx);
    }
    else {
        // TODO: If the entry doesn't belong to the index yet, it either means:
        // * it wasn't indexed yet -> solution: we must block event handling until the indexing was
        // completed
        // * the index is corrupt -> solution: we must queue a rebuild
        // For now we just halt the execution.
        g_assert_not_reached();
    }

    db_entry_destroy(entry_tmp);

    return entry;
}

static void
remove_entry(FsearchDatabaseIndex *self, FsearchDatabaseEntry *entry, int32_t watch_descriptor) {
    g_return_if_fail(self);

    DynamicArray *array = NULL;
    FsearchMemoryPool *pool = NULL;

    const bool is_dir = db_entry_is_folder(entry);
    if (is_dir) {
        FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->watch_descriptors,
                                                                  GINT_TO_POINTER(watch_descriptor));
        if (watched_entry != entry) {
            g_assert_not_reached();
        }

        if (db_entry_folder_get_num_children((FsearchDatabaseEntryFolder *)entry) > 0) {
            // TODO : The folder we are about to remove still has children.
            // Not sure if this is expected behavior by inotify etc. which should be dealt with properly.
            // For now, we abort, but it might make sense to remove those children as well.
            g_assert_not_reached();
        }
        else {
            g_hash_table_remove(self->watch_descriptors, GINT_TO_POINTER(watch_descriptor));
            inotify_rm_watch(self->inotify_fd, watch_descriptor);
            array = self->folders;
            pool = self->folder_pool;
        }
    }
    else {
        array = self->files;
        pool = self->file_pool;
    }

    if (array && pool) {
        uint32_t idx = 0;
        if (darray_binary_search_with_data(array,
                                           entry,
                                           (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path,
                                           NULL,
                                           &idx)) {
            FsearchDatabaseEntry *found_entry = darray_get_item(array, idx);
            darray_remove(array, idx, 1);
            g_debug("index store removed entry: %d - %s", idx, db_entry_get_name_raw_for_display(entry));

            fsearch_memory_pool_free(pool, found_entry, TRUE);
        }
    }
}

static void
handle_create_event(FsearchDatabaseIndex *self, FsearchDatabaseEntry *watched_entry, GString *path, const char *name) {
    off_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;
    FsearchDatabaseEntry *entry = NULL;

    if (fsearch_file_utils_get_info(path->str, &mtime, &size, &is_dir)) {
        if (is_dir) {
            g_debug("new folder...");
            entry = (FsearchDatabaseEntry *)fsearch_database_index_add_folder(self,
                                                                              name,
                                                                              path->str,
                                                                              mtime,
                                                                              (FsearchDatabaseEntryFolder *)watched_entry);
        }
        else {
            g_debug("new file...");
            entry = fsearch_database_index_add_file(self, name, size, mtime, (FsearchDatabaseEntryFolder *)watched_entry);
        }

        propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, NULL, NULL, entry);
    }
}

static void
handle_delete_event(FsearchDatabaseIndex *self, const char *name, int32_t wd, uint32_t mask) {
    FsearchDatabaseEntry *entry = find_entry(self, name, wd, mask);
    if (!entry) {
        return;
    }

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, NULL, NULL, entry);
    remove_entry(self, entry, wd);
}

static void
handle_event(FsearchDatabaseIndex *self, int32_t wd, uint32_t mask, uint32_t cookie, const char *name, uint32_t name_len) {
    FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->watch_descriptors, GINT_TO_POINTER(wd));
    if (!watched_entry) {
        g_debug("no entry for watch descriptor not in hash table!!!");
        return;
    }

    g_autoptr(GString) path = NULL;
    if (name_len) {
        path = db_entry_get_path_full(watched_entry);
        g_string_append_c(path, G_DIR_SEPARATOR);
        g_string_append(path, name);
    }

    if (mask & IN_MODIFY) {
        g_print("IN_MODIFY: ");
    }
    else if (mask & IN_ATTRIB) {
        g_print("IN_ATTRIB: ");
    }
    else if (mask & IN_MOVED_FROM) {
        if (!g_hash_table_lookup(self->pending_moves, GUINT_TO_POINTER(cookie))) {
            g_hash_table_insert(self->pending_moves,
                                GUINT_TO_POINTER(cookie),
                                monitor_event_context_new(name_len ? name : NULL, wd, mask, cookie));
        }
        else {
            // TODO: There's already a MOVED_FROM event with the same cookie in the hash table
            // Not sure how to deal with this situation yet (if it is even possible)
            g_assert_not_reached();
        }
    }
    else if (mask & IN_MOVED_TO) {
        FsearchDatabaseIndexMonitorEventContext *ctx = NULL;
        if (g_hash_table_steal_extended(self->pending_moves, GUINT_TO_POINTER(cookie), NULL, (gpointer *)&ctx)) {
            handle_delete_event(self, ctx->name->str, ctx->watch_descriptor, ctx->mask);
            handle_create_event(self, watched_entry, path, name);
        }
        else {
            // TODO: There's no matching MOVED_FROM event with the same cookie in the hash table
            // Not sure how to deal with this situation yet (if it is even possible)
            // g_assert_not_reached();
        }
    }
    else if (mask & IN_DELETE) {
        handle_delete_event(self, name, wd, mask);
    }
    else if (mask & IN_CREATE) {
        handle_create_event(self, watched_entry, path, name);
    }
    else if (mask & IN_DELETE_SELF) {
        g_print("IN_DELETE_SELF: ");
    }
    else if (mask & IN_UNMOUNT) {
        g_print("IN_UNMOUNT: ");
    }
    else if (mask & IN_MOVE_SELF) {
        g_print("IN_MOVE_SELF: ");
    }
    else if (mask & IN_CLOSE_WRITE) {
        g_print("IN_CLOSE_WRITE: ");
    }
    else {
        return;
    }
}

static gboolean
handle_queued_events_cb(gpointer user_data) {
    g_return_val_if_fail(user_data, G_SOURCE_REMOVE);
    FsearchDatabaseIndex *self = user_data;

    // Assert that this function is running is the worker thread
    g_assert(g_main_context_is_owner(self->worker_ctx));

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    handle_queued_events(self);

    return G_SOURCE_CONTINUE;
}

static gboolean
inotify_listener_cb(int fd, GIOCondition condition, gpointer user_data) {
    FsearchDatabaseIndex *self = user_data;

    // Assert that this function is run in the right monitor thread
    g_assert(g_main_context_is_owner(self->monitor_ctx));

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;

    /* Loop while events can be read from inotify file descriptor. */
    while (true) {
        /* Read some events. */

        ssize_t len = read(fd, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) {
            g_debug("failed to read from fd!");
            return G_SOURCE_REMOVE;
        }

        /* If the nonblocking read() found no events to read, then
           it returns -1 with errno set to EAGAIN. In that case,
           we exit the loop. */

        if (len <= 0) {
            g_debug("processed all inotify events.");
            return G_SOURCE_CONTINUE;
        }

        /* Loop over all events in the buffer. */
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            g_debug("queue event: %s", event->name);
            g_async_queue_push(
                self->event_queue,
                monitor_event_context_new(event->len ? event->name : NULL, event->wd, event->mask, event->cookie));
        }
    }
    return G_SOURCE_CONTINUE;
}

static void
index_free(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    g_clear_pointer(&self->worker_ctx, g_main_context_unref);

    g_source_destroy(self->monitor_source);
    g_clear_pointer(&self->monitor_source, g_source_unref);
    g_clear_pointer(&self->monitor_ctx, g_main_context_unref);

    g_source_destroy(self->event_source);
    g_clear_pointer(&self->event_source, g_source_unref);

    g_clear_pointer(&self->watch_descriptors, g_hash_table_unref);

    close(self->inotify_fd);

    g_clear_pointer(&self->include, fsearch_database_include_unref);
    g_clear_object(&self->exclude_manager);

    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    g_clear_pointer(&self->files, darray_unref);
    g_clear_pointer(&self->folders, darray_unref);

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

    self->files = darray_new(1024);
    self->folders = darray_new(1024);

    self->event_queue = g_async_queue_new_full((GDestroyNotify)monitor_event_context_free);

    self->propagate_work = false;

    self->event_func = event_func;
    self->event_func_data = event_func_data;

    g_mutex_init(&self->mutex);

    self->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_file_entry(),
                                              (GDestroyNotify)db_entry_destroy);
    self->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                db_entry_get_sizeof_folder_entry(),
                                                (GDestroyNotify)db_entry_destroy);

    self->watch_descriptors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    self->pending_moves =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)monitor_event_context_free);
    self->inotify_fd = inotify_init1(IN_NONBLOCK);

    self->monitor_ctx = g_main_context_ref(monitor_ctx);
    self->monitor_source = g_unix_fd_source_new(self->inotify_fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
    g_source_set_callback(self->monitor_source, (GSourceFunc)inotify_listener_cb, self, NULL);
    g_source_attach(self->monitor_source, self->monitor_ctx);

    self->worker_ctx = g_main_context_ref(worker_ctx);
    self->event_source = g_timeout_source_new_seconds(1);
    g_source_set_priority(self->event_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback(self->event_source, (GSourceFunc)handle_queued_events_cb, self, NULL);
    g_source_attach(self->event_source, self->worker_ctx);

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

    self->files = darray_ref(files);
    self->folders = darray_ref(folders);

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
    return darray_ref(self->files);
}

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return darray_ref(self->folders);
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
    db_entry_update_parent_size(file_entry);

    darray_insert_item_sorted(self->files, file_entry, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path, NULL);

    return file_entry;
}

FsearchDatabaseEntryFolder *
fsearch_database_index_add_folder(FsearchDatabaseIndex *self,
                                  const char *name,
                                  const char *path,
                                  time_t mtime,
                                  FsearchDatabaseEntryFolder *parent) {
    g_return_val_if_fail(self, NULL);

    // g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    // g_assert_nonnull(locker);

    FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(self->folder_pool);
    db_entry_set_name(entry, name);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_parent(entry, parent);

    const int32_t wd = inotify_add_watch(self->inotify_fd, path, INOTIFY_FOLDER_MASK);
    db_entry_set_wd((FsearchDatabaseEntryFolder *)entry, wd);

    g_hash_table_insert(self->watch_descriptors, GINT_TO_POINTER(wd), entry);

    darray_insert_item_sorted(self->folders, entry, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path, NULL);

    return (FsearchDatabaseEntryFolder *)entry;
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

bool
fsearch_database_index_scan(FsearchDatabaseIndex *self, GCancellable *cancellable) {
    g_return_val_if_fail(self, false);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (g_atomic_int_get(&self->initialized) > 0) {
        return true;
    }

    bool res = false;
    if (db_scan_folder(fsearch_database_include_get_path(self->include),
                       NULL,
                       self->folder_pool,
                       self->file_pool,
                       self->folders,
                       self->files,
                       self->exclude_manager,
                       self->watch_descriptors,
                       self->inotify_fd,
                       fsearch_database_include_get_one_file_system(self->include),
                       cancellable,
                       NULL)) {
        g_atomic_int_set(&self->initialized, 1);
        handle_queued_events(self);
        res = true;
    }
    else {
        // TODO: reset index
    }

    return res;
}

void
fsearch_database_index_set_propagate_work(FsearchDatabaseIndex *self, bool propagate) {
    g_return_if_fail(self);

    self->propagate_work = propagate;
}

#define G_LOG_DOMAIN "fsearch-database-index"

#include "fsearch_database_index.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_scan.h"
#include "fsearch_file_utils.h"
#include "fsearch_memory_pool.h"

#include <config.h>
#include <glib-unix.h>
#include <glib.h>

#ifdef HAVE_FANOTIFY
#include <sys/fanotify.h>

#define FANOTIFY_FOLDER_MASK                                                                                           \
    (FAN_CREATE | FAN_CLOSE_WRITE | FAN_ATTRIB | FAN_DELETE | FAN_DELETE_SELF | FAN_MOVED_TO | FAN_MOVED_FROM          \
     | FAN_MOVE_SELF | FAN_EVENT_ON_CHILD | FAN_ONDIR)
#endif

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>

#define INOTIFY_FOLDER_MASK                                                                                            \
    (IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_UNMOUNT | IN_MOVE_SELF      \
     | IN_CLOSE_WRITE)
#endif

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

struct _FsearchDatabaseIndex {
    FsearchDatabaseInclude *include;
    FsearchDatabaseExcludeManager *exclude_manager;
    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;
    FsearchDatabaseEntriesContainer *folder_container;
    FsearchDatabaseEntriesContainer *file_container;

    FsearchDatabaseIndexPropertyFlags flags;

    GHashTable *handles;
    GHashTable *watch_descriptors;
    int32_t inotify_fd;
    int32_t fanotify_fd;
    GSource *inotify_monitor_source;
    GSource *fanotify_monitor_source;
    GMainContext *monitor_ctx;
    GMutex monitor_lock;

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

typedef enum {
    EVENT_ATTRIB,
    EVENT_CLOSE_WRITE,
    EVENT_MOVED_FROM,
    EVENT_MOVED_TO,
    EVENT_MOVE_SELF,
    EVENT_DELETE,
    EVENT_CREATE,
    EVENT_DELETE_SELF,
    EVENT_UNMOUNT,
} FsearchDatabaseIndexMonitorEvent;

typedef struct {
    GString *name;
    GString *path;

    FsearchDatabaseEntryFolder *watched_entry;
    FsearchDatabaseEntryFolder *watched_entry_copy;

    FsearchDatabaseIndexMonitorEvent kind;
    bool is_dir;
    bool is_inotify_event;
} FsearchDatabaseIndexMonitorEventContext;

static uint32_t num_file_deletes = 0;
static uint32_t num_folder_deletes = 0;
static uint32_t num_file_creates = 0;
static uint32_t num_folder_creates = 0;
static uint32_t num_attrib_changes = 0;
static uint32_t num_descendant_counted = 0;

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex, fsearch_database_index, fsearch_database_index_ref, fsearch_database_index_unref)

static void
process_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexMonitorEventContext *ctx);

static void
process_queued_events(FsearchDatabaseIndex *self);

static void
propagate_event(FsearchDatabaseIndex *self,
                FsearchDatabaseIndexEventKind kind,
                DynamicArray *folders,
                DynamicArray *files,
                FsearchDatabaseEntry *entry) {
    if (!self->event_func) {
        return;
    }
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(kind, folders, files, entry);
    self->event_func(self, event, self->event_func_data);
}

static void
monitor_event_context_free(FsearchDatabaseIndexMonitorEventContext *ctx) {
    if (ctx->name) {
        g_string_free(g_steal_pointer(&ctx->name), TRUE);
    }
    if (ctx->path) {
        g_string_free(g_steal_pointer(&ctx->path), TRUE);
    }
    g_clear_pointer((FsearchDatabaseEntry **)&ctx->watched_entry_copy, db_entry_free_deep_copy);
    g_clear_pointer(&ctx, free);
}

static FsearchDatabaseIndexMonitorEventContext *
monitor_event_context_new(const char *name,
                          FsearchDatabaseEntryFolder *watched_entry,
                          uint32_t kind,
                          bool is_dir,
                          bool is_inotify_event) {
    FsearchDatabaseIndexMonitorEventContext *ctx = calloc(1, sizeof(FsearchDatabaseIndexMonitorEventContext));
    g_assert(ctx);

    ctx->name = name ? g_string_new(name) : NULL;
    ctx->watched_entry_copy = (FsearchDatabaseEntryFolder *)db_entry_get_deep_copy((FsearchDatabaseEntry *)watched_entry);

    if (ctx->name) {
        ctx->path = db_entry_get_path_full((FsearchDatabaseEntry *)ctx->watched_entry_copy);
        g_string_append_c(ctx->path, G_DIR_SEPARATOR);
        g_string_append(ctx->path, ctx->name->str);
    }

    ctx->kind = kind;
    ctx->is_dir = is_dir;
    ctx->is_inotify_event = is_inotify_event;

    return ctx;
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

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL, NULL);
    while (true) {
        FsearchDatabaseIndexMonitorEventContext *ctx = g_async_queue_try_pop(self->event_queue);
        if (!ctx) {
            break;
        }
        double elapsed = g_timer_elapsed(timer, NULL);
        if (elapsed - last_time > 0.2) {
            g_debug("interupt event processing for a while...");
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL, NULL);
            last_time = elapsed;
            g_usleep(G_USEC_PER_SEC * 0.05);
            g_debug("continue event processing...");
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL, NULL);
        }
        process_event(self, ctx);
        g_clear_pointer(&ctx, monitor_event_context_free);
    }
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL, NULL);

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
find_entry_for_event(FsearchDatabaseIndex *self,
                     FsearchDatabaseIndexMonitorEventContext *ctx,
                     uint32_t *index_out,
                     bool expect_success) {
    g_return_val_if_fail(self, NULL);

    if (!ctx->watched_entry) {
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
        db_entry_get_dummy_for_name_and_parent((FsearchDatabaseEntry *)ctx->watched_entry,
                                               ctx->name->str,
                                               ctx->is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);

    FsearchDatabaseEntriesContainer *container = ctx->is_dir ? self->folder_container : self->file_container;

    FsearchDatabaseEntry *entry = fsearch_database_entries_container_find(container, entry_tmp);
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
remove_entry(FsearchDatabaseIndex *self, FsearchDatabaseEntry *entry, bool is_inotify_event) {
    g_return_if_fail(self);

    FsearchDatabaseEntriesContainer *container = NULL;
    FsearchMemoryPool *pool = NULL;

    const bool is_dir = db_entry_is_folder(entry);
    if (is_dir) {
        if (db_entry_folder_get_num_children((FsearchDatabaseEntryFolder *)entry) > 0) {
            // TODO : The folder we are about to remove still has children.
            // Not sure if this is expected behavior by fanotify and inotify, which should be dealt with properly.
            // For now, we abort, but it might make sense to remove those children as well.
            g_assert_not_reached();
        }

        if (is_inotify_event) {
            int32_t wd = db_entry_get_wd((FsearchDatabaseEntryFolder *)entry);
            FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->watch_descriptors, GINT_TO_POINTER(wd));
            if (watched_entry != entry) {
                g_assert_not_reached();
            }
            else {
                inotify_rm_watch(self->inotify_fd, wd);
                g_hash_table_remove(self->watch_descriptors, GINT_TO_POINTER(wd));
            }
        }

        container = self->folder_container;
        pool = self->folder_pool;
    }
    else {
        container = self->file_container;
        pool = self->file_pool;
    }

    if (container && pool) {
        FsearchDatabaseEntry *found_entry = fsearch_database_entries_container_steal(container, entry);
        if (found_entry) {
            db_entry_set_parent(found_entry, NULL);
            fsearch_memory_pool_free(pool, found_entry, TRUE);
            is_dir ? num_folder_deletes++ : num_file_deletes++;
        }
        else {
            g_assert_not_reached();
        }
        // uint32_t idx = 0;
        // if (darray_binary_search_with_data(array,
        //                                    entry,
        //                                    (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
        //                                    NULL,
        //                                    &idx)) {
        //     FsearchDatabaseEntry *found_entry = darray_get_item(array, idx);
        //     darray_remove(array, idx, 1);

        //    // Un-parent the entry to update its current parents state
        //    db_entry_set_parent(found_entry, NULL);
        //    fsearch_memory_pool_free(pool, found_entry, TRUE);
        //    is_dir ? num_folder_deletes++ : num_file_deletes++;
        //}
    }
}

static void
process_create_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexMonitorEventContext *ctx) {
    off_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;

    FsearchDatabaseEntry *entry = NULL;
    g_autoptr(DynamicArray) folders = NULL;
    g_autoptr(DynamicArray) files = NULL;

    if (fsearch_file_utils_get_info(ctx->path->str, &mtime, &size, &is_dir)) {
        if (is_dir) {
            folders = darray_new(128);
            files = darray_new(128);
            if (db_scan_folder(ctx->path->str,
                               ctx->watched_entry,
                               self->folder_pool,
                               self->file_pool,
                               folders,
                               files,
                               self->exclude_manager,
                               self->handles,
                               self->watch_descriptors,
                               &self->monitor_lock,
                               self->fanotify_fd,
                               self->inotify_fd,
                               self->id,
                               fsearch_database_include_get_one_file_system(self->include),
                               NULL,
                               NULL)) {
                for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
                    FsearchDatabaseEntry *folder = darray_get_item(folders, i);
                    // g_debug("insert folder");
                    fsearch_database_entries_container_insert(self->folder_container, folder);
                    // darray_insert_item_sorted(self->folders,
                    //                           folder,
                    //                           (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                    //                           NULL);
                    num_folder_creates++;
                }
                for (uint32_t i = 0; i < darray_get_num_items(files); ++i) {
                    FsearchDatabaseEntry *file = darray_get_item(files, i);
                    // g_debug("insert file");
                    fsearch_database_entries_container_insert(self->file_container, file);
                    // darray_insert_item_sorted(self->files,
                    //                           file,
                    //                           (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                    //                           NULL);
                    num_file_creates++;
                }
            }
        }
        else {
            // g_debug("insert file");
            entry = fsearch_database_index_add_file(self, ctx->name->str, size, mtime, ctx->watched_entry);
            num_file_creates++;
        }

        propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files, entry);
    }
}

static void
process_delete_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexMonitorEventContext *ctx) {
    uint32_t entry_idx = 0;
    FsearchDatabaseEntry *entry = find_entry_for_event(self, ctx, &entry_idx, false);
    if (!entry) {
        return;
    }
    g_autoptr(DynamicArray) folders = NULL;
    g_autoptr(DynamicArray) files = NULL;
    if (db_entry_is_folder(entry)) {
        g_autoptr(GTimer) timer = g_timer_new();
        FsearchDatabaseEntryFolder *folder_entry_to_remove = (FsearchDatabaseEntryFolder *)entry;

        folders = fsearch_database_entries_container_steal_descendants(self->folder_container, folder_entry_to_remove, -1);

        // It's worth to iterate over all folders to calculate the exact number of file descendants we must find,
        // because this means we can steal the files in huge chunks, which is much faster.
        uint32_t num_file_descendants = db_entry_folder_get_num_files(folder_entry_to_remove);
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntryFolder *folder_entry = darray_get_item(folders, i);
            num_file_descendants += db_entry_folder_get_num_files(folder_entry);
        }

        files = fsearch_database_entries_container_steal_descendants(self->file_container,
                                                                     folder_entry_to_remove,
                                                                     (int32_t)num_file_descendants);
        num_descendant_counted++;
        g_debug("found descendants in %f seconds", g_timer_elapsed(timer, NULL));
    }

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files, entry);

    if (files) {
        for (uint32_t i = 0; i < darray_get_num_items(files); ++i) {
            FsearchDatabaseEntry *file = darray_get_item(files, i);
            db_entry_set_parent(file, NULL);
            fsearch_memory_pool_free(self->file_pool, file, TRUE);
        }
    }
    if (folders) {
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            db_entry_set_parent(folder, NULL);
            fsearch_memory_pool_free(self->folder_pool, folder, TRUE);
        }
    }
    remove_entry(self, entry, ctx->is_inotify_event);
}

static void
process_attrib_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexMonitorEventContext *ctx) {
    FsearchDatabaseEntry *entry = find_entry_for_event(self, ctx, NULL, false);
    if (!entry) {
        return;
    }

    off_t size = 0;
    time_t mtime = 0;

    off_t old_size = db_entry_get_size(entry);
    time_t old_mtime = db_entry_get_mtime(entry);

    bool is_dir = false;
    if (fsearch_file_utils_get_info(ctx->path->str, &mtime, &size, &is_dir)) {
        if (old_size != size || old_mtime != mtime) {
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, NULL, NULL, entry);
            if (mtime != old_mtime) {
                db_entry_set_mtime(entry, mtime);
            }
            if (size != old_size) {
                db_entry_set_size(entry, size);
            }
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, NULL, NULL, entry);
            num_attrib_changes++;
        }
    }
}

static const char *
inotify_event_kind_to_string(FsearchDatabaseIndexMonitorEvent kind) {
    switch (kind) {
    case EVENT_ATTRIB:
        return "ATTRIB";
    case EVENT_MOVED_FROM:
        return "MOVED_FROM";
    case EVENT_MOVED_TO:
        return "MOVED_TO";
    case EVENT_DELETE:
        return "DELETE";
    case EVENT_CREATE:
        return "CREATE";
    case EVENT_DELETE_SELF:
        return "DELETE_SELF";
    case EVENT_UNMOUNT:
        return "UNMOUNT";
    case EVENT_MOVE_SELF:
        return "MOVE_SELF";
    case EVENT_CLOSE_WRITE:
        return "CLOSE_WRITE";
    default:
        return "INVALID";
    }
}

static void
process_event(FsearchDatabaseIndex *self, FsearchDatabaseIndexMonitorEventContext *ctx) {
    if (!ctx->name) {
        return;
    }

    ctx->watched_entry = (FsearchDatabaseEntryFolder *)
        fsearch_database_entries_container_find(self->folder_container, (FsearchDatabaseEntry *)ctx->watched_entry_copy);
    if (!ctx->watched_entry) {
        g_debug("Watched entry no longer present!");
        return;
    }

    g_debug("[index-%d] %s: %s",
            db_entry_get_db_index((FsearchDatabaseEntry *)ctx->watched_entry),
            inotify_event_kind_to_string(ctx->kind),
            ctx->path ? ctx->path->str : "NULL");

    switch (ctx->kind) {
    case EVENT_ATTRIB:
        process_attrib_event(self, ctx);
        break;
    case EVENT_MOVED_FROM:
        process_delete_event(self, ctx);
        break;
    case EVENT_MOVED_TO:
        if (!find_entry_for_event(self, ctx, NULL, false)) {
            process_create_event(self, ctx);
        }
        else {
            process_attrib_event(self, ctx);
        }
        break;
    case EVENT_DELETE:
        process_delete_event(self, ctx);
        break;
    case EVENT_CREATE:
        process_create_event(self, ctx);
        break;
    case EVENT_DELETE_SELF:
        break;
    case EVENT_UNMOUNT:
        break;
    case EVENT_MOVE_SELF:
        break;
    case EVENT_CLOSE_WRITE:
        process_attrib_event(self, ctx);
        break;
    default:
        g_warning("Unhandled event (%d): ", ctx->kind);
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

#ifdef HAVE_FANOTIFY
// From tracker-miners:
// https://gitlab.gnome.org/GNOME/tracker-miners/-/blob/master/src/miners/fs/tracker-monitor-fanotify.c
/* Binary compatible with the last portions of fanotify_event_info_fid */
typedef struct {
    fsid_t fsid;
    struct file_handle handle;
} FsearchDatabaseIndexHandleData;

static inline GBytes *
create_bytes_for_static_handle(FsearchDatabaseIndexHandleData *handle) {
    return g_bytes_new_static(handle, sizeof(FsearchDatabaseIndexHandleData) + handle->handle.handle_bytes);
}

static gboolean
fanotify_listener_cb(int fd, GIOCondition condition, gpointer user_data) {
    FsearchDatabaseIndex *self = user_data;

    // Assert that this function is run in the right monitor thread
    g_assert(g_main_context_is_owner(self->monitor_ctx));

    struct fanotify_event_metadata buf[200] = {};

    for (;;) {
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) {
            g_debug("[fanotify_listener] failed to read from fd!");
            return G_SOURCE_REMOVE;
        }

        /* Check if end of available data reached. */
        if (len <= 0) {
            return G_SOURCE_CONTINUE;
        }

        for (const struct fanotify_event_metadata *metadata = (struct fanotify_event_metadata *)buf;
             FAN_EVENT_OK(metadata, len);
             metadata = FAN_EVENT_NEXT(metadata, len)) {

            /* Check that run-time and compile-time structures match. */
            if (metadata->vers != FANOTIFY_METADATA_VERSION) {
                g_warning("[fanotify_listener] fanotify ABI mismatch, monitoring is disabled");
                return G_SOURCE_REMOVE;
            }

            struct fanotify_event_info_fid *fid = (struct fanotify_event_info_fid *)(metadata + 1);
            struct file_handle *file_handle = (struct file_handle *)fid->handle;

            /* Ensure that the event info is of the correct type. */
            g_assert(fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID_NAME);

            const char *file_name = (const char *)(file_handle->f_handle + file_handle->handle_bytes);
            if (g_strcmp0(file_name, ".") == 0) {
                file_name = NULL;
            }

            /* fsid/handle portions are compatible with HandleData */
            FsearchDatabaseIndexHandleData *handle = (FsearchDatabaseIndexHandleData *)&fid->fsid;
            g_autoptr(GBytes) fid_bytes = create_bytes_for_static_handle(handle);

            g_mutex_lock(&self->monitor_lock);
            FsearchDatabaseEntryFolder *watched_entry = g_hash_table_lookup(self->handles, fid_bytes);
            g_mutex_unlock(&self->monitor_lock);

            if (!watched_entry) {
                g_warning("[fanotify_listener] no watched entry for handle found: %llu -> %s",
                          metadata->mask,
                          file_name ? file_name : "UNKNOWN");
                continue;
            }

            const bool is_dir = metadata->mask & FAN_ONDIR ? true : false;

            // fanotify allows mask to have bits for multiple events set, so we mustn't use if/else branches,
            // but test for all bits to not miss any event
            if (metadata->mask & FAN_CREATE) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_CREATE, is_dir, false));
            }
            if (metadata->mask & FAN_ATTRIB) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_ATTRIB, is_dir, false));
            }
            if (metadata->mask & FAN_CLOSE_WRITE) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_CLOSE_WRITE, is_dir, false));
            }
            if (metadata->mask & FAN_MOVED_TO) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_MOVED_TO, is_dir, false));
            }
            if (metadata->mask & FAN_MOVED_FROM) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_MOVED_FROM, is_dir, false));
            }
            if (metadata->mask & FAN_MOVE_SELF) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_DELETE, is_dir, false));
            }
            if (metadata->mask & FAN_DELETE) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_DELETE, is_dir, false));
            }
            if (metadata->mask & FAN_DELETE_SELF) {
                g_async_queue_push(self->event_queue,
                                   monitor_event_context_new(file_name, watched_entry, EVENT_DELETE, is_dir, false));
            }
        }
    }

    return G_SOURCE_REMOVE;
}
#endif

#ifdef HAVE_INOTIFY
static uint32_t
get_index_event_kind_for_inotify_mask(uint32_t mask) {
    if (mask & IN_ATTRIB) {
        return EVENT_ATTRIB;
    }
    else if (mask & IN_MOVED_FROM) {
        return EVENT_MOVED_FROM;
    }
    else if (mask & IN_MOVED_TO) {
        return EVENT_MOVED_TO;
    }
    else if (mask & IN_DELETE) {
        return EVENT_DELETE;
    }
    else if (mask & IN_CREATE) {
        return EVENT_CREATE;
    }
    else if (mask & IN_DELETE_SELF) {
        return EVENT_DELETE_SELF;
    }
    else if (mask & IN_UNMOUNT) {
        return EVENT_UNMOUNT;
    }
    else if (mask & IN_MOVE_SELF) {
        return EVENT_MOVE_SELF;
    }
    else if (mask & IN_CLOSE_WRITE) {
        return EVENT_CLOSE_WRITE;
    }
    return 0;
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
            return G_SOURCE_CONTINUE;
        }

        /* Loop over all events in the buffer. */
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            g_mutex_lock(&self->monitor_lock);
            FsearchDatabaseEntryFolder *folder = g_hash_table_lookup(self->watch_descriptors, GINT_TO_POINTER(event->wd));
            g_mutex_unlock(&self->monitor_lock);

            if (!folder) {
                if (event->mask & IN_IGNORED) {
                    // The only expected situation when a watched entry is no longer present for a given event,
                    // is when the IN_IGNORED bit is set. This happens after a watched folder was removed or
                    // moved to a different filesystem and we already removed the watch descriptor while handling
                    // this earlier event.
                    g_debug("[inotify_listener] no watched entry for watch descriptor found: %s (%d) -> %s",
                            inotify_event_kind_to_string(get_index_event_kind_for_inotify_mask(event->mask)),
                            event->mask,
                            event->len ? event->name : "UNKNOWN");
                }
                else {
                    // The IN_IGNORED bit is not set and we don't have an associate watched entry. This is probably
                    // a bug.
                    g_assert_not_reached();
                }
                continue;
            }
            g_async_queue_push(self->event_queue,
                               monitor_event_context_new(event->len ? event->name : NULL,
                                                         folder,
                                                         get_index_event_kind_for_inotify_mask(event->mask),
                                                         event->mask & IN_ISDIR ? true : false,
                                                         true));
        }
    }
    return G_SOURCE_CONTINUE;
}
#endif

static void
index_free(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    g_clear_pointer(&self->worker_ctx, g_main_context_unref);

    if (self->inotify_monitor_source) {
        g_source_destroy(self->inotify_monitor_source);
    }
    g_clear_pointer(&self->inotify_monitor_source, g_source_unref);

    if (self->fanotify_monitor_source) {
        g_source_destroy(self->fanotify_monitor_source);
    }
    g_clear_pointer(&self->fanotify_monitor_source, g_source_unref);

    g_clear_pointer(&self->monitor_ctx, g_main_context_unref);

    if (self->event_source) {
        g_source_destroy(self->event_source);
    }
    g_clear_pointer(&self->event_source, g_source_unref);

    g_clear_pointer(&self->watch_descriptors, g_hash_table_unref);
    g_clear_pointer(&self->handles, g_hash_table_unref);

    if (self->inotify_fd >= 0) {
        close(self->inotify_fd);
    }
    if (self->fanotify_fd >= 0) {
        close(self->fanotify_fd);
    }
    g_mutex_clear(&self->monitor_lock);

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

    self->event_queue = g_async_queue_new_full((GDestroyNotify)monitor_event_context_free);

    self->event_func = event_func;
    self->event_func_data = event_func_data;

    g_mutex_init(&self->mutex);

    self->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_file_entry(),
                                              (GDestroyNotify)db_entry_destroy);
    self->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                db_entry_get_sizeof_folder_entry(),
                                                (GDestroyNotify)db_entry_destroy);

    g_mutex_init(&self->monitor_lock);

    self->inotify_fd = -1;
    self->fanotify_fd = -1;

    if (fsearch_database_include_get_monitored(self->include)) {
        self->watch_descriptors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
        self->handles = g_hash_table_new_full(g_bytes_hash, g_bytes_equal, (GDestroyNotify)g_bytes_unref, NULL);

        self->monitor_ctx = g_main_context_ref(monitor_ctx);

#ifdef HAVE_INOTIFY
        self->inotify_fd = inotify_init1(IN_NONBLOCK);
        if (self->inotify_fd >= 0) {
            self->inotify_monitor_source = g_unix_fd_source_new(self->inotify_fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
            g_source_set_callback(self->inotify_monitor_source, (GSourceFunc)inotify_listener_cb, self, NULL);
            g_source_attach(self->inotify_monitor_source, self->monitor_ctx);
        }
#endif

#ifdef HAVE_FANOTIFY
        self->fanotify_fd = fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK | FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME, O_RDONLY);
        if (self->fanotify_fd >= 0) {
            self->fanotify_monitor_source = g_unix_fd_source_new(self->fanotify_fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
            g_source_set_callback(self->fanotify_monitor_source, (GSourceFunc)fanotify_listener_cb, self, NULL);
            g_source_attach(self->fanotify_monitor_source, self->monitor_ctx);
        }
#endif

#if defined(HAVE_FANOTIFY) || defined(HAVE_INOTIFY)
        self->worker_ctx = g_main_context_ref(worker_ctx);
        self->event_source = g_timeout_source_new_seconds(1);
        g_source_set_priority(self->event_source, G_PRIORITY_DEFAULT_IDLE);
        g_source_set_callback(self->event_source, (GSourceFunc)process_queued_events_cb, self, NULL);
        g_source_attach(self->event_source, self->worker_ctx);
#endif
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
    bool res = false;
    if (db_scan_folder(fsearch_database_include_get_path(self->include),
                       NULL,
                       self->folder_pool,
                       self->file_pool,
                       folders,
                       files,
                       self->exclude_manager,
                       self->handles,
                       self->watch_descriptors,
                       &self->monitor_lock,
                       self->fanotify_fd,
                       self->inotify_fd,
                       self->id,
                       fsearch_database_include_get_one_file_system(self->include),
                       cancellable,
                       NULL)) {
        darray_sort_multi_threaded(folders,
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                                   cancellable,
                                   NULL);
        darray_sort_multi_threaded(files,
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                                   cancellable,
                                   NULL);

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

        res = true;
    }
    else {
        // TODO: reset index
    }

    return res;
}

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start) {
    g_return_if_fail(self);

    g_atomic_int_set(&self->monitor, start);
}

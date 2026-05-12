#define G_LOG_DOMAIN "fsearch-database-index"

#include "fsearch_array.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_scan.h"
#include "fsearch_file_utils.h"
#include "fsearch_folder_monitor_event.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"
#include "fsearch_main_context_utils.h"

#include <config.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gmacros.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

struct _FsearchDatabaseIndex {
    FsearchDatabaseInclude *include;
    FsearchDatabaseExcludeManager *exclude_manager;
    FsearchDatabaseChunkedArray *folder_chunks;
    FsearchDatabaseChunkedArray *file_chunks;

    FsearchDatabaseIndexPropertyFlags flags;

    GMainContext *monitor_ctx;
    FsearchFolderMonitorFanotify *fanotify_monitor;
    FsearchFolderMonitorInotify *inotify_monitor;

    GSource *event_source;
    GSource *root_reappear_poll_source;
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

#define ROOT_REAPPEAR_POLL_SECONDS 3

static uint32_t num_file_deletes = 0;
static uint32_t num_folder_deletes = 0;
static uint32_t num_file_creates = 0;
static uint32_t num_folder_creates = 0;
static uint32_t num_attrib_changes = 0;
static uint32_t num_descendant_counted = 0;

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex,
                    fsearch_database_index,
                    fsearch_database_index_ref,
                    fsearch_database_index_unref)

static void
propagate_event(FsearchDatabaseIndex *self,
                FsearchDatabaseIndexEventKind kind,
                DynamicArray *folders,
                DynamicArray *files);

static void
index_stop_root_reappearance_polling(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    if (self->root_reappear_poll_source) {
        fsearch_main_context_blocking_call(self->worker_ctx,
                                           (FsearchMainContextFunc)g_source_destroy,
                                           self->root_reappear_poll_source);
    }
    g_clear_pointer(&self->root_reappear_poll_source, g_source_unref);
}

static gboolean
index_root_reappear_poll_cb(gpointer user_data) {
    g_return_val_if_fail(user_data, G_SOURCE_REMOVE);
    FsearchDatabaseIndex *self = user_data;

    g_assert(g_main_context_is_owner(self->worker_ctx));

    const char *root_path = fsearch_database_include_get_path(self->include);
    if (!g_file_test(root_path, G_FILE_TEST_IS_DIR)) {
        return G_SOURCE_CONTINUE;
    }

    g_debug("[index-%d] root folder reappeared, rescanning: %s", self->id, root_path);
    if (!fsearch_database_index_scan(self, NULL)) {
        g_debug("[index-%d] rescan after reappear failed, keep polling: %s", self->id, root_path);
        return G_SOURCE_CONTINUE;
    }

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL);
    g_mutex_lock(&self->mutex);

    g_autoptr(DynamicArray) folders = fsearch_database_chunked_array_get_joined(self->folder_chunks);
    g_autoptr(DynamicArray) files = fsearch_database_chunked_array_get_joined(self->file_chunks);

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files);

    g_mutex_unlock(&self->mutex);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL);

    index_stop_root_reappearance_polling(self);
    return G_SOURCE_REMOVE;
}

static void
index_start_root_reappearance_polling(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    if (self->root_reappear_poll_source || !self->worker_ctx) {
        return;
    }

    self->root_reappear_poll_source = g_timeout_source_new_seconds(ROOT_REAPPEAR_POLL_SECONDS);
    g_source_set_priority(self->root_reappear_poll_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback(self->root_reappear_poll_source, index_root_reappear_poll_cb, self, NULL);
    g_source_attach(self->root_reappear_poll_source, self->worker_ctx);

    g_debug("[index-%d] start polling for root folder reappearance every %d seconds",
            self->id,
            ROOT_REAPPEAR_POLL_SECONDS);
}

static void
propagate_event(FsearchDatabaseIndex *self,
                FsearchDatabaseIndexEventKind kind,
                DynamicArray *folders,
                DynamicArray *files) {
    if (!self->event_func) {
        return;
    }
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(kind, folders, files, NULL);
    self->event_func(self, event, self->event_func_data);
}

// region Index Store Worker Functions

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event);

static void
process_queued_events(FsearchDatabaseIndex *self);

static inline bool
is_create_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_CREATE ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_RESCAN;
}

static inline bool
is_delete_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_DELETE ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM;
}

static inline bool
is_attrib_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE;
}

static inline bool
is_special_delete_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF ||
           kind == FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT;
}

static GHashTable *
get_skippable_events(GPtrArray *events, GPtrArray *folder_delete_events) {
    g_autoptr(GHashTable) skippable_events = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (folder_delete_events->len <= 0) {
        return g_steal_pointer(&skippable_events);
    }

    for (uint32_t i = 0; i < events->len; ++i) {
        FsearchFolderMonitorEvent *candidate = g_ptr_array_index(events, i);
        if (!candidate) {
            continue;
        }

        for (uint32_t j = 0; j < folder_delete_events->len; ++j) {
            FsearchFolderMonitorEvent *folder_to_delete = g_ptr_array_index(folder_delete_events, j);
            // Check whether candidate->path->str is strictly under deleted_dir.
            // We require the path to start with deleted_dir followed by '/'
            // to avoid false matches where deleted_dir is a prefix of an
            // unrelated sibling name (e.g. /ab matching /abc).
            const size_t dlen = strlen(folder_to_delete->path->str);
            if (strncmp(candidate->path->str, folder_to_delete->path->str, dlen) == 0
                && candidate->path->str[dlen] == '/') {
                g_hash_table_insert(skippable_events, candidate, NULL);
                break;
            }
        }
    }

    return g_steal_pointer(&skippable_events);
}

static void
process_queued_events(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    const int32_t num_events_queued = g_async_queue_length(self->event_queue);
    if (num_events_queued < 1) {
        return;
    }

    // 1. Pop all events into an array for faster traversal and build an array for all folders to be removed
    // The later will be used to detect skippable delete events. For example, a delete event for folder /a makes the
    // delete event for /a/b obsolete, since by removing /a from the index we also remove all its children
    g_autoptr(GPtrArray) events = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_folder_monitor_event_free);
    g_autoptr(GPtrArray) folder_delete_events = g_ptr_array_new();
    while (true) {
        FsearchFolderMonitorEvent *event = g_async_queue_try_pop(self->event_queue);
        if (!event) {
            break;
        }
        g_ptr_array_add(events, event);
        if (event->is_dir && (is_delete_event(event->event_kind) || is_special_delete_event(event->event_kind))) {
            g_ptr_array_add(folder_delete_events, event);
        }
    }
    // 2. Create a hash table of all events that can be skipped (i.e., they're superseded by another event)
    g_autoptr(GHashTable) skippable_events = get_skippable_events(events, folder_delete_events);

    // 3. Process events
    g_autoptr(GTimer) timer = g_timer_new();
    double last_time = 0.0;

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL);
    g_mutex_lock(&self->mutex);

    uint32_t num_skipped = 0;
    uint32_t processed_count = 0;
    for (uint32_t i = 0; i < events->len; i++) {
        FsearchFolderMonitorEvent *event = g_ptr_array_index(events, i);
        if (g_hash_table_contains(skippable_events, event)) {
            // Event can be skipped
            num_skipped++;
            continue;
        }

        processed_count++;

        const double elapsed = g_timer_elapsed(timer, NULL);
        if (elapsed - last_time > 0.2) {
            g_debug("interrupt event processing for a while...");
            g_mutex_unlock(&self->mutex);
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL);
            last_time = elapsed;
            g_usleep(G_USEC_PER_SEC * 0.05);
            g_debug("continue event processing...");
            propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING, NULL, NULL);
            g_mutex_lock(&self->mutex);
        }
        process_event(self, event);
    }

    g_mutex_unlock(&self->mutex);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING, NULL, NULL);

    const double process_time = g_timer_elapsed(timer, NULL);
    self->max_process_time = MAX(process_time, self->max_process_time);
    g_debug("processed %u of %u queued events: (c: %d/%d d: %d/%d a: %d d: %d s: %d) in %fs (max: %fs)",
            processed_count,
            events->len,
            num_folder_creates,
            num_file_creates,
            num_folder_deletes,
            num_file_deletes,
            num_attrib_changes,
            num_descendant_counted,
            num_skipped,
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
    g_return_val_if_fail(event, NULL);

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
    FsearchDatabaseEntry *entry_tmp =
        db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_SIZE | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME,
                     event->name ? event->name->str : event->path->str,
                     event->name ? event->watched_entry : NULL,
                     event->is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);

    FsearchDatabaseChunkedArray *chunks = event->is_dir ? self->folder_chunks : self->file_chunks;

    FsearchDatabaseEntry *entry = steal
                                      ? fsearch_database_chunked_array_steal(chunks, entry_tmp)
                                      : fsearch_database_chunked_array_find(chunks, entry_tmp);
    // temp entry must be freed properly to make sure its parent gets updated back to its previous state (e.g.
    // regarding num_files/folders)
    g_clear_pointer(&entry_tmp, db_entry_free);
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
    return entry;
}

static void
unwatch_folder(FsearchDatabaseIndex *self, FsearchDatabaseEntry *folder) {
    g_return_if_fail(self);
    g_return_if_fail(db_entry_is_folder(folder));

    if (db_entry_is_monitored_inotify(folder)) {
#ifdef HAVE_INOTIFY
        fsearch_folder_monitor_inotify_unwatch(self->inotify_monitor, folder);
#endif
    }
    if (db_entry_is_monitored_fanotify(folder)) {
#ifdef HAVE_FANOTIFY
        fsearch_folder_monitor_fanotify_unwatch(self->fanotify_monitor, folder);
#endif
    }
}

static void
index_stop_monitoring(FsearchDatabaseIndex *self, FsearchFolderMonitorKind monitor_kind) {
    g_return_if_fail(self);
    g_return_if_fail(monitor_kind != FSEARCH_FOLDER_MONITOR_NONE);
    g_return_if_fail(self);
    fsearch_database_index_start_monitoring(self, false);
    g_atomic_int_set(&self->initialized, 0);

    g_autoptr(DynamicArray) folders = fsearch_database_chunked_array_get_joined(self->folder_chunks);
    g_autoptr(DynamicArray) files = fsearch_database_chunked_array_get_joined(self->file_chunks);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files);

#ifdef HAVE_FANOTIFY
    fsearch_folder_monitor_fanotify_free(self->fanotify_monitor);
    self->fanotify_monitor = fsearch_folder_monitor_fanotify_new(self->monitor_ctx, self->event_queue);
#endif
#ifdef HAVE_INOTIFY
    fsearch_folder_monitor_inotify_free(self->inotify_monitor);
    self->inotify_monitor = fsearch_folder_monitor_inotify_new(self->monitor_ctx, self->event_queue);
#endif

    // Clear the event queue
    while (true) {
        FsearchFolderMonitorEvent *event = g_async_queue_try_pop(self->event_queue);
        if (!event) {
            break;
        }
        g_clear_pointer(&event, fsearch_folder_monitor_event_free);
    }

    fsearch_database_index_start_monitoring(self, true);

    if (files) {
        num_file_deletes += darray_get_num_items(files);
    }
    if (folders) {
        num_folder_deletes += darray_get_num_items(folders);
    }
    g_clear_pointer(&self->file_chunks, fsearch_database_chunked_array_unref);
    g_clear_pointer(&self->folder_chunks, fsearch_database_chunked_array_unref);
}

static void
process_create_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    off_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;

    if (!fsearch_file_utils_get_info(event->path->str, &mtime, &size, &is_dir)) {
        return;
    }

    // Check if an entry must be excluded (now that we know whether it's a file or folder)
    g_autofree char *basename = event->name ? NULL : g_path_get_basename(event->path->str);
    if (fsearch_database_exclude_manager_excludes(self->exclude_manager,
                                                  event->path->str,
                                                  event->name ? event->name->str : basename,
                                                  is_dir)) {
        g_debug("[index-%d] monitor create excluded: %s", self->id, event->path->str);
        return;
    }
    g_autoptr(DynamicArray) folders = NULL;
    g_autoptr(DynamicArray) files = NULL;

    if (is_dir) {
        folders = darray_new(128);
        files = darray_new(128);
        if (db_scan_folder(event->path->str,
                           event->watched_entry,
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
            fsearch_database_chunked_array_insert_array(self->folder_chunks, folders);
            fsearch_database_chunked_array_insert_array(self->file_chunks, files);
        }
    }
    else {
        FsearchDatabaseEntry *entry = db_entry_new_with_attributes(DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME
                                                                   | DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                                                                   event->name->str,
                                                                   event->watched_entry,
                                                                   DATABASE_ENTRY_TYPE_FILE,
                                                                   DATABASE_INDEX_PROPERTY_SIZE,
                                                                   size,
                                                                   DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                                                   mtime,
                                                                   DATABASE_INDEX_PROPERTY_NONE);
        fsearch_database_chunked_array_insert(self->file_chunks, entry);

        files = darray_new(1);
        darray_add_item(files, entry);
    }

    num_folder_creates += folders ? darray_get_num_items(folders) : 0;
    num_file_creates += files ? darray_get_num_items(files) : 0;

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files);
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
        g_clear_pointer(&entry, db_entry_free);
        return;
    }

    g_assert(db_entry_is_folder(entry));

    // Deleting a folder is more complex:
    // 1. Find and remove all its descendants from the index
    // 2. Notify listeners about the removal of all descendants and the folder
    // 3. Unparent, unwatch and free all entries

    g_autoptr(GTimer) timer = g_timer_new();
    FsearchDatabaseEntry *folder_entry_to_remove = entry;

    g_autoptr(DynamicArray) folders = fsearch_database_chunked_array_steal_descendants(
        self->folder_chunks,
        folder_entry_to_remove,
        -1);

    // It's worth iterating over all folders to calculate the exact number of file descendants we must find,
    // because this means we can steal the files in huge chunks, which is much faster.
    uint32_t num_file_descendants = db_entry_folder_get_num_files(folder_entry_to_remove);
    for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
        FsearchDatabaseEntry *folder_entry = darray_get_item(folders, i);
        num_file_descendants += db_entry_folder_get_num_files(folder_entry);
    }

    g_autoptr(DynamicArray) files = fsearch_database_chunked_array_steal_descendants(
        self->file_chunks,
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
            // We must skip unparenting because the parent will be deleted as well
            g_clear_pointer(&file, db_entry_free_no_unparent);
        }
        num_file_deletes += darray_get_num_items(files);
    }
    if (folders) {
        // First unwatch all folders. We can't free them in the same loop, because this will invalidate their paths,
        // which are needed in order un-watch them properly
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            unwatch_folder(self, folder);
        }
        for (uint32_t i = 0; i < darray_get_num_items(folders) - 1; ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            // We must skip unparenting because the parent will be deleted as well
            g_clear_pointer(&folder, db_entry_free_no_unparent);
        }
        FsearchDatabaseEntry *last_folder = darray_get_item(folders, darray_get_num_items(folders) - 1);
        // The last folder, i.e., the folder which was deleted, needs to be deleted properly
        g_clear_pointer(&last_folder, db_entry_free);
        num_folder_deletes += darray_get_num_items(folders);
    }
}

static void
process_rescan_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    g_return_if_fail(self);
    g_return_if_fail(event);
    g_return_if_fail(event->watched_entry);

    process_delete_event(self, event);
    process_create_event(self, event);
}

static void
process_attrib_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    FsearchDatabaseEntry *entry = lookup_entry_for_event(self, event, false, false);
    if (!entry) {
        return;
    }

    off_t size = 0;
    time_t mtime = 0;

    bool is_dir = false;
    if (!fsearch_file_utils_get_info(event->path->str, &mtime, &size, &is_dir)) {
        return;
    }

    const off_t old_size = db_entry_get_size(entry);
    const time_t old_mtime = db_entry_get_mtime(entry);

    if (old_size == size && old_mtime == mtime) {
        return;
    }

    g_autoptr(DynamicArray) entries = darray_new(1);
    darray_add_item(entries, entry);
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
                    is_dir ? entries : NULL,
                    !is_dir ? entries : NULL);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_size(entry, size);
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
                    is_dir ? entries : NULL,
                    !is_dir ? entries : NULL);
    num_attrib_changes++;
}

static void
process_move_or_delete_self_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    g_return_if_fail(self);
    g_return_if_fail(event);

    FsearchDatabaseEntry *entry = lookup_entry_for_event(self, event, false, false);
    if (!entry) {
        g_debug("move_self: entry not found: %s", event->path->str);
        return;
    }
    if (db_entry_get_parent(entry) != NULL) {
        g_debug("move_self: entry is not root entry: %s", event->path->str);
        return;
    }
    // In case of a monitored folder itself being moved, we can ignore the case when the folder is
    const char *root_path = fsearch_database_include_get_path(self->include);
    if (g_strcmp0(event->path->str, root_path) != 0) {
        g_debug("move_self: is not root: %s", root_path);
        return;
    }
    g_debug("move_self: is root: %s", root_path);
    index_stop_monitoring(self, event->monitor_kind);
    index_start_root_reappearance_polling(self);
}

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    event->watched_entry = fsearch_database_chunked_array_find(self->folder_chunks, event->watched_entry_copy);
    if (!event->watched_entry) {
        g_debug("Watched entry no longer present!");
        return;
    }

    switch (event->event_kind) {
    case FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB:
        process_attrib_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM:
        process_delete_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_RESCAN:
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO:
        process_rescan_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE:
        process_delete_event(self, event);
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_CREATE:
        if (!lookup_entry_for_event(self, event, false, false)) {
            process_create_event(self, event);
        }
        else {
            // There's already an entry in the index: force a rescan to get the index in a consistent state
            process_rescan_event(self, event);
        }
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT:
        break;
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF:
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF:
        process_move_or_delete_self_event(self, event);
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

    process_queued_events(self);

    return G_SOURCE_CONTINUE;
}

// endregion

static void
index_free(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

#ifdef HAVE_INOTIFY
    g_clear_pointer(&self->inotify_monitor, fsearch_folder_monitor_inotify_free);
#endif
#ifdef HAVE_FANOTIFY
    g_clear_pointer(&self->fanotify_monitor, fsearch_folder_monitor_fanotify_free);
#endif

    index_stop_root_reappearance_polling(self);

    if (self->event_source) {
        fsearch_main_context_blocking_call(self->worker_ctx,
                                           (FsearchMainContextFunc)g_source_destroy,
                                           self->event_source);
    }

    g_clear_pointer(&self->worker_ctx, g_main_context_unref);

    g_clear_pointer(&self->monitor_ctx, g_main_context_unref);

    g_clear_pointer(&self->event_source, g_source_unref);

    g_clear_pointer(&self->include, fsearch_database_include_unref);
    g_clear_object(&self->exclude_manager);

    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    g_clear_pointer(&self->file_chunks, fsearch_database_chunked_array_unref);
    g_clear_pointer(&self->folder_chunks, fsearch_database_chunked_array_unref);

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

    self->ref_count = 1;

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->event_queue = g_async_queue_new_full((GDestroyNotify)fsearch_folder_monitor_event_free);

    self->event_func = event_func;
    self->event_func_data = event_func_data;

    g_mutex_init(&self->mutex);

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

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(uint32_t id,
                                        FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        DynamicArray *folders,
                                        DynamicArray *files,
                                        FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseIndex *self = g_slice_new0(FsearchDatabaseIndex);
    g_assert(self);

    self->ref_count = 1;

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->folder_chunks = fsearch_database_chunked_array_new(folders,
                                                             TRUE,
                                                             DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                             DATABASE_INDEX_PROPERTY_NONE,
                                                             DATABASE_ENTRY_TYPE_FOLDER,
                                                             NULL,
                                                             (GDestroyNotify)db_entry_free_no_unparent);
    self->file_chunks = fsearch_database_chunked_array_new(files,
                                                           TRUE,
                                                           DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                           DATABASE_INDEX_PROPERTY_NONE,
                                                           DATABASE_ENTRY_TYPE_FILE,
                                                           NULL,
                                                           (GDestroyNotify)db_entry_free_no_unparent);

    g_atomic_int_set(&self->initialized, 1);

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
    return fsearch_database_chunked_array_get_joined(self->file_chunks);
}

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_chunked_array_get_joined(self->folder_chunks);
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
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(
        FSEARCH_DATABASE_INDEX_EVENT_SCANNING,
        NULL,
        NULL,
        path);
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
        index_start_root_reappearance_polling(self);
        return false;
    }

    darray_sort_multi_threaded(folders,
                               (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                               cancellable,
                               NULL);
    darray_sort_multi_threaded(files,
                               (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                               cancellable,
                               NULL);

    self->file_chunks = fsearch_database_chunked_array_new(files,
                                                           TRUE,
                                                           DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                           DATABASE_INDEX_PROPERTY_NONE,
                                                           DATABASE_ENTRY_TYPE_FILE,
                                                           NULL,
                                                           (GDestroyNotify)db_entry_free_no_unparent);
    self->folder_chunks = fsearch_database_chunked_array_new(folders,
                                                             TRUE,
                                                             DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                             DATABASE_INDEX_PROPERTY_NONE,
                                                             DATABASE_ENTRY_TYPE_FOLDER,
                                                             NULL,
                                                             (GDestroyNotify)db_entry_free_no_unparent);

    g_atomic_int_set(&self->initialized, 1);

    return true;
}

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start) {
    g_return_if_fail(self);

    g_atomic_int_set(&self->monitor, start);
}
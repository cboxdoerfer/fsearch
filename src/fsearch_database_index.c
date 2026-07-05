#define G_LOG_DOMAIN "fsearch-database-index"

#include "fsearch_database_index.h"

#include "fsearch_array.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_scan.h"
#include "fsearch_file_utils.h"
#include "fsearch_folder_monitor_event.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"

#include <config.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

    GAsyncQueue *event_queue;

    GMutex mutex;

    gdouble max_process_time;

    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_func_data;

    bool needs_root_reappear_poll;

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
propagate_event(FsearchDatabaseIndex *self,
                FsearchDatabaseIndexEventKind kind,
                DynamicArray *folders,
                DynamicArray *files,
                FsearchDatabaseIndexPropertyFlags affected_sort_orders,
                bool marked) {
    if (!self->event_func) {
        return;
    }
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(kind,
                                                                                  folders,
                                                                                  files,
                                                                                  NULL,
                                                                                  affected_sort_orders,
                                                                                  marked);
    self->event_func(self, event, self->event_func_data);
}

// region Index Store Worker Functions

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event);

static gboolean
process_queued_events(FsearchDatabaseIndex *self);

static inline bool
is_create_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_CREATE || kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO
        || kind == FSEARCH_FOLDER_MONITOR_EVENT_RESCAN;
}

static inline bool
is_delete_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_DELETE || kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM;
}

static inline bool
is_attrib_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB || kind == FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE;
}

static inline bool
is_special_delete_event(FsearchFolderMonitorEventKind kind) {
    return kind == FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF || kind == FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF
        || kind == FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT;
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

static inline FsearchDatabaseEntry *
create_dummy_entry(const char *name, FsearchDatabaseEntry *parent, FsearchDatabaseEntryType type) {
    const FsearchDatabaseIndexPropertyFlags flags = DATABASE_INDEX_PROPERTY_FLAG_SIZE
                                                   | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME;

    return db_entry_new_with_attributes(flags, name, parent, type, DATABASE_INDEX_PROPERTY_NONE);
}

static gboolean
process_queued_events(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, FALSE);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    const int32_t num_events_queued = g_async_queue_length(self->event_queue);
    if (num_events_queued < 1) {
        return FALSE;
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

        process_event(self, event);
    }

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

    return processed_count > 0 ? TRUE : FALSE;
}

static FsearchDatabaseEntry *
lookup_entry_for_event_locked(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event, bool steal, bool expect_success) {
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
    FsearchDatabaseEntry *entry_tmp = create_dummy_entry(event->name ? event->name->str : event->path->str,
                                                         event->name ? event->watched_entry : NULL,
                                                         event->is_dir ? DATABASE_ENTRY_TYPE_FOLDER
                                                                       : DATABASE_ENTRY_TYPE_FILE);

    FsearchDatabaseChunkedArray *chunks = event->is_dir ? self->folder_chunks : self->file_chunks;

    FsearchDatabaseEntry *entry = steal ? fsearch_database_chunked_array_steal(chunks, entry_tmp)
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
index_unwatch_folder_locked(FsearchDatabaseIndex *self, FsearchDatabaseEntry *folder) {
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
index_clear_locked(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);
    g_return_if_fail(self);
    fsearch_database_index_start_monitoring(self, false);
    g_atomic_int_set(&self->initialized, 0);

    g_autoptr(DynamicArray) folders = fsearch_database_chunked_array_get_joined(self->folder_chunks);
    g_autoptr(DynamicArray) files = fsearch_database_chunked_array_get_joined(self->file_chunks);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files, DATABASE_INDEX_PROPERTY_FLAG_ALL, false);

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

static DynamicArray *
get_parent_entries(FsearchDatabaseEntry *entry) {
    const uint32_t num_parents = db_entry_get_depth(entry);
    DynamicArray *parent_folders = darray_new(num_parents);
    while (true) {
        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        if (!parent) {
            break;
        }
        darray_add_item(parent_folders, parent);
        entry = parent;
    }
    return parent_folders;
}

static void
remove_and_free_file_entry_locked(FsearchDatabaseIndex *self, FsearchDatabaseEntry *file) {
    g_assert(db_entry_is_file(file));

    g_autoptr(DynamicArray) files = darray_new(1);
    darray_add_item(files, file);
    db_entry_set_mark(file, 1);

    // By removing a file the size of the parent folders changes as well, hence they need to be updated too:
    // First delete the parent folders from the size sorted indexes
    g_autoptr(DynamicArray) parent_folders = get_parent_entries(file);
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
                    parent_folders,
                    NULL,
                    DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                    false);

    // Delete the file from all indexes
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, NULL, files, DATABASE_INDEX_PROPERTY_FLAG_ALL, true);

    // Unparent the file, thereby updating the parent folders sizes
    num_file_deletes++;
    db_entry_set_parent(file, NULL);
    g_clear_pointer(&file, db_entry_free);

    // Insert the parents with the updated sizes again
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
                    parent_folders,
                    NULL,
                    DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                    false);
}

static void
remove_and_free_folder_entry_locked(FsearchDatabaseIndex *self, FsearchDatabaseEntry *folder_entry_to_remove) {
    g_assert(db_entry_is_folder(folder_entry_to_remove));

    g_autoptr(GTimer) timer = g_timer_new();

    // Deleting a folder is more complex:
    // 1. Find and remove all its descendants from the index
    // 2. Notify listeners about the removal of all descendants and the folder
    // 3. Unparent, unwatch and free all entries

    // Don't forget to mark the folder itself for removal
    db_entry_set_mark(folder_entry_to_remove, 1);

    // Remove all parent folders to update their position in the size sorted indexes
    g_autoptr(DynamicArray) parent_folders = get_parent_entries(folder_entry_to_remove);
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
                    parent_folders,
                    NULL,
                    DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                    false);

    g_autoptr(DynamicArray) folders = fsearch_database_chunked_array_steal_descendants(self->folder_chunks,
                                                                                       folder_entry_to_remove,
                                                                                       -1);

    for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
        FsearchDatabaseEntry *folder_entry = darray_get_item(folders, i);
        db_entry_set_mark(folder_entry, 1);
    }
    // TODO: Rely on num_files to steal descendants instead
    g_autoptr(DynamicArray) files = fsearch_database_chunked_array_steal_marked_folders(self->file_chunks);

    num_descendant_counted++;
    g_debug("found descendants in %f seconds", g_timer_elapsed(timer, NULL));

    // We also add the parent folder to the folder array
    if (!folders) {
        folders = darray_new(1);
    }
    darray_add_item(folders, folder_entry_to_remove);

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files, DATABASE_INDEX_PROPERTY_FLAG_ALL, true);

    // Free all entries
    if (files) {
        for (uint32_t i = 0; i < darray_get_num_items(files); ++i) {
            FsearchDatabaseEntry *file = darray_get_item(files, i);
            // Skip unparenting because the parent will be deleted as well
            g_clear_pointer(&file, db_entry_free_no_unparent);
        }
        num_file_deletes += darray_get_num_items(files);
    }
    if (folders) {
        // First unwatch all folders. We can't free them in the same loop, because this will invalidate their paths,
        // which are needed in order un-watch them properly
        for (uint32_t i = 0; i < darray_get_num_items(folders); ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            index_unwatch_folder_locked(self, folder);
        }
        for (uint32_t i = 0; i < darray_get_num_items(folders) - 1; ++i) {
            FsearchDatabaseEntry *folder = darray_get_item(folders, i);
            g_clear_pointer(&folder, db_entry_free_no_unparent);
        }
        // The last folder (the one explicitly deleted) needs to be freed normally
        FsearchDatabaseEntry *last_folder = darray_get_item(folders, darray_get_num_items(folders) - 1);
        // We must unparent this folder so its parent can update its childcount and size
        db_entry_set_parent(last_folder, NULL);
        g_clear_pointer(&last_folder, db_entry_free);

        // Insert the parents with the updated sizes again
        propagate_event(self,
                        FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
                        parent_folders,
                        NULL,
                        DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                        false);

        num_folder_deletes += darray_get_num_items(folders);
    }
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
        g_debug("[index-%s] monitor create excluded: %s", fsearch_database_index_get_path(self), event->path->str);
        return;
    }
    g_autoptr(DynamicArray) folders = NULL;
    g_autoptr(DynamicArray) files = NULL;

    // Remove watched entry and its parents from the size sorted indexes because while adding the newly created files or
    // folders, they're size will get updated
    const uint32_t watched_entry_depth = db_entry_get_depth(event->watched_entry);
    g_autoptr(DynamicArray) parent_folders = darray_new(watched_entry_depth + 1);
    FsearchDatabaseEntry *parent_tmp = event->watched_entry;
    while (parent_tmp) {
        darray_add_item(parent_folders, parent_tmp);
        parent_tmp = db_entry_get_parent(parent_tmp);
    }
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
                    parent_folders,
                    NULL,
                    DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                    false);

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

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files, DATABASE_INDEX_PROPERTY_FLAG_ALL, false);
    propagate_event(self,
                    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
                    parent_folders,
                    NULL,
                    DATABASE_INDEX_PROPERTY_FLAG_SIZE,
                    false);
}

static void
process_delete_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    FsearchDatabaseEntry *entry = lookup_entry_for_event_locked(self, event, true, false);
    if (!entry) {
        return;
    }

    // Deleting a file is simple:
    // 1. notify listeners about the deletion
    // 2. free the entry
    if (db_entry_is_file(entry)) {
        remove_and_free_file_entry_locked(self, entry);
        return;
    }
    else {
        remove_and_free_folder_entry_locked(self, entry);
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
    FsearchDatabaseEntry *entry = lookup_entry_for_event_locked(self, event, false, false);
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

    FsearchDatabaseIndexPropertyFlags affected_sort_orders = DATABASE_INDEX_PROPERTY_FLAG_NONE;
    if (old_mtime != mtime) {
        affected_sort_orders |= DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME;
    }

    g_autoptr(DynamicArray) folders = NULL;
    if (old_size != size) {
        affected_sort_orders |= DATABASE_INDEX_PROPERTY_FLAG_SIZE;

        // When an entry size changed its parents need to be updated as well, since their size will change as well
        folders = get_parent_entries(entry);
    }

    g_autoptr(DynamicArray) files = NULL;
    if (is_dir) {
        if (!folders) {
            folders = darray_new(1);
        }
        darray_add_item(folders, entry);
    }
    else {
        files = darray_new(1);
        darray_add_item(files, entry);
    }

    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED, folders, files, affected_sort_orders, false);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_size(entry, size);
    propagate_event(self, FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED, folders, files, affected_sort_orders, false);
    num_attrib_changes++;
}

static void
process_move_or_delete_self_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    g_return_if_fail(self);
    g_return_if_fail(event);

    FsearchDatabaseEntry *entry = lookup_entry_for_event_locked(self, event, false, false);
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
    index_clear_locked(self);
    self->needs_root_reappear_poll = true;
}

static void
process_event(FsearchDatabaseIndex *self, FsearchFolderMonitorEvent *event) {
    event->watched_entry = fsearch_database_chunked_array_find(self->folder_chunks, event->watched_entry_copy);
    if (!event->watched_entry) {
        g_debug("Watched entry no longer present: (%s) %s",
                fsearch_folder_monitor_event_kind_to_string(event->event_kind),
                db_entry_get_name_raw(event->watched_entry_copy));
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
        if (!lookup_entry_for_event_locked(self, event, false, false)) {
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

gboolean
fsearch_database_index_process_events(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, FALSE);

    // Assert that this function is running is the worker thread

    // Don't process events until the monitoring was enabled and the index was initialized
    if (g_atomic_int_get(&self->monitor) == 0 || g_atomic_int_get(&self->initialized) == 0) {
        return FALSE;
    }

    return process_queued_events(self);
}

static FsearchDatabaseEntry *
create_dummy_entry_chain(const char *root_path, const char *target_path, FsearchDatabaseEntryType target_type) {
    if (g_strcmp0(root_path, target_path) == 0) {
        // target is the root itself
        return target_type == DATABASE_ENTRY_TYPE_FOLDER
                 ? create_dummy_entry(root_path, NULL, DATABASE_ENTRY_TYPE_FOLDER)
                 : NULL;
    }

    // TODO: Performance
    // Ideally we should avoid so many allocations in the function.

    g_autoptr(GFile) root_file = g_file_new_for_path(root_path);
    g_autoptr(GFile) target_file = g_file_new_for_path(target_path);

    // calculates the relative path: root: /a/b, target: /a/b/c/d -> rel: c/d
    g_autofree char *rel_path = g_file_get_relative_path(root_file, target_file);

    if (!rel_path) {
        return NULL; // target_path is not a child of root_path
    }

    FsearchDatabaseEntry *current = create_dummy_entry(root_path, NULL, DATABASE_ENTRY_TYPE_FOLDER);

    g_auto(GStrv) parts = g_strsplit(rel_path, G_DIR_SEPARATOR_S, -1);

    for (int i = 0; parts[i] != NULL; i++) {
        // The last part takes the requested type (FILE or FOLDER), everything in between is a FOLDER
        FsearchDatabaseEntryType type = (parts[i + 1] == NULL) ? target_type : DATABASE_ENTRY_TYPE_FOLDER;

        FsearchDatabaseEntry *child = create_dummy_entry(parts[i], current, type);
        current = child;
    }

    return current;
}

bool
fsearch_database_index_remove_path(FsearchDatabaseIndex *self, const char *path, bool *root_removed) {
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(path, false);
    g_return_val_if_fail(root_removed, false);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

    // Edge Case: Check if the removed path is the root of this index
    const char *root_path = fsearch_database_include_get_path(self->include);
    if (g_strcmp0(path, root_path) == 0) {
        g_debug("[index-%s] remove_path: root folder removed: %s", fsearch_database_index_get_path(self), root_path);
        index_clear_locked(self);
        self->needs_root_reappear_poll = true;
        *root_removed = true;
        return true;
    }

    // Try finding it as a file first using a dummy entry
    FsearchDatabaseEntry *dummy_file = create_dummy_entry_chain(root_path, path, DATABASE_ENTRY_TYPE_FILE);
    if (dummy_file) {
        FsearchDatabaseEntry *entry = fsearch_database_chunked_array_steal(self->file_chunks, dummy_file);
        g_clear_pointer(&dummy_file, db_entry_free_full);

        if (entry) {
            remove_and_free_file_entry_locked(self, g_steal_pointer(&entry));
            return true;
        }
    }

    // If not a file, try finding it as a folder
    FsearchDatabaseEntry *dummy_folder = create_dummy_entry_chain(root_path, path, DATABASE_ENTRY_TYPE_FOLDER);
    if (dummy_folder) {
        FsearchDatabaseEntry *entry = fsearch_database_chunked_array_steal(self->folder_chunks, dummy_folder);
        g_clear_pointer(&dummy_folder, db_entry_free_full);

        if (entry) {
            remove_and_free_folder_entry_locked(self, g_steal_pointer(&entry));
            return true;
        }
    }

    return false;
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

    self->needs_root_reappear_poll = false;

    g_clear_pointer(&self->monitor_ctx, g_main_context_unref);

    g_clear_pointer(&self->include, fsearch_database_include_unref);
    g_clear_object(&self->exclude_manager);

    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    g_clear_pointer(&self->file_chunks, fsearch_database_chunked_array_unref);
    g_clear_pointer(&self->folder_chunks, fsearch_database_chunked_array_unref);

    g_mutex_clear(&self->mutex);

    g_clear_pointer(&self, free);
}

FsearchDatabaseIndex *
fsearch_database_index_new(FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           GMainContext *monitor_ctx,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer event_func_data) {
    FsearchDatabaseIndex *self = calloc(1, sizeof(FsearchDatabaseIndex));
    g_assert(self);

    self->ref_count = 1;

    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->needs_root_reappear_poll = false;

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

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        DynamicArray *folders,
                                        DynamicArray *files,
                                        FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseIndex *self = g_new0(FsearchDatabaseIndex, 1);
    g_assert(self);

    self->ref_count = 1;

    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;
    self->needs_root_reappear_poll = false;

    self->folder_chunks = fsearch_database_chunked_array_new(folders,
                                                             TRUE,
                                                             DATABASE_INDEX_PROPERTY_PATH,
                                                             DATABASE_INDEX_PROPERTY_NONE,
                                                             DATABASE_ENTRY_TYPE_FOLDER,
                                                             NULL,
                                                             (GDestroyNotify)db_entry_free_no_unparent);
    self->file_chunks = fsearch_database_chunked_array_new(files,
                                                           TRUE,
                                                           DATABASE_INDEX_PROPERTY_PATH,
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

void
fsearch_database_index_set_event_func(FsearchDatabaseIndex *self,
                                      FsearchDatabaseIndexEventFunc event_func,
                                      gpointer event_func_data) {
    g_return_if_fail(self);

    self->event_func = event_func;
    self->event_func_data = event_func_data;
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

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_get_flags(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->flags;
}

const char *
fsearch_database_index_get_path(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->include ? fsearch_database_include_get_path(self->include) : NULL;
}

bool
fsearch_database_index_wants_root_reappear_poll(FsearchDatabaseIndex *self) {
    g_assert(self);

    return self->needs_root_reappear_poll;
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
    g_autoptr(FsearchDatabaseIndexEvent) event = fsearch_database_index_event_new(FSEARCH_DATABASE_INDEX_EVENT_SCANNING,
                                                                                  NULL,
                                                                                  NULL,
                                                                                  path,
                                                                                  DATABASE_INDEX_PROPERTY_FLAG_NONE,
                                                                                  false);
    self->event_func(self, event, self->event_func_data);
}

bool
fsearch_database_index_scan(FsearchDatabaseIndex *self, GCancellable *cancellable) {
    g_return_val_if_fail(self, false);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (self->initialized) {
        // Clear existing index data
        index_clear_locked(self);
    }

    g_autoptr(DynamicArray) files = darray_new(4096);
    g_autoptr(DynamicArray) folders = darray_new(4096);

    self->needs_root_reappear_poll = false;

    g_autoptr(GTimer) scan_timer = g_timer_new();

    if (!db_scan_folder(fsearch_database_include_get_path(self->include),
                        NULL,
                        folders,
                        files,
                        self->exclude_manager,
                        self->fanotify_monitor,
                        self->inotify_monitor,
                        fsearch_database_include_get_one_file_system(self->include),
                        cancellable,
                        scan_status_cb,
                        self)) {
        self->needs_root_reappear_poll = true;
        return false;
    }

    darray_sort_multi_threaded(folders,
                               (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path,
                               cancellable,
                               NULL);
    darray_sort_multi_threaded(files, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_full_path, cancellable, NULL);

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

    const int64_t scan_time = g_get_real_time() / G_USEC_PER_SEC;
    fsearch_database_include_set_last_scan_time(self->include, scan_time);

    const uint32_t scan_duration_ms = (uint32_t)(g_timer_elapsed(scan_timer, NULL) * 1000.0);
    fsearch_database_include_set_last_scan_duration(self->include, scan_duration_ms);

    const uint32_t scan_file_count = files ? darray_get_num_items(files) : 0;
    fsearch_database_include_set_last_scanned_file_count(self->include, scan_file_count);

    const uint32_t scan_folder_count = folders ? darray_get_num_items(folders) : 0;
    fsearch_database_include_set_last_scanned_folder_count(self->include, scan_folder_count);

    g_atomic_int_set(&self->initialized, 1);

    return true;
}

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start) {
    g_return_if_fail(self);

    g_atomic_int_set(&self->monitor, start);
}
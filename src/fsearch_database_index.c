#include "fsearch_database_index.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_work.h"
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

    GHashTable *pending_moves;

    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_user_data;

    GMutex mutex;

    uint32_t id;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex, fsearch_database_index, fsearch_database_index_ref, fsearch_database_index_unref)

static void
index_free(FsearchDatabaseIndex *self) {
    g_return_if_fail(self);

    g_source_destroy(self->monitor_source);
    g_clear_pointer(&self->monitor_source, g_source_unref);

    g_clear_pointer(&self->watch_descriptors, g_hash_table_unref);

    g_clear_pointer(&self->include, fsearch_database_include_unref);
    g_clear_object(&self->exclude_manager);

    g_clear_pointer(&self->files, darray_unref);
    g_clear_pointer(&self->folders, darray_unref);

    g_clear_pointer(&self->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&self->folder_pool, fsearch_memory_pool_free_pool);

    g_mutex_clear(&self->mutex);

    g_clear_pointer(&self, free);
}

static gboolean
inotify_events_cb(int fd, GIOCondition condition, gpointer user_data) {
    FsearchDatabaseIndex *self = user_data;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;

    /* Loop while events can be read from inotify file descriptor. */

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    for (;;) {

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

            /* Print event type. */

            FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->watch_descriptors,
                                                                      GINT_TO_POINTER(event->wd));
            if (!watched_entry) {
                g_debug("no entry for watch descriptor not in hash table!!!");
                return G_SOURCE_REMOVE;
            }

            g_autoptr(GString) path = NULL;
            if (event->len) {
                path = db_entry_get_path_full(watched_entry);
                g_string_append_c(path, G_DIR_SEPARATOR);
                g_string_append(path, event->name);
            }

            FsearchDatabaseIndexEventKind event_kind = NUM_FSEARCH_DATABASE_INDEX_EVENTS;
            if (event->mask & IN_MODIFY) {
                g_print("IN_MODIFY: ");
            }
            else if (event->mask & IN_ATTRIB) {
                event_kind = FSEARCH_DATABASE_INDEX_EVENT_ENTRY_ATTRIBUTE_CHANGED;
            }
            else if (event->mask & IN_MOVED_FROM) {
                // TODO: add to pending moves
                g_print("IN_MOVED_FROM: ");
            }
            else if (event->mask & IN_MOVED_TO) {
                // TODO: find corresponding pending move
                g_print("IN_MOVED_TO: ");
            }
            else if (event->mask & IN_DELETE) {
                event_kind = FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED;
            }
            else if (event->mask & IN_CREATE) {
                event_kind = FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED;
            }
            else if (event->mask & IN_DELETE_SELF) {
                event_kind = FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED;
            }
            else if (event->mask & IN_UNMOUNT) {
                g_print("IN_UNMOUNT: ");
            }
            else if (event->mask & IN_MOVE_SELF) {
                g_print("IN_MOVE_SELF: ");
            }
            else if (event->mask & IN_CLOSE_WRITE) {
                event_kind = FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CHANGED;
            }
            else {
                continue;
            }

            // if (event->mask & IN_ISDIR) {
            //     g_print(" [directory]\n");
            // }
            // else {
            //     g_print(" [file]\n");
            // }
            if (event_kind < NUM_FSEARCH_DATABASE_INDEX_EVENTS && self->event_func) {
                g_print("call event func\n");
                self->event_func(self, event_kind, watched_entry, g_steal_pointer(&path), self->event_user_data);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

FsearchDatabaseIndex *
fsearch_database_index_new(uint32_t id,
                           FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer user_data) {
    FsearchDatabaseIndex *self = g_slice_new0(FsearchDatabaseIndex);
    g_assert(self);

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->files = darray_new(1024);
    self->folders = darray_new(1024);

    self->event_func = event_func;
    self->event_user_data = user_data;

    g_mutex_init(&self->mutex);

    self->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_file_entry(),
                                              (GDestroyNotify)db_entry_destroy);
    self->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                db_entry_get_sizeof_folder_entry(),
                                                (GDestroyNotify)db_entry_destroy);

    self->watch_descriptors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    self->pending_moves = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    self->inotify_fd = inotify_init1(IN_NONBLOCK);
    self->monitor_source = g_unix_fd_source_new(self->inotify_fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
    g_source_set_callback(self->monitor_source, (GSourceFunc)inotify_events_cb, self, NULL);
    g_source_attach(self->monitor_source, NULL);

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
                                        FsearchDatabaseIndexPropertyFlags flags,
                                        FsearchDatabaseIndexEventFunc event_func,
                                        gpointer user_data) {
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

    self->event_func = event_func;
    self->event_user_data = user_data;

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

FsearchDatabaseEntry *
fsearch_database_index_add_file(FsearchDatabaseIndex *self,
                                const char *name,
                                off_t size,
                                time_t mtime,
                                FsearchDatabaseEntryFolder *parent) {
    g_return_val_if_fail(self, NULL);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
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

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

    FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(self->folder_pool);
    db_entry_set_name(entry, name);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_parent(entry, parent);

    const uint32_t wd = inotify_add_watch(self->inotify_fd, path, INOTIFY_FOLDER_MASK);
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

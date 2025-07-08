#include "fsearch_folder_monitor_fanotify.h"

#include <config.h>
#include <glib-unix.h>
#include <sys/fanotify.h>
#include <sys/vfs.h>

#include "fsearch_database_entry.h"
#include "fsearch_folder_monitor_event.h"

#define FANOTIFY_FOLDER_MASK                                                                                           \
    (FAN_CREATE | FAN_CLOSE_WRITE | FAN_ATTRIB | FAN_DELETE | FAN_DELETE_SELF | FAN_MOVED_TO | FAN_MOVED_FROM          \
     | FAN_MOVE_SELF | FAN_EVENT_ON_CHILD | FAN_ONDIR)

struct FsearchFolderMonitorFanotify {
    GHashTable *handles_to_folders;
    GHashTable *folders_to_handles;

    GSource *monitor_source;
    GMainContext *monitor_context;

    GAsyncQueue *event_queue;

    int32_t fd;

    ssize_t file_handle_payload;

    GMutex mutex;
};

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

static inline GBytes *
create_bytes_for_handle(FsearchDatabaseIndexHandleData *handle) {
    return g_bytes_new_take(handle, sizeof(FsearchDatabaseIndexHandleData) + handle->handle.handle_bytes);
}

static gboolean
fanotify_listener_cb(int fd, GIOCondition condition, gpointer user_data) {
    FsearchFolderMonitorFanotify *self = user_data;

    // Assert that this function is run in the right monitor thread
    g_assert(g_main_context_is_owner(self->monitor_context));

    struct fanotify_event_metadata buf[2048] = {};

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

            g_mutex_lock(&self->mutex);
            FsearchDatabaseEntry *watched_entry = g_hash_table_lookup(self->handles_to_folders, fid_bytes);
            g_mutex_unlock(&self->mutex);

            const char *file_name = (const char *)(file_handle->f_handle + file_handle->handle_bytes);
            if (g_strcmp0(file_name, ".") == 0) {
                file_name = watched_entry ? db_entry_get_name_raw_for_display(watched_entry) : NULL;
            }

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
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_CREATE,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_ATTRIB) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_CLOSE_WRITE) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_MOVED_TO) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_MOVED_FROM) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_MOVE_SELF) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_DELETE,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_DELETE) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_DELETE,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
            if (metadata->mask & FAN_DELETE_SELF) {
                g_async_queue_push(self->event_queue,
                                   fsearch_folder_monitor_event_new(file_name,
                                                                    watched_entry,
                                                                    FSEARCH_FOLDER_MONITOR_EVENT_DELETE,
                                                                    FSEARCH_FOLDER_MONITOR_FANOTIFY,
                                                                    is_dir));
            }
        }
    }

    return G_SOURCE_REMOVE;
}
FsearchFolderMonitorFanotify *
fsearch_folder_monitor_fanotify_new(GMainContext *monitor_context, GAsyncQueue *event_queue) {
    g_return_val_if_fail(monitor_context, NULL);
    g_return_val_if_fail(event_queue, NULL);

    g_autofree FsearchFolderMonitorFanotify *self = calloc(1, sizeof(FsearchFolderMonitorFanotify));

    self->fd = fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK | FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME, O_RDONLY);
    if (self->fd < 0) {
        return NULL;
    }

    self->event_queue = g_async_queue_ref(event_queue);

    self->handles_to_folders = g_hash_table_new_full(g_bytes_hash, g_bytes_equal, (GDestroyNotify)g_bytes_unref, NULL);
    self->folders_to_handles =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_bytes_unref);

    self->monitor_context = g_main_context_ref(monitor_context);

    self->monitor_source = g_unix_fd_source_new(self->fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
    g_source_set_callback(self->monitor_source, (GSourceFunc)fanotify_listener_cb, self, NULL);
    g_source_attach(self->monitor_source, self->monitor_context);

    g_mutex_init(&self->mutex);

    return g_steal_pointer(&self);
}

void
fsearch_folder_monitor_fanotify_free(FsearchFolderMonitorFanotify *self) {
    g_return_if_fail(self);

    if (self->monitor_source) {
        g_source_destroy(self->monitor_source);
    }
    g_clear_pointer(&self->monitor_source, g_source_unref);
    g_clear_pointer(&self->folders_to_handles, g_hash_table_unref);
    g_clear_pointer(&self->handles_to_folders, g_hash_table_unref);

    if (self->fd >= 0) {
        close(self->fd);
    }

    g_clear_pointer(&self->monitor_context, g_main_context_unref);
    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    g_mutex_clear(&self->mutex);

    g_clear_pointer(&self, free);
}

bool
fsearch_folder_monitor_fanotify_watch(FsearchFolderMonitorFanotify *self, FsearchDatabaseEntry *folder, const char *path) {
    g_assert(folder != NULL);
    struct statfs buf;
    if (statfs(path, &buf) < 0) {
        if (errno != ENOENT)
            g_warning("Could not get filesystem ID for %s", path);
        return false;
    }

    g_autofree FsearchDatabaseIndexHandleData *handle_data =
        calloc(1, sizeof(FsearchDatabaseIndexHandleData) + self->file_handle_payload);

    while (true) {
        int32_t mntid = -1;
        if (name_to_handle_at(AT_FDCWD, path, (void *)&handle_data->handle, &mntid, 0) < 0) {
            if (errno == EOVERFLOW) {
                /* The payload is not big enough to hold a file_handle,
                 * in this case we get the ideal handle data size, so
                 * fetch that and retry.
                 */
                self->file_handle_payload = handle_data->handle.handle_bytes;
                g_clear_pointer(&handle_data, free);
                handle_data = calloc(1, sizeof(FsearchDatabaseIndexHandleData) + self->file_handle_payload);
                handle_data->handle.handle_bytes = self->file_handle_payload;
                continue;
            }
            else if (errno != ENOENT) {
                g_warning("Could not get file handle for '%s': %m", path);
            }
            return false;
        }
        break;
    }

    memcpy(&handle_data->fsid, &buf.f_fsid, sizeof(fsid_t));

    g_autoptr(GBytes) handle_bytes = create_bytes_for_handle(g_steal_pointer(&handle_data));

    // To avoid a potential race condition, we first add the folder-to-handle associations to the hash tables
    // and only then add the fanotify mark to monitor the folder.
    g_hash_table_insert(self->handles_to_folders, g_bytes_ref(handle_bytes), folder);
    g_hash_table_insert(self->folders_to_handles, folder, g_bytes_ref(handle_bytes));
    if (!fanotify_mark(self->fd, FAN_MARK_ADD | FAN_MARK_ONLYDIR, FANOTIFY_FOLDER_MASK, AT_FDCWD, path)) {
        return true;
    }
    // Failed to monitor the folder -> remove the associations from the hash tables
    g_hash_table_remove(self->handles_to_folders, handle_bytes);
    g_hash_table_remove(self->folders_to_handles, folder);
    return false;
}

void
fsearch_folder_monitor_fanotify_unwatch(FsearchFolderMonitorFanotify *self, FsearchDatabaseEntry *folder) {
    GBytes *fanotify_handle_bytes = g_hash_table_lookup(self->folders_to_handles, folder);
    if (fanotify_handle_bytes) {
        g_autoptr(GString) path_full = db_entry_get_path_full(folder);
        if (fanotify_mark(self->fd, FAN_MARK_REMOVE, FANOTIFY_FOLDER_MASK, AT_FDCWD, path_full->str)) {
            if (errno != ENOENT) {
                g_debug("[unwatch_folder] failed to remove fanotify mark: %s", path_full->str);
            }
        }
        g_hash_table_remove(self->handles_to_folders, fanotify_handle_bytes);
        g_hash_table_remove(self->folders_to_handles, folder);
    }
    else {
        g_autoptr(GString) path_full = db_entry_get_path_full(folder);
        g_debug("[unwatch_folder] no fanotify handle found for folder: %s", path_full->str);
    }
}

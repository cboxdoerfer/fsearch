#include "fsearch_folder_monitor_inotify.h"

#include <config.h>
#include <glib-unix.h>
#include <sys/inotify.h>

#include "fsearch_folder_monitor_event.h"

#define INOTIFY_FOLDER_MASK                                                                                            \
    (IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_UNMOUNT | IN_MOVE_SELF      \
     | IN_CLOSE_WRITE)

struct FsearchFolderMonitorInotify {
    GHashTable *watch_descriptors_to_folders;
    GHashTable *watched_folders_to_descriptors;

    GSource *monitor_source;
    GMainContext *monitor_context;

    GAsyncQueue *event_queue;

    int32_t fd;

    GMutex mutex;
};

static FsearchFolderMonitorEventKind
get_index_event_kind_for_inotify_mask(uint32_t mask) {
    if (mask & IN_ATTRIB) {
        return FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB;
    }
    else if (mask & IN_MOVED_FROM) {
        return FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM;
    }
    else if (mask & IN_MOVED_TO) {
        return FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO;
    }
    else if (mask & IN_DELETE) {
        return FSEARCH_FOLDER_MONITOR_EVENT_DELETE;
    }
    else if (mask & IN_CREATE) {
        return FSEARCH_FOLDER_MONITOR_EVENT_CREATE;
    }
    else if (mask & IN_DELETE_SELF) {
        return FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF;
    }
    else if (mask & IN_UNMOUNT) {
        return FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT;
    }
    else if (mask & IN_MOVE_SELF) {
        return FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF;
    }
    else if (mask & IN_CLOSE_WRITE) {
        return FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE;
    }
    return 0;
}

static gboolean
inotify_listener_cb(int fd, GIOCondition condition, gpointer user_data) {
    FsearchFolderMonitorInotify *self = user_data;

    // Assert that this function is run in the right monitor thread
    g_assert(g_main_context_is_owner(self->monitor_context));

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

            g_mutex_lock(&self->mutex);
            FsearchDatabaseEntryFolder *folder = g_hash_table_lookup(self->watch_descriptors_to_folders,
                                                                     GINT_TO_POINTER(event->wd));
            g_mutex_unlock(&self->mutex);

            if (!folder) {
                if (event->mask & IN_IGNORED) {
                    // The only expected situation when a watched entry is no longer present for a given event,
                    // is when the IN_IGNORED bit is set. This happens after a watched folder was removed or
                    // moved to a different filesystem and we already removed the watch descriptor while handling
                    // this earlier event.
                    g_debug("[inotify_listener] no watched entry for watch descriptor found: %s (%d) -> %s",
                            fsearch_folder_monitor_event_kind_to_string(
                                get_index_event_kind_for_inotify_mask(event->mask)),
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
                               fsearch_folder_monitor_event_new(event->len ? event->name : NULL,
                                                                folder,
                                                                get_index_event_kind_for_inotify_mask(event->mask),
                                                                FSEARCH_FOLDER_MONITOR_INOTIFY,
                                                                event->mask & IN_ISDIR ? true : false));
        }
    }
    return G_SOURCE_CONTINUE;
}

FsearchFolderMonitorInotify *
fsearch_folder_monitor_inotify_new(GMainContext *monitor_context, GAsyncQueue *event_queue) {
    g_return_val_if_fail(monitor_context, NULL);
    g_return_val_if_fail(event_queue, NULL);

    const int32_t fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }

    FsearchFolderMonitorInotify *self = calloc(1, sizeof(FsearchFolderMonitorInotify));

    self->fd = fd;

    self->event_queue = g_async_queue_ref(event_queue);

    self->watch_descriptors_to_folders = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    self->watched_folders_to_descriptors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    self->monitor_context = g_main_context_ref(monitor_context);

    self->monitor_source = g_unix_fd_source_new(self->fd, G_IO_IN | G_IO_ERR | G_IO_HUP);
    g_source_set_callback(self->monitor_source, (GSourceFunc)inotify_listener_cb, self, NULL);
    g_source_attach(self->monitor_source, self->monitor_context);

    return self;
}

void
fsearch_folder_monitor_inotify_free(FsearchFolderMonitorInotify *self) {
    g_return_if_fail(self);

    if (self->monitor_source) {
        g_source_destroy(self->monitor_source);
    }
    g_clear_pointer(&self->monitor_source, g_source_unref);

    g_clear_pointer(&self->monitor_context, g_main_context_unref);

    g_clear_pointer(&self->watched_folders_to_descriptors, g_hash_table_unref);
    g_clear_pointer(&self->watch_descriptors_to_folders, g_hash_table_unref);

    g_clear_pointer(&self->event_queue, g_async_queue_unref);

    if (self->fd >= 0) {
        close(self->fd);
    }
    g_mutex_clear(&self->mutex);

    g_clear_pointer(&self, free);
}

bool
fsearch_folder_monitor_inotify_watch(FsearchFolderMonitorInotify *self, FsearchDatabaseEntry *folder, const char *path) {
    const int32_t wd = inotify_add_watch(self->fd, path, INOTIFY_FOLDER_MASK);
    if (wd < 0) {
        g_debug("failed to add inotify watch");
        return false;
    }
    g_hash_table_insert(self->watch_descriptors_to_folders, GINT_TO_POINTER(wd), folder);
    g_hash_table_insert(self->watched_folders_to_descriptors, folder, GINT_TO_POINTER(wd));
    return true;
}

void
fsearch_folder_monitor_inotify_unwatch(FsearchFolderMonitorInotify *self, FsearchDatabaseEntry *folder) {
    int32_t wd = GPOINTER_TO_INT(g_hash_table_lookup(self->watched_folders_to_descriptors, folder));
    FsearchDatabaseEntry *watched_folder = g_hash_table_lookup(self->watch_descriptors_to_folders, GINT_TO_POINTER(wd));
    if (watched_folder == folder) {
        if (inotify_rm_watch(self->fd, wd)) {
            g_debug("[unwatch_folder] failed to remove inotify watch descriptor: %d", wd);
        }
        g_hash_table_remove(self->watch_descriptors_to_folders, GINT_TO_POINTER(wd));
        g_hash_table_remove(self->watched_folders_to_descriptors, folder);
    }
    else {
        g_assert_not_reached();
    }
}

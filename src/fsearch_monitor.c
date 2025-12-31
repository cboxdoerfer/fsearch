/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define G_LOG_DOMAIN "fsearch-monitor"

#include "fsearch_monitor.h"
#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_exclude_path.h"
#include "fsearch_index.h"

#include <errno.h>
#include <fnmatch.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event))
#define INOTIFY_BUFFER_SIZE (1024 * (INOTIFY_EVENT_SIZE + 16))
#define DEFAULT_COALESCE_INTERVAL_MS 1500
#define POLL_TIMEOUT_MS 100

// Coalesced event state
typedef enum {
    COALESCED_CREATED,
    COALESCED_DELETED,
    COALESCED_MODIFIED,
    COALESCED_NOOP,
} CoalescedState;

typedef struct {
    CoalescedState state;
    bool is_dir;
    char *path;
} CoalescedEvent;

// Raw inotify event
typedef struct {
    int wd;
    uint32_t mask;
    uint32_t cookie;
    char *name;
} MonitorEvent;

struct FsearchMonitor {
    FsearchDatabase *db;

    int inotify_fd;
    GThread *watch_thread;

    // Watch descriptor mappings (protected by watch_mutex)
    GHashTable *wd_to_path; // int -> char*
    GHashTable *path_to_wd; // char* -> int
    GMutex watch_mutex;

    // Event queue and coalescing
    GMutex event_mutex;
    GQueue *event_queue;
    guint coalesce_timer_id;
    uint32_t coalesce_interval_ms;

    // Configuration
    GList *index_paths;
    GList *exclude_paths;
    char **exclude_patterns;
    bool exclude_hidden;

    // Callbacks
    FsearchMonitorCallback callback;
    gpointer callback_data;
    FsearchMonitorCallback prepare_callback;
    gpointer prepare_callback_data;
    FsearchMonitorErrorCallback error_callback;
    gpointer error_callback_data;

    // State
    volatile bool running;
    volatile bool watch_limit_reached;
    volatile bool is_batching;
    volatile bool overflow_occurred;
    uint32_t num_watches;

    GMutex state_mutex;
};

// Context for error callback invocation on main thread
typedef struct {
    FsearchMonitor *monitor;
    FsearchMonitorError error;
} ErrorCallbackContext;

static gboolean
invoke_error_callback_idle(gpointer user_data) {
    ErrorCallbackContext *ctx = user_data;
    if (ctx->monitor->error_callback) {
        ctx->monitor->error_callback(ctx->error, ctx->monitor->error_callback_data);
    }
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

// Schedule error callback on main thread
static void
notify_error(FsearchMonitor *monitor, FsearchMonitorError error) {
    ErrorCallbackContext *ctx = g_new(ErrorCallbackContext, 1);
    ctx->monitor = monitor;
    ctx->error = error;
    g_idle_add(invoke_error_callback_idle, ctx);
}

static void
monitor_event_free(MonitorEvent *event) {
    if (event) {
        g_free(event->name);
        g_free(event);
    }
}

static void
coalesced_event_free(CoalescedEvent *event) {
    if (event) {
        g_free(event->path);
        g_free(event);
    }
}

static bool
should_exclude_name(FsearchMonitor *monitor, const char *name) {
    if (!name) {
        return true;
    }

    // Exclude hidden files if configured
    if (monitor->exclude_hidden && name[0] == '.') {
        return true;
    }

    // Check exclude patterns
    if (monitor->exclude_patterns) {
        for (int i = 0; monitor->exclude_patterns[i]; i++) {
            if (fnmatch(monitor->exclude_patterns[i], name, 0) == 0) {
                return true;
            }
        }
    }

    return false;
}

static bool
is_path_excluded(FsearchMonitor *monitor, const char *path) {
    if (!path) {
        return true;
    }

    for (GList *l = monitor->exclude_paths; l != NULL; l = l->next) {
        FsearchExcludePath *exclude = l->data;
        if (exclude->enabled && strcmp(path, exclude->path) == 0) {
            return true;
        }
    }

    return false;
}

static char *
build_full_path(FsearchMonitor *monitor, int wd, const char *name) {
    g_mutex_lock(&monitor->watch_mutex);
    const char *dir_path = g_hash_table_lookup(monitor->wd_to_path, GINT_TO_POINTER(wd));
    char *result = NULL;

    if (dir_path) {
        if (!name || name[0] == '\0') {
            result = g_strdup(dir_path);
        }
        else {
            result = g_build_filename(dir_path, name, NULL);
        }
    }

    g_mutex_unlock(&monitor->watch_mutex);
    return result;
}

// Build inotify watch mask - IN_EXCL_UNLINK may not be available on old kernels
static uint32_t
get_watch_mask(void) {
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR;
#ifdef IN_EXCL_UNLINK
    mask |= IN_EXCL_UNLINK;
#endif
    return mask;
}

// Add inotify watch for a directory
static int
add_watch(FsearchMonitor *monitor, const char *path) {
    if (monitor->watch_limit_reached) {
        return -1;
    }

    int wd = inotify_add_watch(monitor->inotify_fd, path, get_watch_mask());

    if (wd < 0) {
        if (errno == ENOSPC) {
            if (!monitor->watch_limit_reached) {
                monitor->watch_limit_reached = true;
                g_warning("inotify watch limit reached. File monitoring may be incomplete. "
                          "Please increase inotify limits: "
                          "echo 'fs.inotify.max_user_watches=524288' | sudo tee -a /etc/sysctl.conf && sudo sysctl -p");
            }
        }
        else if (errno == ENOENT) {
            // Directory no longer exists, that's okay
            g_debug("[monitor] directory does not exist: %s", path);
        }
        else if (errno == EACCES) {
            g_debug("[monitor] permission denied: %s", path);
        }
        else {
            g_debug("[monitor] failed to add watch for %s: %s", path, g_strerror(errno));
        }
        return -1;
    }

    // Store mappings (protected by watch_mutex)
    g_mutex_lock(&monitor->watch_mutex);
    char *path_copy = g_strdup(path);
    g_hash_table_insert(monitor->wd_to_path, GINT_TO_POINTER(wd), path_copy);
    g_hash_table_insert(monitor->path_to_wd, path_copy, GINT_TO_POINTER(wd));
    monitor->num_watches++;
    g_mutex_unlock(&monitor->watch_mutex);

    g_debug("[monitor] added watch %d for: %s (total: %u)", wd, path, monitor->num_watches);

    return wd;
}

// Remove inotify watch
static void
remove_watch(FsearchMonitor *monitor, const char *path) {
    g_mutex_lock(&monitor->watch_mutex);

    gpointer wd_ptr = g_hash_table_lookup(monitor->path_to_wd, path);
    if (!wd_ptr) {
        g_mutex_unlock(&monitor->watch_mutex);
        return;
    }

    int wd = GPOINTER_TO_INT(wd_ptr);

    g_hash_table_remove(monitor->wd_to_path, wd_ptr);
    g_hash_table_remove(monitor->path_to_wd, path);

    if (monitor->num_watches > 0) {
        monitor->num_watches--;
    }

    uint32_t current_watches = monitor->num_watches;
    g_mutex_unlock(&monitor->watch_mutex);

    // Remove the kernel watch (outside lock since it's a syscall)
    inotify_rm_watch(monitor->inotify_fd, wd);

    g_debug("[monitor] removed watch for: %s (total: %u)", path, current_watches);
}

// Recursively add watches for a directory tree
static void
add_watches_recursive(FsearchMonitor *monitor, const char *path) {
    if (is_path_excluded(monitor, path)) {
        return;
    }

    if (add_watch(monitor, path) < 0) {
        return;
    }

    // Scan for subdirectories
    GError *error = NULL;
    GDir *dir = g_dir_open(path, 0, &error);
    if (!dir) {
        if (error) {
            g_debug("[monitor] failed to open directory %s: %s", path, error->message);
            g_error_free(error);
        }
        return;
    }

    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (should_exclude_name(monitor, name)) {
            continue;
        }

        g_autofree char *child_path = g_build_filename(path, name, NULL);

        struct stat st;
        if (lstat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            add_watches_recursive(monitor, child_path);
        }
    }

    g_dir_close(dir);
}

// Coalesce events by path
static GHashTable *
coalesce_events(GQueue *events, FsearchMonitor *monitor) {
    // path -> CoalescedEvent*
    GHashTable *result = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)coalesced_event_free);

    while (!g_queue_is_empty(events)) {
        MonitorEvent *ev = g_queue_pop_head(events);

        g_autofree char *path = build_full_path(monitor, ev->wd, ev->name);
        if (!path) {
            monitor_event_free(ev);
            continue;
        }

        bool is_dir = (ev->mask & IN_ISDIR) != 0;
        bool is_create = (ev->mask & (IN_CREATE | IN_MOVED_TO)) != 0;
        bool is_delete = (ev->mask & (IN_DELETE | IN_MOVED_FROM)) != 0;
        bool is_modify = (ev->mask & IN_MODIFY) != 0;

        CoalescedEvent *existing = g_hash_table_lookup(result, path);

        if (!existing) {
            existing = g_new0(CoalescedEvent, 1);
            existing->path = g_strdup(path);
            existing->is_dir = is_dir;

            if (is_create) {
                existing->state = COALESCED_CREATED;
            }
            else if (is_delete) {
                existing->state = COALESCED_DELETED;
            }
            else if (is_modify) {
                existing->state = COALESCED_MODIFIED;
            }
            else {
                existing->state = COALESCED_NOOP;
            }

            g_hash_table_insert(result, existing->path, existing);
        }
        else {
            // Combine with existing
            if (is_create) {
                if (existing->state == COALESCED_DELETED) {
                    existing->state = COALESCED_MODIFIED;
                }
                // else stay as created
            }
            else if (is_delete) {
                if (existing->state == COALESCED_CREATED) {
                    existing->state = COALESCED_NOOP;
                }
                else {
                    existing->state = COALESCED_DELETED;
                }
            }
            else if (is_modify) {
                if (existing->state != COALESCED_DELETED && existing->state != COALESCED_CREATED) {
                    existing->state = COALESCED_MODIFIED;
                }
            }
        }

        monitor_event_free(ev);
    }

    return result;
}

// Apply coalesced changes to database
static void
apply_changes_to_db(FsearchMonitor *monitor, GHashTable *changes) {
    db_lock(monitor->db);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, changes);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        CoalescedEvent *ev = value;

        if (ev->state == COALESCED_NOOP) {
            continue;
        }

        g_debug("[monitor] applying %s: %s (dir=%d)",
                ev->state == COALESCED_CREATED   ? "CREATE"
                : ev->state == COALESCED_DELETED ? "DELETE"
                                                 : "MODIFY",
                ev->path,
                ev->is_dir);

        switch (ev->state) {
        case COALESCED_CREATED: {
            // Check if entry already exists (prevents duplicates from scan + monitor race)
            FsearchDatabaseEntry *existing = db_find_entry_by_path(monitor->db, ev->path);
            if (existing) {
                g_debug("[monitor] entry already exists, skipping create: %s", ev->path);
                break;
            }

            struct stat st;
            if (lstat(ev->path, &st) != 0) {
                g_debug("[monitor] cannot stat new entry: %s", ev->path);
                break;
            }

            // Find parent folder
            g_autofree char *parent_path = g_path_get_dirname(ev->path);
            FsearchDatabaseEntryFolder *parent = db_find_folder_by_path(monitor->db, parent_path);
            if (!parent) {
                g_debug("[monitor] parent not found: %s", parent_path);
                break;
            }

            g_autofree char *name = g_path_get_basename(ev->path);

            if (S_ISDIR(st.st_mode)) {
                FsearchDatabaseEntryFolder *new_folder = db_add_folder(monitor->db, parent, name, st.st_mtime);
                if (new_folder) {
                    // Add watch and scan for contents
                    add_watch(monitor, ev->path);

                    // Scan directory for existing contents
                    GError *error = NULL;
                    GDir *dir = g_dir_open(ev->path, 0, &error);
                    if (dir) {
                        const char *child_name;
                        while ((child_name = g_dir_read_name(dir)) != NULL) {
                            if (should_exclude_name(monitor, child_name)) {
                                continue;
                            }
                            g_autofree char *child_path = g_build_filename(ev->path, child_name, NULL);
                            struct stat child_st;
                            if (lstat(child_path, &child_st) == 0) {
                                if (S_ISDIR(child_st.st_mode)) {
                                    // Recursively add subdirectory
                                    add_watches_recursive(monitor, child_path);
                                    // TODO: Add folder entries recursively
                                }
                                else {
                                    db_add_file(monitor->db, new_folder, child_name, child_st.st_size, child_st.st_mtime);
                                }
                            }
                        }
                        g_dir_close(dir);
                    }
                    else if (error) {
                        g_error_free(error);
                    }
                }
            }
            else {
                db_add_file(monitor->db, parent, name, st.st_size, st.st_mtime);
            }
            break;
        }

        case COALESCED_DELETED: {
            FsearchDatabaseEntry *entry = db_find_entry_by_path(monitor->db, ev->path);
            if (!entry) {
                g_debug("[monitor] entry not found for delete: %s", ev->path);
                break;
            }

            if (db_entry_is_folder(entry)) {
                // Remove watch first
                remove_watch(monitor, ev->path);
                db_remove_folder(monitor->db, (FsearchDatabaseEntryFolder *)entry);
            }
            else {
                db_remove_file(monitor->db, entry);
            }
            break;
        }

        case COALESCED_MODIFIED: {
            if (ev->is_dir) {
                // Directory mtime changed, not much to do
                break;
            }

            FsearchDatabaseEntry *entry = db_find_entry_by_path(monitor->db, ev->path);
            if (!entry) {
                // File might have been created, try adding it
                struct stat st;
                if (lstat(ev->path, &st) == 0 && S_ISREG(st.st_mode)) {
                    g_autofree char *parent_path = g_path_get_dirname(ev->path);
                    FsearchDatabaseEntryFolder *parent = db_find_folder_by_path(monitor->db, parent_path);
                    if (parent) {
                        g_autofree char *name = g_path_get_basename(ev->path);
                        db_add_file(monitor->db, parent, name, st.st_size, st.st_mtime);
                    }
                }
                break;
            }

            struct stat st;
            if (lstat(ev->path, &st) == 0) {
                db_update_file(monitor->db, entry, st.st_size, st.st_mtime);
            }
            break;
        }

        case COALESCED_NOOP:
            break;
        }
    }

    db_unlock(monitor->db);
}

// Process queued events (runs on main thread)
static gboolean
process_events_idle(gpointer user_data) {
    FsearchMonitor *monitor = user_data;

    g_mutex_lock(&monitor->event_mutex);
    GQueue *events = monitor->event_queue;
    monitor->event_queue = g_queue_new();
    monitor->coalesce_timer_id = 0;
    g_mutex_unlock(&monitor->event_mutex);

    if (g_queue_is_empty(events)) {
        g_queue_free(events);
        return G_SOURCE_REMOVE;
    }

    g_debug("[monitor] processing %u queued events", g_queue_get_length(events));

    // Coalesce events
    GHashTable *coalesced = coalesce_events(events, monitor);
    g_queue_free(events);

    // Notify prepare callback (allows UI to invalidate caches before entries are modified)
    if (monitor->prepare_callback) {
        monitor->prepare_callback(monitor->prepare_callback_data);
    }

    // Apply to database
    apply_changes_to_db(monitor, coalesced);
    g_hash_table_unref(coalesced);

    // Notify callback
    if (monitor->callback) {
        monitor->callback(monitor->callback_data);
    }

    return G_SOURCE_REMOVE;
}

// Timer callback to trigger event processing
static gboolean
coalesce_timer_callback(gpointer user_data) {
    FsearchMonitor *monitor = user_data;

    g_mutex_lock(&monitor->event_mutex);
    monitor->coalesce_timer_id = 0;

    // If batching, don't process events now - they'll be flushed after scan
    if (monitor->is_batching) {
        g_mutex_unlock(&monitor->event_mutex);
        g_debug("[monitor] batching mode active, deferring event processing");
        return G_SOURCE_REMOVE;
    }
    g_mutex_unlock(&monitor->event_mutex);

    g_idle_add(process_events_idle, monitor);

    return G_SOURCE_REMOVE;
}

// Queue an event for processing
static void
queue_event(FsearchMonitor *monitor, struct inotify_event *event) {
    MonitorEvent *ev = g_new0(MonitorEvent, 1);
    ev->wd = event->wd;
    ev->mask = event->mask;
    ev->cookie = event->cookie;
    ev->name = (event->len > 0) ? g_strdup(event->name) : NULL;

    g_mutex_lock(&monitor->event_mutex);

    g_queue_push_tail(monitor->event_queue, ev);

    // Start or reset coalesce timer
    if (monitor->coalesce_timer_id == 0) {
        monitor->coalesce_timer_id = g_timeout_add(monitor->coalesce_interval_ms, coalesce_timer_callback, monitor);
    }

    g_mutex_unlock(&monitor->event_mutex);
}

// Watch thread main loop
static gpointer
watch_thread_func(gpointer data) {
    FsearchMonitor *monitor = data;
    char buffer[INOTIFY_BUFFER_SIZE] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool crashed = false;

    g_debug("[monitor] watch thread started");

    while (monitor->running) {
        struct pollfd pfd = {
            .fd = monitor->inotify_fd,
            .events = POLLIN,
        };

        int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            g_warning("[monitor] poll error: %s", g_strerror(errno));
            crashed = true;
            break;
        }

        if (ret == 0 || !monitor->running) {
            continue;
        }

        ssize_t len = read(monitor->inotify_fd, buffer, sizeof(buffer));
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            g_warning("[monitor] read error: %s", g_strerror(errno));
            crashed = true;
            break;
        }

        // Process events
        for (char *ptr = buffer; ptr < buffer + len;) {
            struct inotify_event *event = (struct inotify_event *)ptr;

            // Skip excluded names
            if (event->len > 0 && should_exclude_name(monitor, event->name)) {
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Handle inotify queue overflow - events were lost
            if (event->mask & IN_Q_OVERFLOW) {
                g_warning("[monitor] inotify queue overflow - some events may be lost. "
                          "Consider increasing /proc/sys/fs/inotify/max_queued_events");
                monitor->overflow_occurred = true;
                notify_error(monitor, FSEARCH_MONITOR_ERROR_QUEUE_OVERFLOW);
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            if (event->mask & IN_IGNORED) {
                // Watch was removed
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Queue event for coalesced processing
            queue_event(monitor, event);

            // If a new directory was created, we need to add a watch for it
            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                g_autofree char *new_dir = build_full_path(monitor, event->wd, event->name);
                if (new_dir && !is_path_excluded(monitor, new_dir)) {
                    add_watches_recursive(monitor, new_dir);
                }
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    // Notify if thread exited unexpectedly (not due to stop() being called)
    if (crashed && monitor->running) {
        g_warning("[monitor] watch thread crashed unexpectedly");
        notify_error(monitor, FSEARCH_MONITOR_ERROR_THREAD_CRASHED);
    }

    g_debug("[monitor] watch thread exiting");
    return NULL;
}

// Public API

FsearchMonitor *
fsearch_monitor_new(FsearchDatabase *db, GList *index_paths) {
    g_return_val_if_fail(db != NULL, NULL);

    FsearchMonitor *monitor = g_new0(FsearchMonitor, 1);

    monitor->db = db_ref(db);
    monitor->inotify_fd = -1;
    monitor->coalesce_interval_ms = DEFAULT_COALESCE_INTERVAL_MS;
    monitor->exclude_hidden = true;

    g_mutex_init(&monitor->event_mutex);
    g_mutex_init(&monitor->state_mutex);
    g_mutex_init(&monitor->watch_mutex);

    monitor->event_queue = g_queue_new();

    monitor->wd_to_path = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    monitor->path_to_wd = g_hash_table_new(g_str_hash, g_str_equal);

    // Copy index paths
    if (index_paths) {
        monitor->index_paths = g_list_copy_deep(index_paths, (GCopyFunc)fsearch_index_copy, NULL);
    }

    return monitor;
}

void
fsearch_monitor_free(FsearchMonitor *monitor) {
    if (!monitor) {
        return;
    }

    fsearch_monitor_stop(monitor);

    g_mutex_lock(&monitor->event_mutex);
    if (monitor->event_queue) {
        g_queue_free_full(monitor->event_queue, (GDestroyNotify)monitor_event_free);
        monitor->event_queue = NULL;
    }
    g_mutex_unlock(&monitor->event_mutex);

    g_clear_pointer(&monitor->wd_to_path, g_hash_table_unref);
    g_clear_pointer(&monitor->path_to_wd, g_hash_table_unref);

    if (monitor->index_paths) {
        g_list_free_full(monitor->index_paths, (GDestroyNotify)fsearch_index_free);
    }
    if (monitor->exclude_paths) {
        g_list_free_full(monitor->exclude_paths, (GDestroyNotify)fsearch_exclude_path_free);
    }
    g_clear_pointer(&monitor->exclude_patterns, g_strfreev);

    g_clear_pointer(&monitor->db, db_unref);

    g_mutex_clear(&monitor->event_mutex);
    g_mutex_clear(&monitor->state_mutex);
    g_mutex_clear(&monitor->watch_mutex);

    g_free(monitor);
}

bool
fsearch_monitor_start(FsearchMonitor *monitor) {
    g_return_val_if_fail(monitor != NULL, false);

    if (monitor->running) {
        return true;
    }

    // Initialize inotify
    monitor->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (monitor->inotify_fd < 0) {
        g_warning("[monitor] failed to initialize inotify: %s", g_strerror(errno));
        return false;
    }

    monitor->running = true;
    monitor->watch_limit_reached = false;
    monitor->num_watches = 0;

    // Add watches for all index paths
    for (GList *l = monitor->index_paths; l != NULL; l = l->next) {
        FsearchIndex *index = l->data;
        if (index->enabled && index->path) {
            g_debug("[monitor] adding watches for index: %s", index->path);
            add_watches_recursive(monitor, index->path);
        }
    }

    g_debug("[monitor] added %u watches", monitor->num_watches);

    if (monitor->watch_limit_reached) {
        g_warning("[monitor] watch limit reached during setup. "
                  "File monitoring is incomplete.");
    }

    // Start watch thread
    monitor->watch_thread = g_thread_new("fsearch-monitor", watch_thread_func, monitor);

    g_info("[monitor] file monitoring started with %u watches", monitor->num_watches);

    return true;
}

void
fsearch_monitor_stop(FsearchMonitor *monitor) {
    if (!monitor || !monitor->running) {
        return;
    }

    g_debug("[monitor] stopping...");

    monitor->running = false;

    // Wait for thread to finish
    if (monitor->watch_thread) {
        g_thread_join(monitor->watch_thread);
        monitor->watch_thread = NULL;
    }

    // Cancel pending timer
    g_mutex_lock(&monitor->event_mutex);
    if (monitor->coalesce_timer_id != 0) {
        g_source_remove(monitor->coalesce_timer_id);
        monitor->coalesce_timer_id = 0;
    }
    g_mutex_unlock(&monitor->event_mutex);

    // Close inotify
    if (monitor->inotify_fd >= 0) {
        close(monitor->inotify_fd);
        monitor->inotify_fd = -1;
    }

    // Clear watch tables
    g_hash_table_remove_all(monitor->wd_to_path);
    g_hash_table_remove_all(monitor->path_to_wd);
    monitor->num_watches = 0;

    g_debug("[monitor] stopped");
}

bool
fsearch_monitor_is_running(FsearchMonitor *monitor) {
    return monitor && monitor->running;
}

void
fsearch_monitor_set_coalesce_interval_ms(FsearchMonitor *monitor, uint32_t ms) {
    if (monitor) {
        monitor->coalesce_interval_ms = ms > 0 ? ms : DEFAULT_COALESCE_INTERVAL_MS;
    }
}

void
fsearch_monitor_set_excluded_paths(FsearchMonitor *monitor, GList *excludes) {
    if (!monitor) {
        return;
    }

    if (monitor->exclude_paths) {
        g_list_free_full(monitor->exclude_paths, (GDestroyNotify)fsearch_exclude_path_free);
    }

    monitor->exclude_paths = excludes ? g_list_copy_deep(excludes, (GCopyFunc)fsearch_exclude_path_copy, NULL) : NULL;
}

void
fsearch_monitor_set_exclude_patterns(FsearchMonitor *monitor, char **patterns) {
    if (!monitor) {
        return;
    }

    g_clear_pointer(&monitor->exclude_patterns, g_strfreev);
    monitor->exclude_patterns = patterns ? g_strdupv(patterns) : NULL;
}

void
fsearch_monitor_set_exclude_hidden(FsearchMonitor *monitor, bool exclude) {
    if (monitor) {
        monitor->exclude_hidden = exclude;
    }
}

void
fsearch_monitor_set_callback(FsearchMonitor *monitor, FsearchMonitorCallback callback, gpointer user_data) {
    if (monitor) {
        monitor->callback = callback;
        monitor->callback_data = user_data;
    }
}

void
fsearch_monitor_set_prepare_callback(FsearchMonitor *monitor, FsearchMonitorCallback callback, gpointer user_data) {
    if (monitor) {
        monitor->prepare_callback = callback;
        monitor->prepare_callback_data = user_data;
    }
}

uint32_t
fsearch_monitor_get_num_watches(FsearchMonitor *monitor) {
    return monitor ? monitor->num_watches : 0;
}

bool
fsearch_monitor_watch_limit_reached(FsearchMonitor *monitor) {
    return monitor ? monitor->watch_limit_reached : false;
}

void
fsearch_monitor_set_batching(FsearchMonitor *monitor, bool batching) {
    if (!monitor) {
        return;
    }

    g_mutex_lock(&monitor->event_mutex);
    monitor->is_batching = batching;

    if (batching) {
        // Cancel any pending timer when entering batch mode
        if (monitor->coalesce_timer_id != 0) {
            g_source_remove(monitor->coalesce_timer_id);
            monitor->coalesce_timer_id = 0;
        }
        g_debug("[monitor] entering batch mode");
    }
    else {
        g_debug("[monitor] exiting batch mode");
    }

    g_mutex_unlock(&monitor->event_mutex);
}

bool
fsearch_monitor_is_batching(FsearchMonitor *monitor) {
    return monitor ? monitor->is_batching : false;
}

void
fsearch_monitor_flush_events(FsearchMonitor *monitor) {
    if (!monitor) {
        return;
    }

    g_mutex_lock(&monitor->event_mutex);

    // Cancel any pending timer
    if (monitor->coalesce_timer_id != 0) {
        g_source_remove(monitor->coalesce_timer_id);
        monitor->coalesce_timer_id = 0;
    }

    // Swap out the event queue
    GQueue *events = monitor->event_queue;
    monitor->event_queue = g_queue_new();

    g_mutex_unlock(&monitor->event_mutex);

    if (g_queue_is_empty(events)) {
        g_queue_free(events);
        g_debug("[monitor] flush: no events to process");
        return;
    }

    g_debug("[monitor] flushing %u batched events", g_queue_get_length(events));

    // Coalesce and apply events
    GHashTable *coalesced = coalesce_events(events, monitor);
    g_queue_free(events);

    apply_changes_to_db(monitor, coalesced);
    g_hash_table_unref(coalesced);

    // Notify callback
    if (monitor->callback) {
        monitor->callback(monitor->callback_data);
    }
}

void
fsearch_monitor_set_database(FsearchMonitor *monitor, FsearchDatabase *db) {
    if (!monitor || !db) {
        return;
    }

    g_mutex_lock(&monitor->state_mutex);

    // Replace database reference
    FsearchDatabase *old_db = monitor->db;
    monitor->db = db_ref(db);

    if (old_db) {
        db_unref(old_db);
    }

    g_mutex_unlock(&monitor->state_mutex);

    g_debug("[monitor] database reference updated");
}

void
fsearch_monitor_set_error_callback(FsearchMonitor *monitor, FsearchMonitorErrorCallback callback, gpointer user_data) {
    if (!monitor) {
        return;
    }

    monitor->error_callback = callback;
    monitor->error_callback_data = user_data;
}

bool
fsearch_monitor_overflow_occurred(FsearchMonitor *monitor) {
    return monitor ? monitor->overflow_occurred : false;
}

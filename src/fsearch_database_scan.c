#define G_LOG_DOMAIN "fsearch-database-scan"

#include "fsearch_database_scan.h"

#include "fsearch_database_entry.h"
#include "fsearch_database_sort.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <sys/fanotify.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

#define INOTIFY_FOLDER_MASK                                                                                            \
    (IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_UNMOUNT | IN_MOVE_SELF      \
     | IN_CLOSE_WRITE)

#define FANOTIFY_FOLDER_MASK                                                                                           \
    (FAN_CREATE | FAN_CLOSE_WRITE | FAN_ATTRIB | FAN_DELETE | FAN_DELETE_SELF | FAN_MOVED_TO | FAN_MOVED_FROM          \
     | FAN_MOVE_SELF | FAN_EVENT_ON_CHILD | FAN_ONDIR)

enum {
    WALK_OK = 0,
    WALK_BADIO,
    WALK_CANCEL,
};

typedef struct DatabaseWalkContext {
    GString *path;
    FsearchDatabaseExcludeManager *exclude_manager;
    DynamicArray *folders;
    DynamicArray *files;
    FsearchMemoryPool *folder_pool;
    FsearchMemoryPool *file_pool;
    GHashTable *handles;
    GHashTable *watch_descriptors;
    DynamicArray *watch_descriptor_array;
    int32_t inotify_fd;
    int32_t fanotify_fd;
    bool one_file_system;
    GTimer *timer;
    GMutex *monitor_lock;
    GCancellable *cancellable;
    void (*status_cb)(const char *);

    ssize_t file_handle_payload;
    dev_t root_device_id;
} DatabaseWalkContext;

// copied and modified from tracker-miners:
// https://gitlab.gnome.org/GNOME/tracker-miners/-/blob/master/src/miners/fs/tracker-monitor-fanotify.c

/* Binary compatible with the last portions of fanotify_event_info_fid */
typedef struct {
    fsid_t fsid;
    struct file_handle handle;
} FsearchDatabaseIndexHandleData;

static inline GBytes *
create_bytes_for_handle(FsearchDatabaseIndexHandleData *handle) {
    return g_bytes_new_take(handle, sizeof(FsearchDatabaseIndexHandleData) + handle->handle.handle_bytes);
}

static bool
add_fanotify_mark(DatabaseWalkContext *ctx, const char *path, FsearchDatabaseEntry *watched_folder) {
    g_assert(watched_folder != NULL);

    struct statfs buf;
    if (statfs(path, &buf) < 0) {
        if (errno != ENOENT)
            g_warning("Could not get filesystem ID for %s", path);
        return false;
    }

    g_autofree FsearchDatabaseIndexHandleData *handle_data =
        calloc(1, sizeof(FsearchDatabaseIndexHandleData) + ctx->file_handle_payload);

    while (true) {
        int32_t mntid = -1;
        if (name_to_handle_at(AT_FDCWD, path, (void *)&handle_data->handle, &mntid, 0) < 0) {
            if (errno == EOVERFLOW) {
                /* The payload is not big enough to hold a file_handle,
                 * in this case we get the ideal handle data size, so
                 * fetch that and retry.
                 */
                ctx->file_handle_payload = handle_data->handle.handle_bytes;
                g_clear_pointer(&handle_data, free);
                handle_data = calloc(1, sizeof(FsearchDatabaseIndexHandleData) + ctx->file_handle_payload);
                handle_data->handle.handle_bytes = ctx->file_handle_payload;
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

    GBytes *handle_bytes = create_bytes_for_handle(g_steal_pointer(&handle_data));
    g_hash_table_insert(ctx->handles, handle_bytes, watched_folder);

    if (fanotify_mark(ctx->fanotify_fd, (FAN_MARK_ADD | FAN_MARK_ONLYDIR), FANOTIFY_FOLDER_MASK, AT_FDCWD, path)) {
        g_hash_table_remove(ctx->handles, handle_bytes);
        return false;
    }

    return true;
}

// end tracker-miners copy

static FsearchDatabaseEntryFolder *
add_folder(DatabaseWalkContext *walk_context,
           const char *name,
           const char *path,
           time_t mtime,
           FsearchDatabaseEntryFolder *parent) {
    FsearchDatabaseEntry *folder = fsearch_memory_pool_malloc(walk_context->folder_pool);
    db_entry_set_name(folder, name);
    db_entry_set_type(folder, DATABASE_ENTRY_TYPE_FOLDER);
    db_entry_set_mtime(folder, mtime);
    db_entry_set_parent(folder, (FsearchDatabaseEntryFolder *)parent);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(walk_context->monitor_lock);
    g_assert_nonnull(locker);

    if (walk_context->fanotify_fd >= 0 && add_fanotify_mark(walk_context, path, folder)) {
        // g_debug("added fanotify mark: %s", path);
    }
    else if (walk_context->inotify_fd >= 0) {
        // Use inotify as a fallback
        // g_debug("use inotify as fallback for path: %s", path);
        const int32_t wd = inotify_add_watch(walk_context->inotify_fd, path, INOTIFY_FOLDER_MASK);
        db_entry_set_wd((FsearchDatabaseEntryFolder *)folder, wd);
        g_hash_table_insert(walk_context->watch_descriptors, GINT_TO_POINTER(wd), folder);
        darray_add_item(walk_context->watch_descriptor_array, GINT_TO_POINTER(wd));
    }
    else {
        g_debug("don't monitor: %s", path);
    }

    darray_add_item(walk_context->folders, folder);

    return (FsearchDatabaseEntryFolder *)folder;
}

FsearchDatabaseEntry *
add_file(DatabaseWalkContext *walk_context, const char *name, off_t size, time_t mtime, FsearchDatabaseEntryFolder *parent) {
    FsearchDatabaseEntry *file_entry = fsearch_memory_pool_malloc(walk_context->file_pool);
    db_entry_set_name(file_entry, name);
    db_entry_set_size(file_entry, size);
    db_entry_set_mtime(file_entry, mtime);
    db_entry_set_type(file_entry, DATABASE_ENTRY_TYPE_FILE);
    db_entry_set_parent(file_entry, parent);

    darray_add_item(walk_context->files, file_entry);

    return file_entry;
}

static int
db_folder_scan_recursive(DatabaseWalkContext *walk_context, FsearchDatabaseEntryFolder *parent) {
    if (g_cancellable_is_cancelled(walk_context->cancellable)) {
        g_debug("[db_scan] cancelled");
        return WALK_CANCEL;
    }

    GString *path = walk_context->path;
    g_string_append_c(path, G_DIR_SEPARATOR);

    // remember end of parent path
    const gsize path_len = path->len;

    DIR *dir = NULL;
    if (!(dir = opendir(path->str))) {
        g_debug("[db_scan] failed to open directory: %s", path->str);
        return WALK_BADIO;
    }

    const int dir_fd = dirfd(dir);

    const double elapsed_seconds = g_timer_elapsed(walk_context->timer, NULL);
    if (elapsed_seconds > 0.1) {
        if (walk_context->status_cb) {
            walk_context->status_cb(path->str);
        }
        g_timer_start(walk_context->timer);
    }

    struct dirent *dent = NULL;
    while ((dent = readdir(dir))) {
        if (g_cancellable_is_cancelled(walk_context->cancellable)) {
            g_debug("[db_scan] cancelled");
            g_clear_pointer(&dir, closedir);
            return WALK_CANCEL;
        }

        // TODO: we can test for hidden here to avoid stat call

        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
            continue;
        }
        const size_t d_name_len = strlen(dent->d_name);
        if (d_name_len >= 256) {
            g_warning("[db_scan] file name too long, skipping: \"%s\" (len: %lu)", dent->d_name, d_name_len);
            continue;
        }

        // create full path of file/folder
        g_string_truncate(path, path_len);
        g_string_append(path, dent->d_name);

        struct stat st;
        int stat_flags = AT_SYMLINK_NOFOLLOW;
#ifdef AT_NO_AUTOMOUNT
        stat_flags |= AT_NO_AUTOMOUNT;
#endif
        if (fstatat(dir_fd, dent->d_name, &st, stat_flags)) {
            g_debug("[db_scan] can't stat: %s", path->str);
            continue;
        }

        if (walk_context->one_file_system && walk_context->root_device_id != st.st_dev) {
            g_debug("[db_scan] different filesystem, skipping: %s", path->str);
            continue;
        }

        const bool is_dir = S_ISDIR(st.st_mode);
        if (fsearch_database_exclude_manager_excludes(walk_context->exclude_manager, path->str, dent->d_name, is_dir)) {
            g_debug("[db_scan] excluded: %s", path->str);
            continue;
        }

        if (is_dir) {
            db_folder_scan_recursive(walk_context,
                                     add_folder(walk_context, dent->d_name, path->str, st.st_mtime, parent));
        }
        else {
            add_file(walk_context, dent->d_name, st.st_size, st.st_mtime, parent);
        }
    }

    g_clear_pointer(&dir, closedir);
    return WALK_OK;
}

bool
db_scan_folder(const char *path,
               FsearchDatabaseEntryFolder *parent,
               FsearchMemoryPool *folder_pool,
               FsearchMemoryPool *file_pool,
               DynamicArray *folders,
               DynamicArray *files,
               FsearchDatabaseExcludeManager *exclude_manager,
               GHashTable *handles,
               GHashTable *watch_descriptors,
               GMutex *monitor_lock,
               int32_t fanotify_fd,
               int32_t inotify_fd,
               bool one_file_system,
               GCancellable *cancellable,
               void (*status_cb)(const char *)) {
    g_return_val_if_fail(index, false);
    g_return_val_if_fail(index, false);

    g_assert(g_path_is_absolute(path));
    g_debug("[db_scan] scan path: %s", path);

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_warning("[db_scan] %s doesn't exist", path);
        return false;
    }

    g_autoptr(GString) path_string = g_string_new(path);
    // remove leading path separator '/' for root directory
    if (strcmp(path_string->str, G_DIR_SEPARATOR_S) == 0) {
        g_string_erase(path_string, 0, 1);
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    struct stat root_st;
    if (lstat(path, &root_st)) {
        g_debug("[db_scan] can't stat: %s", path);
    }

    g_autoptr(DynamicArray) watch_descriptor_array = darray_new(128);

    DatabaseWalkContext walk_context = {
        .folder_pool = folder_pool,
        .file_pool = file_pool,
        .folders = folders,
        .files = files,
        .handles = handles,
        .watch_descriptors = watch_descriptors,
        .watch_descriptor_array = watch_descriptor_array,
        .inotify_fd = inotify_fd,
        .fanotify_fd = fanotify_fd,
        .exclude_manager = exclude_manager,
        .path = path_string,
        .one_file_system = one_file_system,
        .timer = timer,
        .cancellable = cancellable,
        .status_cb = status_cb,
        .root_device_id = root_st.st_dev,
        .file_handle_payload = 0,
        .monitor_lock = monitor_lock,
    };

    if (!parent) {
        parent = add_folder(&walk_context, path, path, root_st.st_mtime, NULL);
    }
    else {
        g_autofree char *name = g_path_get_basename(path);
        FsearchDatabaseEntryFolder *folder = add_folder(&walk_context, name, path, root_st.st_mtime, parent);
        parent = folder;
    }

    uint32_t res = db_folder_scan_recursive(&walk_context, parent);

    if (res == WALK_OK) {
        return true;
    }

    // TODO: free
    if (res == WALK_CANCEL) {
        g_debug("[db_scan] scan cancelled.");
    }
    else {
        g_warning("[db_scan] walk error: %d", res);
    }
    return false;
}

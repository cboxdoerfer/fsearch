#include "fsearch_database_scan.h"

#include "fsearch_database_entry.h"
#include "fsearch_database_sort.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <sys/stat.h>

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

enum {
    WALK_OK = 0,
    WALK_BADIO,
    WALK_CANCEL,
};

typedef struct DatabaseWalkContext {
    FsearchDatabaseIndex *index;
    GString *path;
    FsearchDatabaseExcludeManager *exclude_manager;
    GTimer *timer;
    GCancellable *cancellable;
    void (*status_cb)(const char *);

    dev_t root_device_id;
} DatabaseWalkContext;

static int
db_folder_scan_recursive(DatabaseWalkContext *walk_context, FsearchDatabaseEntryFolder *parent) {
    if (walk_context->cancellable && g_cancellable_is_cancelled(walk_context->cancellable)) {
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
        if (walk_context->cancellable && g_cancellable_is_cancelled(walk_context->cancellable)) {
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

        if (fsearch_database_index_get_one_file_system(walk_context->index) && walk_context->root_device_id != st.st_dev) {
            g_debug("[db_scan] different filesystem, skipping: %s", path->str);
            continue;
        }

        const bool is_dir = S_ISDIR(st.st_mode);
        if (fsearch_database_exclude_manager_excludes(walk_context->exclude_manager, path->str, dent->d_name, is_dir)) {
            g_debug("[db_scan] excluded: %s", path->str);
            continue;
        }

        if (is_dir) {
            db_folder_scan_recursive(
                walk_context,
                fsearch_database_index_add_folder(walk_context->index, dent->d_name, path->str, st.st_mtime, parent));
        }
        else {
            fsearch_database_index_add_file(walk_context->index, dent->d_name, st.st_size, st.st_mtime, parent);
        }
    }

    g_clear_pointer(&dir, closedir);
    return WALK_OK;
}

bool
db_scan_folder(FsearchDatabaseIndex *index,
               const char *path,
               FsearchDatabaseExcludeManager *exclude_manager,
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

    DatabaseWalkContext walk_context = {
        .index = index,
        .exclude_manager = exclude_manager,
        .path = path_string,
        .timer = timer,
        .cancellable = cancellable,
        .status_cb = status_cb,
        .root_device_id = root_st.st_dev,
    };

    uint32_t res =
        db_folder_scan_recursive(&walk_context,
                                 fsearch_database_index_add_folder(index, path_string->str, path_string->str, 0, NULL));

    if (res == WALK_OK) {
        // g_debug("[db_scan] scanned: %d files, %d folders -> %d total",
        //         db_get_num_files(db),
        //         db_get_num_folders(db),
        //         db_get_num_entries(db));
        return true;
    }

    if (res == WALK_CANCEL) {
        g_debug("[db_scan] scan cancelled.");
    }
    else {
        g_warning("[db_scan] walk error: %d", res);
    }
    return false;
}

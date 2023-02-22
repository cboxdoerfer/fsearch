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

static bool
is_cancelled(GCancellable *cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return true;
    }
    return false;
}

typedef struct DatabaseWalkContext {
    FsearchDatabaseIndex *index;
    GString *path;
    FsearchDatabaseInclude *include;
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

        if (fsearch_database_include_get_one_file_system(walk_context->include)
            && walk_context->root_device_id != st.st_dev) {
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
                fsearch_database_index_add_folder(walk_context->index, dent->d_name, st.st_mtime, parent));
        }
        else {
            fsearch_database_index_add_file(walk_context->index, dent->d_name, st.st_size, st.st_mtime, parent);
        }
    }

    g_clear_pointer(&dir, closedir);
    return WALK_OK;
}

static FsearchDatabaseIndex *
db_scan_folder(FsearchDatabaseInclude *include,
               FsearchDatabaseExcludeManager *exclude_manager,
               FsearchDatabaseIndexPropertyFlags flags,
               GCancellable *cancellable,
               void (*status_cb)(const char *)) {
    const char *directory_path = fsearch_database_include_get_path(include);
    g_assert(g_path_is_absolute(directory_path));
    g_debug("[db_scan] scan path: %s", directory_path);

    if (!g_file_test(directory_path, G_FILE_TEST_IS_DIR)) {
        g_warning("[db_scan] %s doesn't exist", directory_path);
        return NULL;
    }

    g_autoptr(GString) path = g_string_new(directory_path);
    // remove leading path separator '/' for root directory
    if (strcmp(path->str, G_DIR_SEPARATOR_S) == 0) {
        g_string_erase(path, 0, 1);
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    struct stat root_st;
    if (lstat(directory_path, &root_st)) {
        g_debug("[db_scan] can't stat: %s", directory_path);
    }

    g_autoptr(FsearchDatabaseIndex) index =
        fsearch_database_index_new(fsearch_database_include_get_id(include), include, exclude_manager, flags, NULL, NULL);

    DatabaseWalkContext walk_context = {
        .index = index,
        .include = include,
        .exclude_manager = exclude_manager,
        .path = path,
        .timer = timer,
        .cancellable = cancellable,
        .status_cb = status_cb,
        .root_device_id = root_st.st_dev,
    };

    uint32_t res = db_folder_scan_recursive(&walk_context, fsearch_database_index_add_folder(index, path->str, 0, NULL));

    if (res == WALK_OK) {
        // g_debug("[db_scan] scanned: %d files, %d folders -> %d total",
        //         db_get_num_files(db),
        //         db_get_num_folders(db),
        //         db_get_num_entries(db));
        return g_steal_pointer(&index);
    }

    if (res == WALK_CANCEL) {
        g_debug("[db_scan] scan cancelled.");
    }
    else {
        g_warning("[db_scan] walk error: %d", res);
    }
    return NULL;
}

FsearchDatabaseIndexStore *
db_scan2(FsearchDatabaseIncludeManager *include_manager,
         FsearchDatabaseExcludeManager *exclude_manager,
         FsearchDatabaseIndexPropertyFlags flags,
         GCancellable *cancellable) {
    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(flags);

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        g_autoptr(FsearchDatabaseIndex) index = db_scan_folder(include, exclude_manager, flags, cancellable, NULL);
        if (index) {
            fsearch_database_index_store_add(store, index);
        }
    }
    if (is_cancelled(cancellable)) {
        return NULL;
    }
    // if (status_cb) {
    //     status_cb(_("Sorting…"));
    // }
    fsearch_database_index_store_sort(store, cancellable);

    if (is_cancelled(cancellable)) {
        return NULL;
    }

    return g_steal_pointer(&store);
}

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

    FsearchDatabaseIndex *index = walk_context->index;

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
            FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(index->folder_pool);
            db_entry_set_name(entry, dent->d_name);
            db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
            db_entry_set_mtime(entry, st.st_mtime);
            db_entry_set_parent(entry, parent);

            darray_add_item(index->folders[DATABASE_INDEX_PROPERTY_NAME], entry);

            db_folder_scan_recursive(walk_context, (FsearchDatabaseEntryFolder *)entry);
        }
        else {
            FsearchDatabaseEntry *file_entry = fsearch_memory_pool_malloc(index->file_pool);
            db_entry_set_name(file_entry, dent->d_name);
            db_entry_set_size(file_entry, st.st_size);
            db_entry_set_mtime(file_entry, st.st_mtime);
            db_entry_set_type(file_entry, DATABASE_ENTRY_TYPE_FILE);
            db_entry_set_parent(file_entry, parent);
            db_entry_update_parent_size(file_entry);

            darray_add_item(index->files[DATABASE_INDEX_PROPERTY_NAME], file_entry);
        }
    }

    g_clear_pointer(&dir, closedir);
    return WALK_OK;
}

static bool
db_scan_folder(FsearchDatabaseIndex *index,
               FsearchDatabaseInclude *include,
               FsearchDatabaseExcludeManager *exclude_manager,
               GCancellable *cancellable,
               void (*status_cb)(const char *)) {
    const char *directory_path = fsearch_database_include_get_path(include);
    g_assert(g_path_is_absolute(directory_path));
    g_debug("[db_scan] scan path: %s", directory_path);

    if (!g_file_test(directory_path, G_FILE_TEST_IS_DIR)) {
        g_warning("[db_scan] %s doesn't exist", directory_path);
        return false;
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

    FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(index->folder_pool);
    db_entry_set_name(entry, path->str);
    db_entry_set_parent(entry, NULL);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);

    darray_add_item(index->folders[DATABASE_INDEX_PROPERTY_NAME], entry);

    uint32_t res = db_folder_scan_recursive(&walk_context, (FsearchDatabaseEntryFolder *)entry);

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

FsearchDatabaseIndex *
db_scan2(FsearchDatabaseIncludeManager *include_manager,
         FsearchDatabaseExcludeManager *exclude_manager,
         FsearchDatabaseIndexPropertyFlags flags,
         GCancellable *cancellable) {
    FsearchDatabaseIndex *index = calloc(1, sizeof(FsearchDatabaseIndex));
    g_assert(index);

    index->flags = flags;
    index->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                               db_entry_get_sizeof_file_entry(),
                                               (GDestroyNotify)db_entry_destroy);
    index->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                 db_entry_get_sizeof_folder_entry(),
                                                 (GDestroyNotify)db_entry_destroy);
    index->files[DATABASE_INDEX_PROPERTY_NAME] = darray_new(1024);
    index->folders[DATABASE_INDEX_PROPERTY_NAME] = darray_new(1024);

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        db_scan_folder(index, include, exclude_manager, cancellable, NULL);
    }
    if (is_cancelled(cancellable)) {
        goto cancelled;
    }
    // if (status_cb) {
    //     status_cb(_("Sorting…"));
    // }
    fsearch_database_sort(index->files, index->folders, index->flags, cancellable);
    if (is_cancelled(cancellable)) {
        goto cancelled;
    }

    return index;

cancelled:
    g_clear_pointer(&index, fsearch_database_index_free);
    return NULL;
}

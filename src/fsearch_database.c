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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define G_LOG_DOMAIN "fsearch-database"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glib/gi18n.h>

#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "fsearch_database.h"
#include "fsearch_database2.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_file.h"
#include "fsearch_database_view2.h"
#include "fsearch_exclude_path.h"
#include "fsearch_index.h"
#include "fsearch_memory_pool.h"
#include "fsearch_task.h"

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

#define DATABASE_MAJOR_VERSION 0
#define DATABASE_MINOR_VERSION 9
#define DATABASE_MAGIC_NUMBER "FSDB"

struct FsearchDatabase {
    DynamicArray *sorted_files[NUM_DATABASE_INDEX_TYPES];
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_TYPES];

    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;

    GList *db_views;
    FsearchThreadPool *thread_pool;

    FsearchDatabaseIndexFlags index_flags;

    GList *indexes;
    GList *excludes;
    char **exclude_files;

    bool exclude_hidden;
    time_t timestamp;

    volatile int ref_count;

    GMutex mutex;
};

enum {
    WALK_OK = 0,
    WALK_BADIO,
    WALK_CANCEL,
};

bool
db_register_view(FsearchDatabase *db, gpointer view) {
    if (g_list_find(db->db_views, view)) {
        g_debug("[db_register_view] view is already registered for database");
        return false;
    }
    db->db_views = g_list_append(db->db_views, view);
    return true;
}

bool
db_unregister_view(FsearchDatabase *db, gpointer view) {
    if (!g_list_find(db->db_views, view)) {
        g_debug("[db_unregister_view] view isn't registered for database");
        return false;
    }
    db->db_views = g_list_remove(db->db_views, view);
    return true;
}

static void
db_sorted_entries_free(FsearchDatabase *db) {
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        g_clear_pointer(&db->sorted_files[i], darray_unref);
        g_clear_pointer(&db->sorted_folders[i], darray_unref);
    }
}

static bool
is_cancelled(GCancellable *cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return true;
    }
    return false;
}

static void
db_sort_entries(FsearchDatabase *db, DynamicArray *entries, DynamicArray **sorted_entries, GCancellable *cancellable) {
    // first sort by path
    darray_sort_multi_threaded(entries, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path, cancellable, NULL);
    if (is_cancelled(cancellable)) {
        return;
    }
    sorted_entries[DATABASE_INDEX_TYPE_PATH] = darray_copy(entries);

    // then by name
    darray_sort(entries, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_name, cancellable, NULL);
    if (is_cancelled(cancellable)) {
        return;
    }

    // now build individual lists sorted by all of the indexed metadata
    if ((db->index_flags & DATABASE_INDEX_FLAG_SIZE) != 0) {
        sorted_entries[DATABASE_INDEX_TYPE_SIZE] = darray_copy(entries);
        darray_sort_multi_threaded(sorted_entries[DATABASE_INDEX_TYPE_SIZE],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_size,
                                   cancellable,
                                   NULL);
        if (is_cancelled(cancellable)) {
            return;
        }
    }

    if ((db->index_flags & DATABASE_INDEX_FLAG_MODIFICATION_TIME) != 0) {
        sorted_entries[DATABASE_INDEX_TYPE_MODIFICATION_TIME] = darray_copy(entries);
        darray_sort_multi_threaded(sorted_entries[DATABASE_INDEX_TYPE_MODIFICATION_TIME],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_modification_time,
                                   cancellable,
                                   NULL);
        if (is_cancelled(cancellable)) {
            return;
        }
    }
}

static void
db_sort(FsearchDatabase *db, GCancellable *cancellable) {
    g_assert(db);

    g_autoptr(GTimer) timer = g_timer_new();

    // first we sort all the files
    DynamicArray *files = db->sorted_files[DATABASE_INDEX_TYPE_NAME];
    if (files) {
        db_sort_entries(db, files, db->sorted_files, cancellable);
        if (is_cancelled(cancellable)) {
            return;
        }

        // now build extension sort array
        db->sorted_files[DATABASE_INDEX_TYPE_EXTENSION] = darray_copy(files);
        darray_sort_multi_threaded(db->sorted_files[DATABASE_INDEX_TYPE_EXTENSION],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_extension,
                                   cancellable,
                                   NULL);
        if (is_cancelled(cancellable)) {
            return;
        }

        const double seconds = g_timer_elapsed(timer, NULL);
        g_timer_reset(timer);
        g_debug("[db_sort] sorted files: %f s", seconds);
    }

    // then we sort all the folders
    DynamicArray *folders = db->sorted_folders[DATABASE_INDEX_TYPE_NAME];
    if (folders) {
        db_sort_entries(db, folders, db->sorted_folders, cancellable);
        if (is_cancelled(cancellable)) {
            return;
        }

        // Folders don't have a file extension -> use the name array instead
        db->sorted_folders[DATABASE_INDEX_TYPE_EXTENSION] = darray_ref(folders);

        const double seconds = g_timer_elapsed(timer, NULL);
        g_debug("[db_sort] sorted folders: %f s", seconds);
    }
}

static void
db_update_timestamp(FsearchDatabase *db) {
    g_assert(db);
    db->timestamp = time(NULL);
}

bool
db_load(FsearchDatabase *db, const char *file_path, void (*status_cb)(const char *)) {
    g_assert(file_path);
    g_assert(db);

    g_autofree FsearchDatabaseIndex *index = db_file_load(file_path, status_cb);

    if (!index) {
        return false;
    }
    db_sorted_entries_free(db);
    g_clear_pointer(&db->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&db->folder_pool, fsearch_memory_pool_free_pool);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        db->sorted_files[i] = g_steal_pointer(&index->files[i]);
        db->sorted_folders[i] = g_steal_pointer(&index->folders[i]);
    }
    db->file_pool = g_steal_pointer(&index->file_pool);
    db->folder_pool = g_steal_pointer(&index->folder_pool);

    db->index_flags = index->flags;
    return true;
}

bool
db_save(FsearchDatabase *db, const char *path) {
    g_assert(path);
    g_assert(db);

    g_autofree FsearchDatabaseIndex *index = calloc(1, sizeof(FsearchDatabaseIndex));
    g_assert(index);
    index->flags = db->index_flags;
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        index->files[i] = db->sorted_files[i];
        index->folders[i] = db->sorted_folders[i];
    }
    index->file_pool = db->file_pool;
    index->folder_pool = db->folder_pool;

    return db_file_save(index, path);
}

static bool
file_is_excluded(const char *name, char **exclude_files) {
    if (exclude_files) {
        for (int i = 0; exclude_files[i]; ++i) {
            if (!fnmatch(exclude_files[i], name, 0)) {
                return true;
            }
        }
    }
    return false;
}

static bool
directory_is_excluded(const char *name, GList *excludes) {
    while (excludes) {
        FsearchExcludePath *fs_path = excludes->data;
        if (!strcmp(name, fs_path->path)) {
            if (fs_path->enabled) {
                return true;
            }
            return false;
        }
        excludes = excludes->next;
    }
    return false;
}

typedef struct DatabaseWalkContext {
    FsearchDatabase *db;
    GString *path;
    GTimer *timer;
    GCancellable *cancellable;
    void (*status_cb)(const char *);

    dev_t root_device_id;
    bool one_filesystem;
    bool exclude_hidden;
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

    FsearchDatabase *db = walk_context->db;

    struct dirent *dent = NULL;
    while ((dent = readdir(dir))) {
        if (walk_context->cancellable && g_cancellable_is_cancelled(walk_context->cancellable)) {
            g_debug("[db_scan] cancelled");
            g_clear_pointer(&dir, closedir);
            return WALK_CANCEL;
        }
        if (walk_context->exclude_hidden && dent->d_name[0] == '.') {
            // file is dotfile, skip
            // g_debug("[db_scan] exclude hidden: %s", dent->d_name);
            continue;
        }
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
            continue;
        }
        if (file_is_excluded(dent->d_name, db->exclude_files)) {
            // g_debug("[db_scan] excluded: %s", dent->d_name);
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

        if (walk_context->one_filesystem && walk_context->root_device_id != st.st_dev) {
            g_debug("[db_scan] different filesystem, skipping: %s", path->str);
            continue;
        }

        const bool is_dir = S_ISDIR(st.st_mode);
        if (is_dir && directory_is_excluded(path->str, db->excludes)) {
            g_debug("[db_scan] excluded directory: %s", path->str);
            continue;
        }

        if (is_dir) {
            FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(db->folder_pool);
            db_entry_set_name(entry, dent->d_name);
            db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
            db_entry_set_mtime(entry, st.st_mtime);
            db_entry_set_parent(entry, parent);

            darray_add_item(db->sorted_folders[DATABASE_INDEX_TYPE_NAME], entry);

            db_folder_scan_recursive(walk_context, (FsearchDatabaseEntryFolder *)entry);
        }
        else {
            FsearchDatabaseEntry *file_entry = fsearch_memory_pool_malloc(db->file_pool);
            db_entry_set_name(file_entry, dent->d_name);
            db_entry_set_size(file_entry, st.st_size);
            db_entry_set_mtime(file_entry, st.st_mtime);
            db_entry_set_type(file_entry, DATABASE_ENTRY_TYPE_FILE);
            db_entry_set_parent(file_entry, parent);
            db_entry_update_parent_size(file_entry);

            darray_add_item(db->sorted_files[DATABASE_INDEX_TYPE_NAME], file_entry);
        }
    }

    g_clear_pointer(&dir, closedir);
    return WALK_OK;
}

static bool
db_scan_folder(FsearchDatabase *db,
               const char *dname,
               bool one_filesystem,
               GCancellable *cancellable,
               void (*status_cb)(const char *)) {
    g_assert(dname);
    g_assert(dname[0] == G_DIR_SEPARATOR);
    g_debug("[db_scan] scan path: %s", dname);

    if (!g_file_test(dname, G_FILE_TEST_IS_DIR)) {
        g_warning("[db_scan] %s doesn't exist", dname);
        return false;
    }

    g_autoptr(GString) path = g_string_new(dname);
    // remove leading path separator '/' for root directory
    if (strcmp(path->str, G_DIR_SEPARATOR_S) == 0) {
        g_string_erase(path, 0, 1);
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    struct stat root_st;
    if (lstat(dname, &root_st)) {
        g_debug("[db_scan] can't stat: %s", dname);
    }

    DatabaseWalkContext walk_context = {
        .db = db,
        .path = path,
        .timer = timer,
        .cancellable = cancellable,
        .status_cb = status_cb,
        .root_device_id = root_st.st_dev,
        .one_filesystem = one_filesystem,
        .exclude_hidden = db->exclude_hidden,
    };

    FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(db->folder_pool);
    db_entry_set_name(entry, path->str);
    db_entry_set_parent(entry, NULL);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);

    darray_add_item(db->sorted_folders[DATABASE_INDEX_TYPE_NAME], entry);

    uint32_t res = db_folder_scan_recursive(&walk_context, (FsearchDatabaseEntryFolder *)entry);

    if (res == WALK_OK) {
        g_debug("[db_scan] scanned: %d files, %d folders -> %d total",
                db_get_num_files(db),
                db_get_num_folders(db),
                db_get_num_entries(db));
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

static gint
compare_index_path(FsearchIndex *p1, FsearchIndex *p2) {
    return strcmp(p1->path, p2->path);
}

static gint
compare_exclude_path(FsearchExcludePath *p1, FsearchExcludePath *p2) {
    return strcmp(p1->path, p2->path);
}

static void
on_load_started(FsearchDatabase2 *db, gpointer data, gpointer user_data) {
    g_print("signal: load_started\n");
}

static void
on_load_finished(FsearchDatabase2 *db, gpointer data, gpointer user_data) {
    g_print("signal: load_finished\n");
}

FsearchDatabase *
db_new(GList *indexes, GList *excludes, char **exclude_files, bool exclude_hidden) {
    g_autoptr(FsearchDatabase2) db2 = fsearch_database2_new(NULL);
    //FsearchDatabase2 *db2 = fsearch_database2_new(NULL);
    g_autoptr(FsearchDatabaseView2) view2 = fsearch_database_view2_new(G_OBJECT(db2));
    //FsearchDatabaseView2 *view2 = fsearch_database_view2_new(G_OBJECT(db2));
    g_signal_connect(db2, "load-started", G_CALLBACK(on_load_started), NULL);
    g_signal_connect(db2, "load-finished", G_CALLBACK(on_load_finished), NULL);

    FsearchDatabase *db = g_new0(FsearchDatabase, 1);
    g_assert(db);
    g_mutex_init(&db->mutex);
    if (indexes) {
        db->indexes = g_list_copy_deep(indexes, (GCopyFunc)fsearch_index_copy, NULL);

        db->indexes = g_list_sort(db->indexes, (GCompareFunc)compare_index_path);
    }
    if (excludes) {
        db->excludes = g_list_copy_deep(excludes, (GCopyFunc)fsearch_exclude_path_copy, NULL);
        db->excludes = g_list_sort(db->excludes, (GCompareFunc)compare_exclude_path);
    }
    if (exclude_files) {
        db->exclude_files = g_strdupv(exclude_files);
    }

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        db->sorted_files[i] = NULL;
        db->sorted_folders[i] = NULL;
    }
    db->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                            db_entry_get_sizeof_file_entry(),
                                            (GDestroyNotify)db_entry_destroy);
    db->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_folder_entry(),
                                              (GDestroyNotify)db_entry_destroy);

    db->thread_pool = fsearch_thread_pool_init();

    db->exclude_hidden = exclude_hidden;
    db->ref_count = 1;
    return db;
}

static void
db_free(FsearchDatabase *db) {
    g_assert(db);

    g_debug("[db_free] freeing...");
    db_lock(db);
    if (db->ref_count > 0) {
        g_warning("[db_free] pending references on free: %d", db->ref_count);
    }

    db_sorted_entries_free(db);

    g_clear_pointer(&db->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&db->folder_pool, fsearch_memory_pool_free_pool);

    if (db->indexes) {
        g_list_free_full(g_steal_pointer(&db->indexes), (GDestroyNotify)fsearch_index_free);
    }
    if (db->excludes) {
        g_list_free_full(g_steal_pointer(&db->excludes), (GDestroyNotify)fsearch_exclude_path_free);
    }

    g_clear_pointer(&db->exclude_files, g_strfreev);
    g_clear_pointer(&db->thread_pool, fsearch_thread_pool_free);

    db_unlock(db);

    g_mutex_clear(&db->mutex);

    g_clear_pointer(&db, free);

#ifdef HAVE_MALLOC_TRIM
    malloc_trim(0);
#endif

    g_debug("[db_free] freed");
}

time_t
db_get_timestamp(FsearchDatabase *db) {
    g_assert(db);
    return db->timestamp;
}

uint32_t
db_get_num_files(FsearchDatabase *db) {
    g_assert(db);
    return db->sorted_files[DATABASE_INDEX_TYPE_NAME] ? darray_get_num_items(db->sorted_files[DATABASE_INDEX_TYPE_NAME])
                                                      : 0;
}

uint32_t
db_get_num_folders(FsearchDatabase *db) {
    g_assert(db);
    return db->sorted_folders[DATABASE_INDEX_TYPE_NAME]
             ? darray_get_num_items(db->sorted_folders[DATABASE_INDEX_TYPE_NAME])
             : 0;
}

uint32_t
db_get_num_entries(FsearchDatabase *db) {
    g_assert(db);
    return db_get_num_files(db) + db_get_num_folders(db);
}

void
db_unlock(FsearchDatabase *db) {
    g_assert(db);
    g_mutex_unlock(&db->mutex);
}

void
db_lock(FsearchDatabase *db) {
    g_assert(db);
    g_mutex_lock(&db->mutex);
}

bool
db_try_lock(FsearchDatabase *db) {
    g_assert(db);
    return g_mutex_trylock(&db->mutex);
}

static bool
is_valid_sort_type(FsearchDatabaseIndexType sort_type) {
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_TYPES) {
        return true;
    }
    return false;
}

bool
db_has_entries_sorted_by_type(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    g_assert(db);

    if (is_valid_sort_type(sort_type)) {
        return db->sorted_folders[sort_type] ? true : false;
    }
    return false;
}

DynamicArray *
db_get_folders_sorted_copy(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    g_assert(db);
    if (!is_valid_sort_type(sort_type)) {
        return NULL;
    }
    DynamicArray *folders = db->sorted_folders[sort_type];
    return folders ? darray_copy(folders) : NULL;
}

DynamicArray *
db_get_files_sorted_copy(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    g_assert(db);
    if (!is_valid_sort_type(sort_type)) {
        return NULL;
    }
    DynamicArray *files = db->sorted_files[sort_type];
    return files ? darray_copy(files) : NULL;
}

DynamicArray *
db_get_folders_copy(FsearchDatabase *db) {
    return db_get_folders_sorted_copy(db, DATABASE_INDEX_TYPE_NAME);
}

DynamicArray *
db_get_files_copy(FsearchDatabase *db) {
    return db_get_files_sorted_copy(db, DATABASE_INDEX_TYPE_NAME);
}

bool
db_get_entries_sorted(FsearchDatabase *db,
                      FsearchDatabaseIndexType requested_sort_type,
                      FsearchDatabaseIndexType *returned_sort_type,
                      DynamicArray **folders,
                      DynamicArray **files) {
    g_assert(db);
    g_assert(returned_sort_type);
    g_assert(folders);
    g_assert(files);
    if (!is_valid_sort_type(requested_sort_type)) {
        return false;
    }

    FsearchDatabaseIndexType sort_type = requested_sort_type;
    if (!db_has_entries_sorted_by_type(db, requested_sort_type)) {
        sort_type = DATABASE_INDEX_TYPE_NAME;
    }

    if (!db_has_entries_sorted_by_type(db, sort_type)) {
        return false;
    }

    *folders = darray_ref(db->sorted_folders[sort_type]);
    *files = darray_ref(db->sorted_files[sort_type]);
    *returned_sort_type = sort_type;
    return true;
}

DynamicArray *
db_get_folders_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    g_assert(db);
    if (!is_valid_sort_type(sort_type)) {
        return NULL;
    }

    DynamicArray *folders = db->sorted_folders[sort_type];
    return darray_ref(folders);
}

DynamicArray *
db_get_files_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    g_assert(db);
    if (!is_valid_sort_type(sort_type)) {
        return NULL;
    }

    DynamicArray *files = db->sorted_files[sort_type];
    return darray_ref(files);
}

DynamicArray *
db_get_files(FsearchDatabase *db) {
    g_assert(db);
    return db_get_files_sorted(db, DATABASE_INDEX_TYPE_NAME);
}

DynamicArray *
db_get_folders(FsearchDatabase *db) {
    g_assert(db);
    return db_get_folders_sorted(db, DATABASE_INDEX_TYPE_NAME);
}

FsearchThreadPool *
db_get_thread_pool(FsearchDatabase *db) {
    g_assert(db);
    return db->thread_pool;
}

bool
db_scan(FsearchDatabase *db, GCancellable *cancellable, void (*status_cb)(const char *)) {
    g_assert(db);

    bool ret = false;

    db_sorted_entries_free(db);

    db->index_flags |= DATABASE_INDEX_FLAG_NAME;
    db->index_flags |= DATABASE_INDEX_FLAG_SIZE;
    db->index_flags |= DATABASE_INDEX_FLAG_MODIFICATION_TIME;

    db->sorted_files[DATABASE_INDEX_TYPE_NAME] = darray_new(1024);
    db->sorted_folders[DATABASE_INDEX_TYPE_NAME] = darray_new(1024);

    for (GList *l = db->indexes; l != NULL; l = l->next) {
        FsearchIndex *fs_path = l->data;
        if (!fs_path->path) {
            continue;
        }
        if (!fs_path->enabled) {
            continue;
        }
        if (fs_path->update) {
            ret = db_scan_folder(db, fs_path->path, fs_path->one_filesystem, cancellable, status_cb) || ret;
        }
        if (is_cancelled(cancellable)) {
            return false;
        }
    }
    if (status_cb) {
        status_cb(_("Sorting…"));
    }
    db_sort(db, cancellable);
    if (is_cancelled(cancellable)) {
        return false;
    }
    return ret;
}

FsearchDatabase *
db_ref(FsearchDatabase *db) {
    if (!db || g_atomic_int_get(&db->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&db->ref_count);
    g_debug("[db_ref] increased to: %d", db->ref_count);
    return db;
}

void
db_unref(FsearchDatabase *db) {
    if (!db || g_atomic_int_get(&db->ref_count) <= 0) {
        return;
    }
    g_debug("[db_unref] dropped to: %d", db->ref_count - 1);
    if (g_atomic_int_dec_and_test(&db->ref_count)) {
        g_clear_pointer(&db, db_free);
    }
}

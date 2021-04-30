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

#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <glib/gi18n.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "fsearch_database.h"
#include "fsearch_exclude_path.h"
#include "fsearch_include_path.h"
#include "fsearch_memory_pool.h"

#define BTREE_NODE_POOL_BLOCK_ELEMENTS 10000

struct FsearchDatabaseEntryCommon {
    FsearchDatabaseEntryFolder *parent;
    char *name;
    off_t size;

    uint8_t type;
};

struct _FsearchDatabaseEntryFile {
    struct FsearchDatabaseEntryCommon shared;
};

struct _FsearchDatabaseEntryFolder {
    struct FsearchDatabaseEntryCommon shared;

    GSList *folder_children;
    GSList *file_children;
};

struct _FsearchDatabase {
    DynamicArray *files;
    DynamicArray *folders;

    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;

    uint32_t num_entries;
    uint32_t num_folders;
    uint32_t num_files;

    GList *includes;
    GList *excludes;
    char **exclude_files;

    bool exclude_hidden;
    time_t timestamp;

    int32_t ref_count;
    GMutex mutex;
};

enum {
    WALK_OK = 0,
    WALK_BADIO,
    WALK_CANCEL,
};

// Implementation

static void
db_file_entry_destroy(FsearchDatabaseEntryFolder *entry) {
    if (!entry) {
        return;
    }
    if (entry->shared.name) {
        free(entry->shared.name);
        entry->shared.name = NULL;
    }
}

static void
db_folder_entry_destroy(FsearchDatabaseEntryFolder *entry) {
    if (!entry) {
        return;
    }
    if (entry->shared.name) {
        free(entry->shared.name);
        entry->shared.name = NULL;
    }
    if (entry->file_children) {
        g_slist_free(entry->file_children);
        entry->file_children = NULL;
    }
    if (entry->folder_children) {
        g_slist_free(entry->folder_children);
        entry->folder_children = NULL;
    }
}

static uint32_t
db_entry_get_depth(FsearchDatabaseEntry *entry) {
    uint32_t depth = 0;
    while (entry && entry->shared.parent) {
        entry = (FsearchDatabaseEntry *)entry->shared.parent;
        depth++;
    }
    return depth;
}

static FsearchDatabaseEntryFolder *
db_entry_get_parent_nth(FsearchDatabaseEntryFolder *entry, int32_t nth) {
    while (entry && nth > 0) {
        entry = entry->shared.parent;
        nth--;
    }
    return entry;
}

static void
sort_entry_by_path_recursive(FsearchDatabaseEntryFolder *entry_a, FsearchDatabaseEntryFolder *entry_b, int *res) {
    if (!entry_a) {
        return;
    }
    if (entry_a->shared.parent && entry_a->shared.parent != entry_b->shared.parent) {
        sort_entry_by_path_recursive(entry_a->shared.parent, entry_b->shared.parent, res);
    }
    if (*res != 0) {
        return;
    }
    *res = strverscmp(entry_a->shared.name, entry_b->shared.name);
}

static int
sort_entry_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    FsearchDatabaseEntry *entry_a = *a;
    FsearchDatabaseEntry *entry_b = *b;
    uint32_t a_depth = db_entry_get_depth(entry_a);
    uint32_t b_depth = db_entry_get_depth(entry_b);

    int res = 0;
    if (a_depth == b_depth) {
        sort_entry_by_path_recursive(entry_a->shared.parent, entry_b->shared.parent, &res);
    }
    else if (a_depth > b_depth) {
        int32_t diff = a_depth - b_depth;
        FsearchDatabaseEntryFolder *parent_a = db_entry_get_parent_nth(entry_a->shared.parent, diff);
        sort_entry_by_path_recursive(parent_a, entry_b->shared.parent, &res);
        res = res == 0 ? 1 : res;
    }
    else {
        int32_t diff = b_depth - a_depth;
        FsearchDatabaseEntryFolder *parent_b = db_entry_get_parent_nth(entry_b->shared.parent, diff);
        sort_entry_by_path_recursive(entry_a->shared.parent, parent_b, &res);
        res = res == 0 ? -1 : res;
    }
    return res;
}

static int
sort_entry_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return strverscmp((*a)->shared.name, (*b)->shared.name);
}

static void
db_sort(FsearchDatabase *db) {
    assert(db != NULL);

    GTimer *timer = g_timer_new();
    if (db->files) {
        darray_sort_multi_threaded(db->files, (DynamicArrayCompareFunc)sort_entry_by_path);
        darray_sort(db->files, (DynamicArrayCompareFunc)sort_entry_by_name);
        const double seconds = g_timer_elapsed(timer, NULL);
        g_timer_reset(timer);
        g_debug("[database] sorted files: %f s", seconds);
    }
    if (db->folders) {
        darray_sort_multi_threaded(db->folders, (DynamicArrayCompareFunc)sort_entry_by_path);
        darray_sort(db->folders, (DynamicArrayCompareFunc)sort_entry_by_name);
        const double seconds = g_timer_elapsed(timer, NULL);
        g_debug("[database] sorted folders: %f s", seconds);
    }
    g_timer_destroy(timer);
    timer = NULL;
}

static void
db_update_timestamp(FsearchDatabase *db) {
    assert(db != NULL);
    db->timestamp = time(NULL);
}

// static FsearchDatabaseNode *
// db_location_load_from_file(const char *fname) {
//     assert(fname != NULL);
//
//     FILE *fp = fopen(fname, "rb");
//     if (!fp) {
//         return NULL;
//     }
//
//     DatabaseEntry *root = NULL;
//     FsearchDatabaseNode *location = db_location_new();
//
//     char magic[4];
//     if (fread(magic, 4, 1, fp) != 1) {
//         trace("[database_read_file] failed to read magic\n");
//         goto load_fail;
//     }
//     if (strncmp(magic, "FSDB", 4)) {
//         trace("[database_read_file] bad signature\n");
//         goto load_fail;
//     }
//
//     uint8_t majorver = 0;
//     if (fread(&majorver, 1, 1, fp) != 1) {
//         goto load_fail;
//     }
//     if (majorver != 0) {
//         trace("[database_read_file] bad majorver=%d\n", majorver);
//         goto load_fail;
//     }
//
//     uint8_t minorver = 0;
//     if (fread(&minorver, 1, 1, fp) != 1) {
//         goto load_fail;
//     }
//     if (minorver != 1) {
//         trace("[database_read_file] bad minorver=%d\n", minorver);
//         goto load_fail;
//     }
//     trace("[database_read_file] database version=%d.%d\n", majorver, minorver);
//
//     uint32_t num_items = 0;
//     if (fread(&num_items, 4, 1, fp) != 1) {
//         goto load_fail;
//     }
//
//     uint32_t num_folders = 0;
//     uint32_t num_files = 0;
//
//     uint32_t num_items_read = 0;
//     DatabaseEntry *prev = NULL;
//     while (true) {
//         uint16_t name_len = 0;
//         if (fread(&name_len, 2, 1, fp) != 1) {
//             trace("[database_read_file] failed to read name length\n");
//             goto load_fail;
//         }
//
//         if (name_len == 0) {
//             // reached end of child marker
//             if (!prev) {
//                 goto load_fail;
//             }
//             prev = prev->parent;
//             if (!prev) {
//                 // prev was root node, we're done
//                 trace("[database_read_file] reached root node. done\n");
//                 break;
//             }
//             continue;
//         }
//
//         // read name
//         char name[name_len + 1];
//         if (fread(&name, name_len, 1, fp) != 1) {
//             trace("[database_read_file] failed to read name\n");
//             goto load_fail;
//         }
//         name[name_len] = '\0';
//
//         // read is_dir
//         uint8_t is_dir = 0;
//         if (fread(&is_dir, 1, 1, fp) != 1) {
//             trace("[database_read_file] failed to read is_dir\n");
//             goto load_fail;
//         }
//
//         // read size
//         uint64_t size = 0;
//         if (fread(&size, 8, 1, fp) != 1) {
//             trace("[database_read_file] failed to read size\n");
//             goto load_fail;
//         }
//
//         // read mtime
//         uint64_t mtime = 0;
//         if (fread(&mtime, 8, 1, fp) != 1) {
//             trace("[database_read_file] failed to read mtime\n");
//             goto load_fail;
//         }
//
//         // read sort position
//         uint32_t pos = 0;
//         if (fread(&pos, 4, 1, fp) != 1) {
//             trace("[database_read_file] failed to read sort position\n");
//             goto load_fail;
//         }
//
//         int is_root = !strcmp(name, "/");
//         DatabaseEntry *new = fsearch_memory_pool_malloc(location->pool);
//         new->name = is_root ? strdup("/") : strdup(name);
//         new->mtime = mtime;
//         new->size = size;
//         new->is_dir = is_dir;
//         new->pos = pos;
//
//         is_dir ? num_folders++ : num_files++;
//         num_items_read++;
//
//         if (!prev) {
//             prev = new;
//             root = new;
//             continue;
//         }
//         prev = btree_node_prepend(prev, new);
//     }
//     trace("[database_load] finished with %d of %d items successfully read\n", num_items_read, num_items);
//
//     location->num_items = num_items_read;
//     location->num_folders = num_folders;
//     location->num_files = num_files;
//     location->entries = root;
//
//     fclose(fp);
//
//     return location;
//
// load_fail:
//     fprintf(stderr, "database load fail (%s)!\n", fname);
//     if (fp) {
//         fclose(fp);
//     }
//     db_location_free(location);
//     return NULL;
// }

// static bool
// db_location_write_to_file(FsearchDatabaseNode *location, const char *path) {
//     assert(path != NULL);
//     assert(location != NULL);
//
//     if (!location->entries) {
//         return false;
//     }
//     g_mkdir_with_parents(path, 0700);
//
//     GString *db_path = g_string_new(path);
//     g_string_append(db_path, "/database.db");
//
//     FILE *fp = fopen(db_path->str, "w+b");
//     if (!fp) {
//         return false;
//     }
//
//     const char magic[] = "FSDB";
//     if (fwrite(magic, 4, 1, fp) != 1) {
//         goto save_fail;
//     }
//
//     const uint8_t majorver = 0;
//     if (fwrite(&majorver, 1, 1, fp) != 1) {
//         goto save_fail;
//     }
//
//     const uint8_t minorver = 1;
//     if (fwrite(&minorver, 1, 1, fp) != 1) {
//         goto save_fail;
//     }
//
//     uint32_t num_items = btree_node_n_nodes(location->entries);
//     if (fwrite(&num_items, 4, 1, fp) != 1) {
//         goto save_fail;
//     }
//
//     const uint16_t del = 0;
//
//     DatabaseEntry *root = location->entries;
//     DatabaseEntry *node = root;
//     uint32_t is_root = !strcmp(root->name, "");
//
//     while (node) {
//         const char *name = is_root ? "/" : node->name;
//         is_root = 0;
//         uint16_t len = strlen(name);
//         if (len) {
//             // write length of node name
//             if (fwrite(&len, 2, 1, fp) != 1) {
//                 goto save_fail;
//             }
//             // write node name
//             if (fwrite(name, len, 1, fp) != 1) {
//                 goto save_fail;
//             }
//             // write is_dir
//             uint8_t is_dir = node->is_dir;
//             if (fwrite(&is_dir, 1, 1, fp) != 1) {
//                 goto save_fail;
//             }
//
//             // write node size
//             uint64_t size = node->size;
//             if (fwrite(&size, 8, 1, fp) != 1) {
//                 goto save_fail;
//             }
//
//             // write node modification time
//             uint64_t mtime = node->mtime;
//             if (fwrite(&mtime, 8, 1, fp) != 1) {
//                 goto save_fail;
//             }
//
//             // write node sort position
//             uint32_t pos = node->pos;
//             if (fwrite(&pos, 4, 1, fp) != 1) {
//                 goto save_fail;
//             }
//
//             DatabaseEntry *temp = node->children;
//             if (!temp) {
//                 // reached end of children, write delimiter
//                 if (fwrite(&del, 2, 1, fp) != 1) {
//                     goto save_fail;
//                 }
//                 DatabaseEntry *current = node;
//                 while (true) {
//                     temp = current->next;
//                     if (temp) {
//                         // found next, sibling add that
//                         node = temp;
//                         break;
//                     }
//
//                     if (fwrite(&del, 2, 1, fp) != 1) {
//                         goto save_fail;
//                     }
//                     temp = current->parent;
//                     if (!temp) {
//                         // reached last node, abort
//                         node = NULL;
//                         break;
//                     }
//                     else {
//                         current = temp;
//                     }
//                 }
//             }
//             else {
//                 node = temp;
//             }
//         }
//         else {
//             goto save_fail;
//         }
//     }
//
//     fclose(fp);
//
//     trace("[database_save] saved %s\n", path);
//     return true;
//
// save_fail:
//
//     fclose(fp);
//     unlink(db_path->str);
//     g_string_free(db_path, TRUE);
//     return false;
// }

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

static void
db_entry_update_folder_size(FsearchDatabaseEntryFolder *folder, off_t size) {
    if (!folder) {
        return;
    }
    folder->shared.size += size;
    db_entry_update_folder_size(folder->shared.parent, size);
}

typedef struct DatabaseWalkContext {
    FsearchDatabase *db;
    GString *path;
    GTimer *timer;
    GCancellable *cancellable;
    void (*status_cb)(const char *);
    bool exclude_hidden;
} DatabaseWalkContext;

static int
db_folder_scan_recursive(DatabaseWalkContext *walk_context, FsearchDatabaseEntryFolder *parent) {
    if (walk_context->cancellable && g_cancellable_is_cancelled(walk_context->cancellable)) {
        return WALK_CANCEL;
    }

    GString *path = walk_context->path;
    g_string_append_c(path, '/');

    // remember end of parent path
    gsize path_len = path->len;

    DIR *dir = NULL;
    if (!(dir = opendir(path->str))) {
        return WALK_BADIO;
    }

    double elapsed_seconds = g_timer_elapsed(walk_context->timer, NULL);
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
            closedir(dir);
            return WALK_CANCEL;
        }
        if (walk_context->exclude_hidden && dent->d_name[0] == '.') {
            // file is dotfile, skip
            continue;
        }
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
            continue;
        }
        if (file_is_excluded(dent->d_name, db->exclude_files)) {
            continue;
        }

        // create full path of file/folder
        g_string_truncate(path, path_len);
        g_string_append(path, dent->d_name);

        struct stat st;
        if (lstat(path->str, &st) == -1) {
            // warn("Can't stat %s", fn);
            continue;
        }

        const bool is_dir = S_ISDIR(st.st_mode);
        if (is_dir && directory_is_excluded(path->str, db->excludes)) {
            g_debug("[database_scan] excluded directory: %s", path->str);
            continue;
        }

        if (is_dir) {
            FsearchDatabaseEntryFolder *folder_entry = fsearch_memory_pool_malloc(db->folder_pool);
            folder_entry->shared.name = strdup(dent->d_name);
            folder_entry->shared.parent = parent;
            folder_entry->shared.type = DATABASE_ENTRY_TYPE_FOLDER;

            parent->folder_children = g_slist_prepend(parent->folder_children, folder_entry);
            darray_add_item(db->folders, folder_entry);

            db->num_folders++;

            db_folder_scan_recursive(walk_context, folder_entry);
        }
        else {
            FsearchDatabaseEntryFile *file_entry = fsearch_memory_pool_malloc(db->file_pool);
            file_entry->shared.name = strdup(dent->d_name);
            file_entry->shared.parent = parent;
            file_entry->shared.type = DATABASE_ENTRY_TYPE_FILE;
            file_entry->shared.size = st.st_size;

            // update parent size
            db_entry_update_folder_size(parent, file_entry->shared.size);

            parent->file_children = g_slist_prepend(parent->file_children, file_entry);
            darray_add_item(db->files, file_entry);

            db->num_files++;
        }

        db->num_entries++;
    }

    closedir(dir);
    return WALK_OK;
}

static void
db_scan_folder(FsearchDatabase *db, const char *dname, GCancellable *cancellable, void (*status_cb)(const char *)) {
    assert(dname != NULL);
    assert(dname[0] == '/');
    g_debug("[database] scan path: %s", dname);

    GString *path = g_string_new(dname);
    // remove leading path separator '/'
    g_string_erase(path, 0, 1);

    GTimer *timer = g_timer_new();
    g_timer_start(timer);
    DatabaseWalkContext walk_context = {
        .db = db,
        .path = path,
        .timer = timer,
        .cancellable = cancellable,
        .status_cb = status_cb,
        .exclude_hidden = db->exclude_hidden,
    };

    FsearchDatabaseEntryFolder *parent = fsearch_memory_pool_malloc(db->folder_pool);
    parent->shared.name = strdup(path->str);
    parent->shared.parent = NULL;

    if (strcmp(path->str, "") != 0) {
        g_string_prepend_c(path, '/');
    }
    uint32_t res = db_folder_scan_recursive(&walk_context, parent);

    g_string_free(path, TRUE);
    g_timer_destroy(timer);
    if (res == WALK_OK) {
        g_debug("[database] scanned: %d files, %d files -> %d total", db->num_files, db->num_folders, db->num_entries);
        return;
    }

    g_warning("[database_scan] walk error: %d\n", res);
}

bool
db_save(FsearchDatabase *db) {
    assert(db != NULL);
    return false;
}

FsearchDatabase *
db_new(GList *includes, GList *excludes, char **exclude_files, bool exclude_hidden) {
    FsearchDatabase *db = g_new0(FsearchDatabase, 1);
    g_mutex_init(&db->mutex);
    if (includes) {
        db->includes = g_list_copy_deep(includes, (GCopyFunc)fsearch_include_path_copy, NULL);
    }
    if (excludes) {
        db->excludes = g_list_copy_deep(excludes, (GCopyFunc)fsearch_exclude_path_copy, NULL);
    }
    if (exclude_files) {
        db->exclude_files = g_strdupv(exclude_files);
    }
    db->file_pool = fsearch_memory_pool_new(BTREE_NODE_POOL_BLOCK_ELEMENTS,
                                            sizeof(FsearchDatabaseEntryFile),
                                            (GDestroyNotify)db_file_entry_destroy);
    db->folder_pool = fsearch_memory_pool_new(BTREE_NODE_POOL_BLOCK_ELEMENTS,
                                              sizeof(FsearchDatabaseEntryFolder),
                                              (GDestroyNotify)db_folder_entry_destroy);
    db->files = darray_new(1000);
    db->folders = darray_new(1000);
    db->exclude_hidden = exclude_hidden;
    db->ref_count = 1;
    return db;
}

static void
db_free(FsearchDatabase *db) {
    assert(db != NULL);

    g_debug("[database_free] freeing...");
    db_lock(db);
    if (db->ref_count > 0) {
        g_warning("[database_free] pending references on free: %d", db->ref_count);
    }

    if (db->files) {
        darray_free(db->files);
        db->files = NULL;
    }
    if (db->folders) {
        darray_free(db->folders);
        db->folders = NULL;
    }
    if (db->folder_pool) {
        fsearch_memory_pool_free(db->folder_pool);
        db->folder_pool = NULL;
    }
    if (db->file_pool) {
        fsearch_memory_pool_free(db->file_pool);
        db->file_pool = NULL;
    }
    if (db->includes) {
        g_list_free_full(db->includes, (GDestroyNotify)fsearch_include_path_free);
        db->includes = NULL;
    }
    if (db->excludes) {
        g_list_free_full(db->excludes, (GDestroyNotify)fsearch_exclude_path_free);
        db->excludes = NULL;
    }
    if (db->exclude_files) {
        g_strfreev(db->exclude_files);
        db->exclude_files = NULL;
    }
    db_unlock(db);

    g_mutex_clear(&db->mutex);
    g_free(db);
    db = NULL;

    malloc_trim(0);

    g_debug("[database_free] freed");
    return;
}

time_t
db_get_timestamp(FsearchDatabase *db) {
    assert(db != NULL);
    return db->timestamp;
}

uint32_t
db_get_num_files(FsearchDatabase *db) {
    assert(db != NULL);
    return db->num_files;
}

uint32_t
db_get_num_folders(FsearchDatabase *db) {
    assert(db != NULL);
    return db->num_folders;
}

uint32_t
db_get_num_entries(FsearchDatabase *db) {
    assert(db != NULL);
    return db->num_entries;
}

void
db_unlock(FsearchDatabase *db) {
    assert(db != NULL);
    g_mutex_unlock(&db->mutex);
}

void
db_lock(FsearchDatabase *db) {
    assert(db != NULL);
    g_mutex_lock(&db->mutex);
}

bool
db_try_lock(FsearchDatabase *db) {
    assert(db != NULL);
    return g_mutex_trylock(&db->mutex);
}

DynamicArray *
db_get_files(FsearchDatabase *db) {
    assert(db != NULL);
    return db->files;
}

DynamicArray *
db_get_folders(FsearchDatabase *db) {
    assert(db != NULL);
    return db->folders;
}

bool
db_load(FsearchDatabase *db, const char *path, void (*status_cb)(const char *)) {
    assert(db != NULL);
    return false;
}

bool
db_scan(FsearchDatabase *db, GCancellable *cancellable, void (*status_cb)(const char *)) {
    assert(db != NULL);

    bool ret = false;
    for (GList *l = db->includes; l != NULL; l = l->next) {
        FsearchIncludePath *fs_path = l->data;
        if (!fs_path->path) {
            continue;
        }
        if (!fs_path->enabled) {
            continue;
        }
        if (fs_path->update) {
            db_scan_folder(db, fs_path->path, cancellable, status_cb);
        }
    }
    db_sort(db);
    return ret;
}

void
db_ref(FsearchDatabase *db) {
    assert(db != NULL);
    db_lock(db);
    db->ref_count++;
    db_unlock(db);
    g_debug("[database_ref] increased to: %d", db->ref_count);
}

void
db_unref(FsearchDatabase *db) {
    assert(db != NULL);
    db_lock(db);
    db->ref_count--;
    db_unlock(db);
    g_debug("[database_unref] dropped to: %d", db->ref_count);
    if (db->ref_count <= 0) {
        db_free(db);
    }
}

static char *
init_path_recursively(FsearchDatabaseEntryFolder *folder, char *path, size_t path_len) {
    char *insert_pos = NULL;
    if (folder->shared.parent) {
        insert_pos = init_path_recursively(folder->shared.parent, path, path_len);
    }
    insert_pos += g_strlcpy(insert_pos, folder->shared.name, path - insert_pos);
    *insert_pos = '/';
    insert_pos++;
    return insert_pos;
}

static void
build_path_recursively(FsearchDatabaseEntryFolder *folder, GString *str) {
    if (folder->shared.parent) {
        build_path_recursively(folder->shared.parent, str);
    }
    g_string_append_c(str, '/');
    g_string_append(str, folder->shared.name);
}

int32_t
db_entry_init_path(FsearchDatabaseEntry *entry, char *path, size_t path_len) {
    init_path_recursively(entry->shared.parent, path, path_len);
    return 0;
}

GString *
db_entry_get_path(FsearchDatabaseEntry *entry) {
    GString *path = g_string_new(NULL);
    build_path_recursively(entry->shared.parent, path);
    return path;
}

void
db_entry_append_path(FsearchDatabaseEntry *entry, GString *str) {
    build_path_recursively(entry->shared.parent, str);
}

off_t
db_entry_get_size(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.size : 0;
}

const char *
db_entry_get_name(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.name : NULL;
}

FsearchDatabaseEntryFolder *
db_entry_get_parent(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.parent : NULL;
}

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.type : DATABASE_ENTRY_TYPE_NONE;
}

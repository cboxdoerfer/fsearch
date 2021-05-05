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

#define G_LOG_DOMAIN "fsearch-database"

#include <assert.h>
#include <dirent.h>
#include <fnmatch.h>
#include <glib/gi18n.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "fsearch_database.h"
#include "fsearch_exclude_path.h"
#include "fsearch_file_utils.h"
#include "fsearch_index.h"
#include "fsearch_memory_pool.h"

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

#define DATABASE_MAJOR_VERSION 0
#define DATABASE_MINOR_VERSION 3
#define DATABASE_MAGIC_NUMBER "FSDB"

struct FsearchDatabaseEntryCommon {
    FsearchDatabaseEntryFolder *parent;
    char *name;
    off_t size;

    // idx: index of this entry in the sorted list at pos DATABASE_INDEX_TYPE_NAME
    uint32_t idx;
    uint8_t type;
};

struct _FsearchDatabaseEntryFile {
    struct FsearchDatabaseEntryCommon shared;
};

struct _FsearchDatabaseEntryFolder {
    struct FsearchDatabaseEntryCommon shared;

    // db_idx: the database index this folder belongs to
    uint32_t db_idx;
};

struct _FsearchDatabase {
    DynamicArray *files;
    DynamicArray *folders;

    DynamicArray *sorted_files[NUM_DATABASE_INDEX_TYPES];
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_TYPES];

    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;

    uint32_t num_entries;
    uint32_t num_folders;
    uint32_t num_files;

    GList *indexes;
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
db_sorted_entries_free(FsearchDatabase *db) {
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        DynamicArray *files = db->sorted_files[i];
        if (files) {
            darray_free(files);
            files = NULL;
        }
        db->sorted_files[i] = NULL;

        DynamicArray *folders = db->sorted_folders[i];
        if (folders) {
            darray_free(folders);
            folders = NULL;
        }
        db->sorted_folders[i] = NULL;
    }
}

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

int
db_entry_compare_entries_by_size(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    off_t size_a = db_entry_get_size(*a);
    off_t size_b = db_entry_get_size(*b);
    return (size_a > size_b) ? 1 : -1;
}

int
db_entry_compare_entries_by_type(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    FsearchDatabaseEntryType type_a = db_entry_get_type(*a);
    FsearchDatabaseEntryType type_b = db_entry_get_type(*b);
    if (type_a == DATABASE_ENTRY_TYPE_FOLDER && type_b == DATABASE_ENTRY_TYPE_FOLDER) {
        return 0;
    }

    const char *name_a = db_entry_get_name(*a);
    const char *name_b = db_entry_get_name(*b);
    char *file_type_a = fsearch_file_utils_get_file_type_non_localized(name_a, FALSE);
    char *file_type_b = fsearch_file_utils_get_file_type_non_localized(name_b, FALSE);

    int return_val = strcmp(file_type_a, file_type_b);
    g_free(file_type_a);
    file_type_a = NULL;
    g_free(file_type_b);
    file_type_b = NULL;

    return return_val;
}

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return 0;
}

int
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return 0;
}

int
db_entry_compare_entries_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
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

int
db_entry_compare_entries_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return strverscmp((*a)->shared.name, (*b)->shared.name);
}

static void
db_sort(FsearchDatabase *db) {
    assert(db != NULL);

    GTimer *timer = g_timer_new();

    DynamicArray *files = db->sorted_files[DATABASE_INDEX_TYPE_NAME];
    if (files) {
        darray_sort_multi_threaded(files, (DynamicArrayCompareFunc)db_entry_compare_entries_by_path);
        db->sorted_files[DATABASE_INDEX_TYPE_PATH] = darray_copy(files);

        darray_sort(files, (DynamicArrayCompareFunc)db_entry_compare_entries_by_name);

        db->sorted_files[DATABASE_INDEX_TYPE_SIZE] = darray_copy(files);
        darray_sort_multi_threaded(db->sorted_files[DATABASE_INDEX_TYPE_SIZE],
                                   (DynamicArrayCompareFunc)db_entry_compare_entries_by_size);

        const double seconds = g_timer_elapsed(timer, NULL);
        g_timer_reset(timer);
        g_debug("[db_sort] sorted files: %f s", seconds);
    }
    DynamicArray *folders = db->sorted_folders[DATABASE_INDEX_TYPE_NAME];
    if (folders) {
        darray_sort_multi_threaded(folders, (DynamicArrayCompareFunc)db_entry_compare_entries_by_path);
        db->sorted_folders[DATABASE_INDEX_TYPE_PATH] = darray_copy(folders);

        darray_sort(folders, (DynamicArrayCompareFunc)db_entry_compare_entries_by_name);

        db->sorted_folders[DATABASE_INDEX_TYPE_SIZE] = darray_copy(folders);
        darray_sort_multi_threaded(db->sorted_folders[DATABASE_INDEX_TYPE_SIZE],
                                   (DynamicArrayCompareFunc)db_entry_compare_entries_by_size);

        const double seconds = g_timer_elapsed(timer, NULL);
        g_debug("[db_sort] sorted folders: %f s", seconds);
    }

    g_timer_destroy(timer);
    timer = NULL;
}

static void
db_update_timestamp(FsearchDatabase *db) {
    assert(db != NULL);
    db->timestamp = time(NULL);
}

static void
db_entry_update_folder_indices(FsearchDatabase *db) {
    if (!db || !db->sorted_folders[DATABASE_INDEX_TYPE_NAME]) {
        return;
    }
    uint32_t num_folders = darray_get_num_items(db->sorted_folders[DATABASE_INDEX_TYPE_NAME]);
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntryFolder *folder = darray_get_item(db->sorted_folders[DATABASE_INDEX_TYPE_NAME], i);
        if (!folder) {
            continue;
        }
        folder->shared.idx = i;
    }
}

static uint8_t
get_name_offset(const char *old, const char *new) {
    uint8_t offset = 0;
    while (old[offset] == new[offset] && old[offset] != '\0' && new[offset] != '\0') {
        offset++;
    }
    return offset;
}

static FILE *
db_file_open_locked(const char *file_path, const char *mode) {
    FILE *file_pointer = fopen(file_path, mode);
    if (!file_pointer) {
        g_debug("[db_file] can't open database file: %s", file_path);
        return NULL;
    }

    int file_descriptor = fileno(file_pointer);
    if (flock(file_descriptor, LOCK_EX | LOCK_NB) == -1) {
        g_debug("[db_file] database file is already locked by a different process: %s", file_path);

        fclose(file_pointer);
        file_pointer = NULL;
    }

    return file_pointer;
}

static bool
db_load_entry_shared(FILE *fp, struct FsearchDatabaseEntryCommon *shared, GString *previous_entry_name) {
    // name_offset: character position after which previous_entry_name and entry_name differ
    uint8_t name_offset = 0;
    if (fread(&name_offset, 1, 1, fp) != 1) {
        g_debug("[db_load] failed to load name offset");
        return false;
    }

    // name_len: length of the new name characters
    uint8_t name_len = 0;
    if (fread(&name_len, 1, 1, fp) != 1) {
        g_debug("[db_load] failed to load name length");
        return false;
    }

    // erase previous name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);

    char name[256] = "";
    // name: new characters to be appended to previous_entry_name
    if (name_len > 0) {
        if (fread(name, name_len, 1, fp) != 1) {
            g_debug("[db_load] failed to load name");
            return false;
        }
        name[name_len] = '\0';
    }

    // now we can build the new full file name
    g_string_append(previous_entry_name, name);
    shared->name = g_strdup(previous_entry_name->str);

    // size: size of file/folder
    uint64_t size = 0;
    if (fread(&size, 8, 1, fp) != 1) {
        g_debug("[db_load] failed to load size");
        return false;
    }
    shared->size = (off_t)size;

    return true;
}

static bool
db_load_header(FILE *fp) {
    char magic[5] = "";
    if (fread(magic, strlen(DATABASE_MAGIC_NUMBER), 1, fp) != 1) {
        return false;
    }
    magic[4] = '\0';
    if (strcmp(magic, DATABASE_MAGIC_NUMBER) != 0) {
        g_debug("[db_load] invalid magic number: %s", magic);
        return false;
    }

    uint8_t majorver = 0;
    if (fread(&majorver, 1, 1, fp) != 1) {
        return false;
    }
    if (majorver != DATABASE_MAJOR_VERSION) {
        g_debug("[db_load] invalid major version: %d", majorver);
        return false;
    }

    uint8_t minorver = 0;
    if (fread(&minorver, 1, 1, fp) != 1) {
        return false;
    }
    if (minorver != DATABASE_MINOR_VERSION) {
        g_debug("[db_load] invalid minor version: %d", minorver);
        return false;
    }

    return true;
}

static bool
db_load_parent_idx(FILE *fp, uint32_t *parent_idx) {
    if (fread(parent_idx, 4, 1, fp) != 1) {
        g_debug("[db_load] failed to load parent_idx");
        return false;
    }
    return true;
}

static bool
db_load_folders(FILE *fp, DynamicArray *folders, uint32_t num_folders) {
    bool result = true;
    GString *previous_entry_name = g_string_sized_new(256);

    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_folders; idx++) {
        FsearchDatabaseEntryFolder *folder = darray_get_item(folders, idx);

        if (!db_load_entry_shared(fp, &folder->shared, previous_entry_name)) {
            result = false;
            break;
        }

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        if (!db_load_parent_idx(fp, &parent_idx)) {
            result = false;
            break;
        }

        if (parent_idx != folder->shared.idx) {
            FsearchDatabaseEntryFolder *parent = darray_get_item(folders, parent_idx);
            folder->shared.parent = parent;
        }
        else {
            // parent_idx and idx are the same (i.e. folder is a root index) so it has no parent
            folder->shared.parent = NULL;
        }
    }

    // fail if we didn't read the correct number of folders
    if (result && idx != num_folders) {
        g_debug("[db_load] failed to read folders (read %d of %d)", idx, num_folders);
        result = false;
    }

    g_string_free(previous_entry_name, TRUE);
    previous_entry_name = NULL;

    return result;
}

static bool
db_load_files(FILE *fp, FsearchMemoryPool *pool, DynamicArray *folders, DynamicArray *files, uint32_t num_files) {
    bool result = true;
    GString *previous_entry_name = g_string_sized_new(256);

    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_files; idx++) {
        FsearchDatabaseEntryFile *file = fsearch_memory_pool_malloc(pool);
        file->shared.type = DATABASE_ENTRY_TYPE_FILE;
        file->shared.idx = idx;

        if (!db_load_entry_shared(fp, &file->shared, previous_entry_name)) {
            result = false;
            break;
        }

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        if (!db_load_parent_idx(fp, &parent_idx)) {
            result = false;
            break;
        }
        FsearchDatabaseEntryFolder *parent = darray_get_item(folders, parent_idx);
        file->shared.parent = parent;

        darray_add_item(files, file);
    }

    // fail if we didn't read the correct number of files
    if (result && idx != num_files) {
        g_debug("[db_load] failed to read files (read %d of %d)", idx, num_files);
        result = false;
    }

    g_string_free(previous_entry_name, TRUE);
    previous_entry_name = NULL;

    return result;
}

static bool
db_load_sorted_entries(FILE *fp, DynamicArray *src, uint32_t num_src_entries, DynamicArray *dest) {

    uint32_t *indexes = calloc(num_src_entries + 1, sizeof(uint32_t));
    assert(indexes != NULL);

    bool res = true;

    if (fread(indexes, 4, num_src_entries, fp) != num_src_entries) {
        res = false;
    }
    else {
        for (uint32_t i = 0; i < num_src_entries; i++) {
            uint32_t idx = indexes[i];
            void *entry = darray_get_item(src, idx);
            if (!entry) {
                return false;
            }
            darray_add_item(dest, entry);
        }
    }

    free(indexes);
    indexes = NULL;

    return res;
}

static bool
db_load_sorted_arrays(FILE *fp, DynamicArray **sorted_folders, DynamicArray **sorted_files) {
    uint32_t num_sorted_arrays = 0;

    DynamicArray *files = sorted_files[0];
    DynamicArray *folders = sorted_folders[0];

    if (fread(&num_sorted_arrays, 4, 1, fp) != 1) {
        g_debug("failed to load number of sorted arrays");
        return false;
    }

    for (uint32_t i = 0; i < num_sorted_arrays; i++) {
        uint32_t sorted_array_id = 0;
        if (fread(&sorted_array_id, 4, 1, fp) != 1) {
            g_debug("failed to load sorted array id");
            return false;
        }

        if (sorted_array_id < 1 || sorted_array_id >= NUM_DATABASE_INDEX_TYPES) {
            g_debug("sorted array id is not supported: %d", sorted_array_id);
            return false;
        }

        uint32_t num_folders = darray_get_num_items(folders);
        sorted_folders[sorted_array_id] = darray_new(num_folders);
        if (!db_load_sorted_entries(fp, folders, num_folders, sorted_folders[sorted_array_id])) {
            g_debug("failed to load sorted folder indexes: %d", sorted_array_id);
            return false;
        }

        uint32_t num_files = darray_get_num_items(files);
        sorted_files[sorted_array_id] = darray_new(num_files);
        if (!db_load_sorted_entries(fp, files, num_files, sorted_files[sorted_array_id])) {
            g_debug("failed to load sorted file indexes: %d", sorted_array_id);
            return false;
        }
    }

    return true;
}

bool
db_load(FsearchDatabase *db, const char *file_path, void (*status_cb)(const char *)) {
    assert(file_path != NULL);
    assert(db != NULL);

    FILE *fp = db_file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    DynamicArray *folders = NULL;
    DynamicArray *files = NULL;
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_TYPES] = {NULL};
    DynamicArray *sorted_files[NUM_DATABASE_INDEX_TYPES] = {NULL};

    if (!db_load_header(fp)) {
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (fread(&num_folders, 4, 1, fp) != 1) {
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (fread(&num_files, 4, 1, fp) != 1) {
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    // pre-allocate the folders array so we can later map parent indices to the corresponding pointers
    sorted_folders[DATABASE_INDEX_TYPE_NAME] = darray_new(num_folders);
    folders = sorted_folders[DATABASE_INDEX_TYPE_NAME];

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntryFolder *folder = fsearch_memory_pool_malloc(db->folder_pool);
        folder->shared.idx = i;
        folder->shared.type = DATABASE_ENTRY_TYPE_FOLDER;
        folder->shared.parent = NULL;
        darray_add_item(folders, folder);
    }

    if (status_cb) {
        status_cb(_("Loading folders…"));
    }
    // load folders
    if (!db_load_folders(fp, folders, num_folders)) {
        goto load_fail;
    }

    if (status_cb) {
        status_cb(_("Loading files…"));
    }
    // load files
    sorted_files[DATABASE_INDEX_TYPE_NAME] = darray_new(num_files);
    files = sorted_files[DATABASE_INDEX_TYPE_NAME];
    if (!db_load_files(fp, db->file_pool, folders, files, num_files)) {
        goto load_fail;
    }

    if (!db_load_sorted_arrays(fp, sorted_folders, sorted_files)) {
        goto load_fail;
    }

    db_sorted_entries_free(db);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        db->sorted_files[i] = sorted_files[i];
        db->sorted_folders[i] = sorted_folders[i];
    }

    db->num_entries = num_files + num_folders;
    db->num_files = num_files;
    db->num_folders = num_folders;

    fclose(fp);
    fp = NULL;

    return true;

load_fail:
    g_debug("[db_load] load failed");

    if (fp) {
        fclose(fp);
        fp = NULL;
    }

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (sorted_folders[i]) {
            darray_free(sorted_folders[i]);
        }
        sorted_folders[i] = NULL;

        if (sorted_files[i]) {
            darray_free(sorted_files[i]);
        }
        sorted_files[i] = NULL;
    }

    return false;
}

static bool
db_save_entry_shared(FILE *fp,
                     struct FsearchDatabaseEntryCommon *shared,
                     uint32_t parent_idx,
                     GString *previous_entry_name,
                     GString *new_entry_name) {
    // init new_entry_name with the name of the current entry
    g_string_erase(new_entry_name, 0, -1);
    g_string_append(new_entry_name, shared->name);

    // name_offset: character position after which previous_entry_name and new_entry_name differ
    uint8_t name_offset = get_name_offset(previous_entry_name->str, new_entry_name->str);
    if (fwrite(&name_offset, 1, 1, fp) != 1) {
        g_debug("[db_save] failed to save name offset");
        return false;
    }

    // name_len: length of the new name characters
    uint8_t name_len = new_entry_name->len - name_offset;
    if (fwrite(&name_len, 1, 1, fp) != 1) {
        g_debug("[db_save] failed to save name length");
        return false;
    }

    // append new unique characters to previous_entry_name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);
    g_string_append(previous_entry_name, new_entry_name->str + name_offset);

    if (name_len > 0) {
        // name: new characters to be written to file
        const char *name = previous_entry_name->str + name_offset;
        if (fwrite(name, name_len, 1, fp) != 1) {
            g_debug("[db_save] failed to save name");
            return false;
        }
    }

    // size: file or folder size (folder size: sum of all children sizes)
    uint64_t size = shared->size;
    if (fwrite(&size, 8, 1, fp) != 1) {
        g_debug("[db_save] failed to save size");
        return false;
    }

    // parent_idx: index of parent folder
    if (fwrite(&parent_idx, 4, 1, fp) != 1) {
        g_debug("[db_save] failed to save parent_idx");
        return false;
    }

    return true;
}

static bool
db_save_header(FILE *fp) {
    const char magic[] = DATABASE_MAGIC_NUMBER;
    if (fwrite(magic, strlen(magic), 1, fp) != 1) {
        return false;
    }

    const uint8_t majorver = DATABASE_MAJOR_VERSION;
    if (fwrite(&majorver, 1, 1, fp) != 1) {
        return false;
    }

    const uint8_t minorver = DATABASE_MINOR_VERSION;
    if (fwrite(&minorver, 1, 1, fp) != 1) {
        return false;
    }

    return true;
}
static bool
db_save_files(FILE *fp, DynamicArray *files, uint32_t num_files) {
    bool result = true;

    GString *name_prev = g_string_sized_new(256);
    GString *name_new = g_string_sized_new(256);

    for (uint32_t i = 0; i < num_files; i++) {
        FsearchDatabaseEntryFile *file = darray_get_item(files, i);

        // let's also update the idx of the file here while we're at it to make sure we have the correct
        // idx set when we store the fast sort indexes
        file->shared.idx = i;

        uint32_t parent_idx = file->shared.parent->shared.idx;
        if (!db_save_entry_shared(fp, &file->shared, parent_idx, name_prev, name_new)) {
            result = false;
            break;
        }
    }

    g_string_free(name_prev, TRUE);
    name_prev = NULL;

    g_string_free(name_new, TRUE);
    name_new = NULL;

    return result;
}

static uint32_t *
build_sorted_entry_index_list(DynamicArray *entries, uint32_t num_entries) {
    if (num_entries < 1) {
        return NULL;
    }
    uint32_t *indexes = calloc(num_entries + 1, sizeof(uint32_t));
    assert(indexes != NULL);

    for (int i = 0; i < num_entries; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        indexes[i] = entry->shared.idx;
    }
    return indexes;
}

static bool
db_save_sorted_entries(FILE *fp, DynamicArray *entries, uint32_t num_entries) {
    if (num_entries < 1) {
        // nothing to write, we're done here
        return true;
    }

    uint32_t *sorted_entry_index_list = build_sorted_entry_index_list(entries, num_entries);
    if (!sorted_entry_index_list) {
        g_debug("[db_save] failed to create sorted index list");
        return false;
    }

    bool res = true;
    if (fwrite(sorted_entry_index_list, 4, num_entries, fp) != num_entries) {
        res = false;
    }
    free(sorted_entry_index_list);
    sorted_entry_index_list = NULL;

    return res;
}

static bool
db_save_sorted_arrays(FILE *fp, FsearchDatabase *db, uint32_t num_files, uint32_t num_folders) {
    uint32_t num_sorted_arrays = 0;
    for (uint32_t i = 1; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (db->sorted_folders[i] && db->sorted_files[i]) {
            num_sorted_arrays++;
        }
    }

    if (fwrite(&num_sorted_arrays, 4, 1, fp) != 1) {
        g_debug("failed to save number of sorted arrays: %d", num_sorted_arrays);
        return false;
    }

    if (num_sorted_arrays < 1) {
        return true;
    }

    for (uint32_t id = 1; id < NUM_DATABASE_INDEX_TYPES; id++) {
        DynamicArray *folders = db->sorted_folders[id];
        DynamicArray *files = db->sorted_files[id];
        if (!files || !folders) {
            continue;
        }

        // id: this is the id of the sorted files
        if (fwrite(&id, 4, 1, fp) != 1) {
            g_debug("failed to save sorted arrays id: %d", id);
            return false;
        }

        if (!db_save_sorted_entries(fp, folders, num_folders)) {
            g_debug("failed to save sorted folders");
            return false;
        }
        if (!db_save_sorted_entries(fp, files, num_files)) {
            g_debug("failed to save sorted files");
            return false;
        }
    }
    return true;
}

static bool
db_save_folders(FILE *fp, DynamicArray *folders, uint32_t num_folders) {
    bool result = true;

    GString *name_prev = g_string_sized_new(256);
    GString *name_new = g_string_sized_new(256);

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntryFolder *folder = darray_get_item(folders, i);

        uint32_t parent_idx = folder->shared.parent ? folder->shared.parent->shared.idx : folder->shared.idx;
        if (!db_save_entry_shared(fp, &folder->shared, parent_idx, name_prev, name_new)) {
            result = false;
            break;
        }
    }

    g_string_free(name_prev, TRUE);
    name_prev = NULL;

    g_string_free(name_new, TRUE);
    name_new = NULL;

    return result;
}

bool
db_save_indexes(FILE *fp, FsearchDatabase *db) {
    // TODO
    return true;
}

bool
db_save_excludes(FILE *fp, FsearchDatabase *db) {
    // TODO
    return true;
}

bool
db_save_exclude_pattern(FILE *fp, FsearchDatabase *db) {
    // TODO
    return true;
}

bool
db_save(FsearchDatabase *db, const char *path) {
    assert(path != NULL);
    assert(db != NULL);

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_debug("[db_save] database path doesn't exist: %s", path);
        return false;
    }

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    GString *path_full = g_string_new(path);
    g_string_append_c(path_full, G_DIR_SEPARATOR);
    g_string_append(path_full, "fsearch.db");

    GString *path_full_temp = g_string_new(path_full->str);
    g_string_append(path_full_temp, ".tmp");

    FILE *fp = db_file_open_locked(path_full_temp->str, "wb");
    if (!fp) {
        goto save_fail;
    }

    db_entry_update_folder_indices(db);

    if (!db_save_header(fp)) {
        goto save_fail;
    }

    DynamicArray *files = db->sorted_files[DATABASE_INDEX_TYPE_NAME];
    DynamicArray *folders = db->sorted_folders[DATABASE_INDEX_TYPE_NAME];

    uint32_t num_folders = darray_get_num_items(folders);
    if (fwrite(&num_folders, 4, 1, fp) != 1) {
        goto save_fail;
    }

    uint32_t num_files = darray_get_num_items(files);
    if (fwrite(&num_files, 4, 1, fp) != 1) {
        goto save_fail;
    }

    if (!db_save_indexes(fp, db)) {
        goto save_fail;
    }

    if (!db_save_excludes(fp, db)) {
        goto save_fail;
    }

    if (!db_save_exclude_pattern(fp, db)) {
        goto save_fail;
    }

    if (!db_save_folders(fp, folders, num_folders)) {
        goto save_fail;
    }
    if (!db_save_files(fp, files, num_files)) {
        goto save_fail;
    }
    if (!db_save_sorted_arrays(fp, db, num_files, num_folders)) {
        goto save_fail;
    }

    // remove current database file
    unlink(path_full->str);

    fclose(fp);
    fp = NULL;

    // rename temporary fsearch.db.tmp to fsearch.db
    if (rename(path_full_temp->str, path_full->str) != 0) {
        goto save_fail;
    }

    g_string_free(path_full, TRUE);
    path_full = NULL;

    g_string_free(path_full_temp, TRUE);
    path_full_temp = NULL;

    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_stop(timer);
    g_timer_destroy(timer);
    timer = NULL;

    g_debug("[db_save] database file saved in: %f ms", seconds * 1000);

    return true;

save_fail:
    g_warning("save fail");

    if (fp) {
        fclose(fp);
        fp = NULL;
    }

    // remove temporary fsearch.db.tmp file
    unlink(path_full_temp->str);

    g_string_free(path_full, TRUE);
    path_full = NULL;

    g_string_free(path_full_temp, TRUE);
    path_full_temp = NULL;

    g_timer_destroy(timer);
    timer = NULL;

    return false;
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
    g_string_append_c(path, G_DIR_SEPARATOR);

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
            g_debug("[db_scan] excluded directory: %s", path->str);
            continue;
        }

        if (is_dir) {
            FsearchDatabaseEntryFolder *folder_entry = fsearch_memory_pool_malloc(db->folder_pool);
            folder_entry->shared.name = strdup(dent->d_name);
            folder_entry->shared.parent = parent;
            folder_entry->shared.type = DATABASE_ENTRY_TYPE_FOLDER;

            darray_add_item(db->sorted_folders[DATABASE_INDEX_TYPE_NAME], folder_entry);

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

            darray_add_item(db->sorted_files[DATABASE_INDEX_TYPE_NAME], file_entry);

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
    assert(dname[0] == G_DIR_SEPARATOR);
    g_debug("[db_scan] scan path: %s", dname);

    if (!g_file_test(dname, G_FILE_TEST_IS_DIR)) {
        g_warning("[db_scan] %s doesn't exist", dname);
        return;
    }

    GString *path = g_string_new(dname);
    // remove leading path separator '/' for root directory
    if (strcmp(path->str, G_DIR_SEPARATOR_S) == 0) {
        g_string_erase(path, 0, 1);
    }

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
    parent->shared.type = DATABASE_ENTRY_TYPE_FOLDER;

    darray_add_item(db->sorted_folders[DATABASE_INDEX_TYPE_NAME], parent);
    db->num_folders++;
    db->num_entries++;

    uint32_t res = db_folder_scan_recursive(&walk_context, parent);

    g_string_free(path, TRUE);
    g_timer_destroy(timer);
    if (res == WALK_OK) {
        g_debug("[db_scan] scanned: %d files, %d files -> %d total", db->num_files, db->num_folders, db->num_entries);
        return;
    }

    g_warning("[db_scan] walk error: %d", res);
}

static gint
compare_index_path(FsearchIndex *p1, FsearchIndex *p2) {
    return strcmp(p1->path, p2->path);
}

static gint
compare_exclude_path(FsearchExcludePath *p1, FsearchExcludePath *p2) {
    return strcmp(p1->path, p2->path);
}

FsearchDatabase *
db_new(GList *indexes, GList *excludes, char **exclude_files, bool exclude_hidden) {
    FsearchDatabase *db = g_new0(FsearchDatabase, 1);
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
                                            sizeof(FsearchDatabaseEntryFile),
                                            (GDestroyNotify)db_file_entry_destroy);
    db->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              sizeof(FsearchDatabaseEntryFolder),
                                              (GDestroyNotify)db_folder_entry_destroy);
    db->exclude_hidden = exclude_hidden;
    db->ref_count = 1;
    return db;
}

static void
db_free(FsearchDatabase *db) {
    assert(db != NULL);

    g_debug("[db_free] freeing...");
    db_lock(db);
    if (db->ref_count > 0) {
        g_warning("[db_free] pending references on free: %d", db->ref_count);
    }

    db_sorted_entries_free(db);

    if (db->file_pool) {
        fsearch_memory_pool_free(db->file_pool);
        db->file_pool = NULL;
    }

    if (db->folder_pool) {
        fsearch_memory_pool_free(db->folder_pool);
        db->folder_pool = NULL;
    }

    if (db->indexes) {
        g_list_free_full(db->indexes, (GDestroyNotify)fsearch_index_free);
        db->indexes = NULL;
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

    g_debug("[db_free] freed");
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
db_get_folders_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    assert(db != NULL);
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_TYPES) {
        return db->sorted_folders[sort_type];
    }
    return NULL;
}

DynamicArray *
db_get_files_sorted(FsearchDatabase *db, FsearchDatabaseIndexType sort_type) {
    assert(db != NULL);
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_TYPES) {
        return db->sorted_files[sort_type];
    }
    return NULL;
}

DynamicArray *
db_get_files(FsearchDatabase *db) {
    assert(db != NULL);
    return db->sorted_files[DATABASE_INDEX_TYPE_NAME];
}

DynamicArray *
db_get_folders(FsearchDatabase *db) {
    assert(db != NULL);
    return db->sorted_folders[DATABASE_INDEX_TYPE_NAME];
}

bool
db_scan(FsearchDatabase *db, GCancellable *cancellable, void (*status_cb)(const char *)) {
    assert(db != NULL);

    bool ret = false;

    db_sorted_entries_free(db);

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
    g_debug("[db_ref] increased to: %d", db->ref_count);
}

void
db_unref(FsearchDatabase *db) {
    assert(db != NULL);
    db_lock(db);
    db->ref_count--;
    db_unlock(db);
    g_debug("[db_unref] dropped to: %d", db->ref_count);
    if (db->ref_count <= 0) {
        db_free(db);
    }
}

static void
build_path_recursively(FsearchDatabaseEntryFolder *folder, GString *str) {
    if (!folder) {
        return;
    }
    if (folder->shared.parent) {
        build_path_recursively(folder->shared.parent, str);
        g_string_append_c(str, G_DIR_SEPARATOR);
    }
    if (strcmp(folder->shared.name, "") != 0) {
        g_string_append(str, folder->shared.name);
    }
}

GString *
db_entry_get_path(FsearchDatabaseEntry *entry) {
    GString *path = g_string_new(NULL);
    build_path_recursively(entry->shared.parent, path);
    return path;
}

GString *
db_entry_get_path_full(FsearchDatabaseEntry *entry) {
    GString *path_full = db_entry_get_path(entry);
    if (!path_full) {
        return NULL;
    }
    if (entry->shared.name[0] != G_DIR_SEPARATOR) {
        g_string_append_c(path_full, G_DIR_SEPARATOR);
    }
    g_string_append(path_full, entry->shared.name);
    return path_full;
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
    if (!entry) {
        return NULL;
    }
    if (strcmp(entry->shared.name, "") != 0) {
        return entry->shared.name;
    }
    return G_DIR_SEPARATOR_S;
}

FsearchDatabaseEntryFolder *
db_entry_get_parent(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.parent : NULL;
}

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry) {
    return entry ? entry->shared.type : DATABASE_ENTRY_TYPE_NONE;
}

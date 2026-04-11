#define G_LOG_DOMAIN "fsearch-database-file"

#include "fsearch_database_file.h"

#include "fsearch_array.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_exclude.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gmacros.h>
#include <glibconfig.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATABASE_MAJOR_VERSION 2
#define DATABASE_MINOR_VERSION 2
#define DATABASE_MAGIC_NUMBER "FSDB"
#define DATABASE_CHECKSUM_SIZE 16

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FILE, fclose)

typedef struct {
    DynamicArray *files[NUM_DATABASE_INDEX_PROPERTIES];
    DynamicArray *folders[NUM_DATABASE_INDEX_PROPERTIES];
    FsearchDatabaseIndexPropertyFlags flags;
} LoadSaveContext;

static void
update_folder_indices(DynamicArray *folders) {
    g_assert(folders);
    const uint32_t num_folders = darray_get_num_items(folders);
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders, i);
        if (!folder) {
            continue;
        }
        db_entry_set_index(folder, i);
    }
}

static uint16_t
get_name_offset(const char *old, const char *new) {
    if (!old || !new) {
        return 0;
    }

    uint16_t offset = 0;
    while (offset < UINT16_MAX
           && old[offset]
           && old[offset] == new[offset]
    ) {
        offset++;
    }
    return offset;
}

static FILE *
file_open_locked(const char *file_path, const char *mode) {
    FILE *file_pointer = fopen(file_path, mode);
    if (!file_pointer) {
        g_debug("[db_file] can't open database file: %s", file_path);
        return NULL;
    }

    int file_descriptor = fileno(file_pointer);
    if (flock(file_descriptor, LOCK_EX | LOCK_NB) == -1) {
        g_debug("[db_file] database file is already locked by a different process: %s", file_path);

        g_clear_pointer(&file_pointer, fclose);
    }

    return file_pointer;
}

typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
    bool error;
} DatabaseFileReadCursor;

typedef struct {
    FILE *fp;
    GChecksum *checksum;
    size_t bytes_written;
    bool error;
} DatabaseFileWriteCursor;

static inline void
cursor_write(DatabaseFileWriteCursor *cursor, const void *src, size_t size) {
    if (cursor->error) {
        return;
    }
    if (fwrite(src, size, 1, cursor->fp) != 1) {
        cursor->error = true;
        return;
    }
    if (cursor->checksum) {
        g_checksum_update(cursor->checksum, src, (gssize)size);
    }
    cursor->bytes_written += size;
}

static inline void
cursor_write_string(DatabaseFileWriteCursor *cursor, const char *str, uint32_t len) {
    cursor_write(cursor, &len, sizeof(len));
    if (len > 0) {
        cursor_write(cursor, str, len);
    }
}

static inline void
cursor_read(DatabaseFileReadCursor *cursor, void *dest, size_t size) {
    // If we already failed a previous read, or if this read goes out of bounds, abort.
    if (cursor->error || cursor->ptr + size > cursor->end) {
        cursor->error = true;
        return;
    }
    memcpy(dest, cursor->ptr, size);
    cursor->ptr += size;
}

// region Database-File-Read

static void
database_file_load_entry(DatabaseFileReadCursor *cursor,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         GString *previous_entry_name,
                         FsearchDatabaseEntry **entry_out,
                         FsearchDatabaseEntryType type) {
    // name_offset: character position after which previous_entry_name and entry_name differ
    uint16_t name_offset = 0;
    cursor_read(cursor, &name_offset, sizeof(name_offset));

    // name_len: length of the new name characters
    uint16_t name_len = 0;
    cursor_read(cursor, &name_len, sizeof(name_len));

    if (cursor->error) {
        return;
    }

    // Manually add sanity check since were using g_string_append_len later instead of cursor_read
    if (cursor->ptr + name_len > cursor->end) {
        cursor->error = true;
        return;
    }

    // erase the previous name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);

    // name: new characters to be appended to previous_entry_name
    if (name_len > 0) {
        g_string_append_len(previous_entry_name, (const char *)cursor->ptr, name_len);
        cursor->ptr += name_len;
    }

    *entry_out = db_entry_new(index_flags, previous_entry_name->str, NULL, type);
    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        // size: size of file/folder
        int64_t size = 0;
        cursor_read(cursor, &size, sizeof(size));

        db_entry_set_size(*entry_out, (off_t)size);
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time file/folder
        int64_t mtime = 0;
        cursor_read(cursor, &mtime, sizeof(mtime));

        db_entry_set_mtime(*entry_out, (time_t)mtime);
    }
}

static bool
database_file_read_element(void *restrict ptr, size_t size, FILE *restrict stream, GChecksum *checksum) {
    const bool res = fread(ptr, size, 1, stream) == 1 ? true : false;
    if (res && checksum) {
        g_checksum_update(checksum, ptr, (gssize)size);
    }
    return res;
}

static bool
database_file_load_header(FILE *fp, GChecksum *checksum) {
    char magic[5] = "";
    if (!database_file_read_element(magic, strlen(DATABASE_MAGIC_NUMBER), fp, checksum)) {
        return false;
    }
    magic[4] = '\0';
    if (strcmp(magic, DATABASE_MAGIC_NUMBER) != 0) {
        g_debug("[db_load] invalid magic number: %s", magic);
        return false;
    }

    uint8_t majorver = 0;
    if (!database_file_read_element(&majorver, 1, fp, checksum)) {
        return false;
    }
    if (majorver != DATABASE_MAJOR_VERSION) {
        g_debug("[db_load] invalid major version: %d", majorver);
        g_debug("[db_load] expected major version: %d", DATABASE_MAJOR_VERSION);
        return false;
    }

    uint8_t minorver = 0;
    if (!database_file_read_element(&minorver, 1, fp, checksum)) {
        return false;
    }
    if (minorver > DATABASE_MINOR_VERSION) {
        g_debug("[db_load] invalid minor version: %d", minorver);
        g_debug("[db_load] expected minor version: <= %d", DATABASE_MINOR_VERSION);
        return false;
    }

    uint8_t is_little_endian = 0;
    if (!database_file_read_element(&is_little_endian, 1, fp, checksum)) {
        return false;
    }
    const uint8_t is_little_endian_host = G_BYTE_ORDER == G_LITTLE_ENDIAN ? 1 : 0;
    if (is_little_endian != is_little_endian_host) {
        g_debug("[db_load] invalid architecture: file: %d, host: %d", is_little_endian, is_little_endian_host);
        return false;
    }
    return true;
}

static void
database_file_load_add_to_index_array(GHashTable *index_table, FsearchDatabaseEntry *entry) {
    const uint32_t db_index = db_entry_get_db_index(entry);
    DynamicArray *index_array = g_hash_table_lookup(index_table, GUINT_TO_POINTER(db_index));

    if (!index_array) {
        index_array = darray_new(1024);
        g_hash_table_insert(index_table, GUINT_TO_POINTER(db_index), index_array);
    }
    darray_add_item(index_array, entry);
}

static bool
database_file_load_folders(FILE *fp,
                           FsearchDatabaseIndexPropertyFlags index_flags,
                           DynamicArray *folders,
                           uint32_t num_folders,
                           uint64_t folder_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(1024);

    g_autofree uint8_t *folder_block = calloc(folder_block_size + 1, sizeof(uint8_t));
    g_return_val_if_fail(folder_block, false);

    if (fread(folder_block, sizeof(uint8_t), folder_block_size, fp) != folder_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    // Initialize the cursor with the allocated block
    DatabaseFileReadCursor cursor = {folder_block, folder_block + folder_block_size, false};
    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_folders; idx++) {
        g_autoptr(FsearchDatabaseEntry) folder = NULL;

        // db_index: the database index this folder belongs to
        uint16_t db_index = 0;
        cursor_read(&cursor, &db_index, sizeof(db_index));

        database_file_load_entry(&cursor,
                                 index_flags,
                                 previous_entry_name,
                                 &folder,
                                 DATABASE_ENTRY_TYPE_FOLDER);
        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        cursor_read(&cursor, &parent_idx, sizeof(parent_idx));

        if (cursor.error) {
            g_debug("[db_load] out of bounds read detected at folder idx: %d", idx);
            return false;
        }

        if (parent_idx != UINT32_MAX && parent_idx >= num_folders) {
            g_debug("[db_load] Corrupt parent index: %d", parent_idx);
            return false;
        }

        if (index_flags & DATABASE_INDEX_PROPERTY_FLAG_DB_INDEX) {
            db_entry_set_db_index(folder, db_index);
        }

        if (parent_idx != idx) {
            // Until all folders are ready, we have to reference parents by their index
            db_entry_set_parent_no_update(folder, GUINT_TO_POINTER(parent_idx));
        }
        else {
            // parent_idx and idx are the same (i.e., folder is a root index), so it has no parent
            db_entry_set_parent_no_update(folder, GUINT_TO_POINTER(UINT32_MAX));
        }
        darray_add_item(folders, g_steal_pointer(&folder));
    }

    // fail if we didn't read the correct number of folders
    if (idx != num_folders) {
        g_debug("[db_load] failed to read folders (read %d of %d)", idx, num_folders);
        return false;
    }
    // Did the parsed data exactly match the declared block size?
    if (cursor.ptr != cursor.end) {
        g_debug("[db_load] block size mismatch! Parsed %zd bytes, expected %" PRIu64,
                (size_t)(cursor.ptr - folder_block),
                folder_block_size);
        return false;
    }

    return true;
}

static bool
database_file_load_files(FILE *fp,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         DynamicArray *folders,
                         DynamicArray *files,
                         uint32_t num_files,
                         uint64_t file_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(1024);
    g_autofree uint8_t *file_block = calloc(file_block_size + 1, sizeof(uint8_t));
    g_return_val_if_fail(file_block, false);

    if (fread(file_block, sizeof(uint8_t), file_block_size, fp) != file_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    const uint32_t num_folders = darray_get_num_items(folders);

    // Initialize the cursor with the allocated block
    DatabaseFileReadCursor cursor = {file_block, file_block + file_block_size, false};

    // load files
    uint32_t idx = 0;
    for (idx = 0; idx < num_files; idx++) {
        g_autoptr(FsearchDatabaseEntry) entry = NULL;
        database_file_load_entry(&cursor,
                                 index_flags,
                                 previous_entry_name,
                                 &entry,
                                 DATABASE_ENTRY_TYPE_FILE);

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        cursor_read(&cursor, &parent_idx, sizeof(parent_idx));

        if (cursor.error) {
            g_debug("[db_load] out of bounds read detected at file idx: %d", idx);
            return false;
        }

        if (parent_idx != UINT32_MAX && parent_idx >= num_folders) {
            g_debug("[db_load] Corrupt parent index: %d", parent_idx);
            return false;
        }

        FsearchDatabaseEntry *parent = darray_get_item(folders, parent_idx);
        db_entry_set_parent(entry, parent);

        darray_add_item(files, g_steal_pointer(&entry));
    }
    // fail if we didn't read the correct number of files
    if (idx != num_files) {
        g_debug("[db_load] failed to read all files (read %d of %d)", idx, num_files);
        return false;
    }

    // Did the parsed data exactly match the declared block size?
    if (cursor.ptr != cursor.end) {
        g_debug("[db_load] block size mismatch! Parsed %zd bytes, expected %" PRIu64,
                (size_t)(cursor.ptr - file_block),
                file_block_size);
        return false;
    }

    return true;
}

static bool
database_file_load_sorted_entries(FILE *fp, DynamicArray *src, uint32_t num_src_entries, DynamicArray *dest) {
    g_autofree uint32_t *indexes = calloc(num_src_entries + 1, sizeof(uint32_t));
    g_assert(indexes);

    if (fread(indexes, 4, num_src_entries, fp) != num_src_entries) {
        return false;
    }

    for (uint32_t i = 0; i < num_src_entries; i++) {
        uint32_t idx = indexes[i];
        void *entry = darray_get_item(src, idx);
        if (!entry) {
            return false;
        }
        darray_add_item(dest, entry);
    }

    return true;
}

static bool
database_file_load_sorted_arrays(FILE *fp, DynamicArray **sorted_folders, DynamicArray **sorted_files) {
    uint32_t num_sorted_arrays = 0;

    DynamicArray *files = sorted_files[DATABASE_INDEX_PROPERTY_NAME];
    DynamicArray *folders = sorted_folders[DATABASE_INDEX_PROPERTY_NAME];

    if (!database_file_read_element(&num_sorted_arrays, 4, fp, NULL)) {
        g_debug("[db_load] failed to load number of sorted arrays");
        return false;
    }

    for (uint32_t i = 0; i < num_sorted_arrays; i++) {
        uint32_t sorted_array_id = 0;
        if (!database_file_read_element(&sorted_array_id, 4, fp, NULL)) {
            g_debug("[db_load] failed to load sorted array id");
            return false;
        }

        if (sorted_array_id < 1 || sorted_array_id >= NUM_DATABASE_INDEX_PROPERTIES) {
            g_debug("[db_load] sorted array id is not supported: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_folders = darray_get_num_items(folders);
        sorted_folders[sorted_array_id] = darray_new(num_folders);
        if (!database_file_load_sorted_entries(fp, folders, num_folders, sorted_folders[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted folder indexes: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_files = darray_get_num_items(files);
        sorted_files[sorted_array_id] = darray_new(num_files);
        if (!database_file_load_sorted_entries(fp, files, num_files, sorted_files[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted file indexes: %d", sorted_array_id);
            return false;
        }
    }

    return true;
}

static char *
database_file_read_string(FILE *fp, size_t max_size, GChecksum *checksum) {
    uint32_t string_len = 0;
    if (!database_file_read_element(&string_len, sizeof(string_len), fp, checksum)) {
        return NULL;
    }
    if (string_len > max_size) {
        return NULL;
    }

    g_autofree char *path = calloc(string_len + 1, sizeof(char));
    if (!database_file_read_element(path, string_len, fp, checksum)) {
        return NULL;
    }
    return g_steal_pointer(&path);
}

static bool
database_file_load_includes(FILE *fp, FsearchDatabaseIncludeManager *include_manager, GChecksum *checksum) {
    uint32_t num_includes = 0;
    if (!database_file_read_element(&num_includes, sizeof(num_includes), fp, checksum)) {
        g_debug("[db_load] failed to read number of includes");
        return false;
    }

    for (int i = 0; i < num_includes; ++i) {
        uint32_t type = 0;
        if (!database_file_read_element(&type, sizeof(type), fp, checksum)) {
            g_debug("[db_load] failed to read type of include");
            return false;
        }

        int32_t id = 0;
        if (!database_file_read_element(&id, sizeof(id), fp, checksum)) {
            g_debug("[db_load] failed to read id of include");
            return false;
        }

        g_autofree char *path = database_file_read_string(fp, 4 * PATH_MAX, checksum);
        if (!path) {
            g_debug("[db_load] failed to read path of include");
            return false;
        }

        uint8_t one_file_system = 0;
        if (!database_file_read_element(&one_file_system, sizeof(one_file_system), fp, checksum)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t is_active = 0;
        if (!database_file_read_element(&is_active, sizeof(is_active), fp, checksum)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t is_monitored = 0;
        if (!database_file_read_element(&is_monitored, sizeof(is_monitored), fp, checksum)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t scan_after_launch = 0;
        if (!database_file_read_element(&scan_after_launch, sizeof(scan_after_launch), fp, checksum)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        g_autoptr(FsearchDatabaseInclude) include = fsearch_database_include_new(
            path,
            is_active,
            one_file_system,
            is_monitored,
            scan_after_launch,
            id);
        fsearch_database_include_manager_add(include_manager, include);
    }
    return true;
}

static bool
database_file_load_excludes(FILE *fp, FsearchDatabaseExcludeManager *exclude_manager, GChecksum *checksum) {
    uint32_t num_excludes = 0;
    if (!database_file_read_element(&num_excludes, sizeof(num_excludes), fp, checksum)) {
        g_debug("[db_load] failed to read number of excludes");
        return false;
    }

    for (int i = 0; i < num_excludes; ++i) {
        uint32_t record_type = 0;
        if (!database_file_read_element(&record_type, sizeof(record_type), fp, checksum)) {
            g_debug("[db_load] failed to read type of exclude");
            return false;
        }

        uint8_t exclude_type = FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED;
        uint8_t scope = FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH;
        uint8_t target = FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH;
        if (!database_file_read_element(&exclude_type, sizeof(exclude_type), fp, checksum)) {
            g_debug("[db_load] failed to read exclude type");
            return false;
        }
        if (!database_file_read_element(&scope, sizeof(scope), fp, checksum)) {
            g_debug("[db_load] failed to read exclude scope");
            return false;
        }
        if (!database_file_read_element(&target, sizeof(target), fp, checksum)) {
            g_debug("[db_load] failed to read exclude target");
            return false;
        }

        g_autofree char *pattern = database_file_read_string(fp, 4 * PATH_MAX, checksum);
        if (!pattern) {
            g_debug("[db_load] failed to read pattern of exclude");
            return false;
        }

        uint8_t is_active = 0;
        if (!database_file_read_element(&is_active, sizeof(is_active), fp, checksum)) {
            g_debug("[db_load] failed to read path_len of exclude");
            return false;
        }

        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(pattern,
            is_active,
            (FsearchDatabaseExcludeType)exclude_type,
            (FsearchDatabaseExcludeMatchScope)scope,
            (FsearchDatabaseExcludeTarget)target);
        fsearch_database_exclude_manager_add(exclude_manager, exclude);
    }

    uint8_t exclude_hidden = 0;
    if (!database_file_read_element(&exclude_hidden, sizeof(exclude_hidden), fp, checksum)) {
        g_debug("[db_load] failed to read exclude hidden setting");
        return false;
    }
    fsearch_database_exclude_manager_set_exclude_hidden(exclude_manager, exclude_hidden);

    return true;
}

// endregion

// region Database-File-Write

static void
database_file_save_entry(DatabaseFileWriteCursor *cursor,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         FsearchDatabaseEntry *entry,
                         uint32_t parent_idx,
                         GString *previous_entry_name,
                         GString *new_entry_name) {
    // init new_entry_name with the name of the current entry
    g_string_assign(new_entry_name, db_entry_get_name_raw(entry));

    // name_offset: character position after which previous_entry_name and new_entry_name differ
    const uint16_t name_offset = get_name_offset(previous_entry_name->str, new_entry_name->str);
    cursor_write(cursor, &name_offset, sizeof(name_offset));

    // name_len: length of the new name characters
    const uint16_t name_len = new_entry_name->len - name_offset;
    cursor_write(cursor, &name_len, sizeof(name_len));

    // append new unique characters to previous_entry_name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);
    g_string_append(previous_entry_name, new_entry_name->str + name_offset);

    if (name_len > 0) {
        // name: new characters to be written to file
        const char *name = previous_entry_name->str + name_offset;
        cursor_write(cursor, name, name_len);
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        // size: file or folder size (folder size: sum of all children sizes)
        const uint64_t size = db_entry_get_size(entry);
        cursor_write(cursor, &size, sizeof(size));
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time of file/folder
        const uint64_t mtime = db_entry_get_mtime(entry);
        cursor_write(cursor, &mtime, sizeof(mtime));
    }

    // parent_idx: index of parent folder
    cursor_write(cursor, &parent_idx, sizeof(parent_idx));
}

static void
database_file_save_header(DatabaseFileWriteCursor *cursor) {
    const char magic[] = DATABASE_MAGIC_NUMBER;
    cursor_write(cursor, magic, strlen(magic));

    const uint8_t majorver = DATABASE_MAJOR_VERSION;
    cursor_write(cursor, &majorver, sizeof(majorver));

    const uint8_t minorver = DATABASE_MINOR_VERSION;
    cursor_write(cursor, &minorver, sizeof(minorver));

    const uint8_t is_little_endian = G_BYTE_ORDER == G_LITTLE_ENDIAN ? 1 : 0;
    cursor_write(cursor, &is_little_endian, sizeof(is_little_endian));
}

static void
database_file_save_files(DatabaseFileWriteCursor *cursor,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         DynamicArray *files,
                         uint32_t num_files) {
    g_autoptr(GString) name_prev = g_string_sized_new(1024);
    g_autoptr(GString) name_new = g_string_sized_new(1024);

    for (uint32_t i = 0; i < num_files; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(files, i);

        db_entry_set_index(entry, i);
        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = db_entry_get_index(parent);
        database_file_save_entry(cursor,
                                 index_flags,
                                 entry,
                                 parent_idx,
                                 name_prev,
                                 name_new);
        if (cursor->error) {
            return;
        }
    }
}

static uint32_t *
build_sorted_entry_index_list(DynamicArray *entries, uint32_t num_entries) {
    if (num_entries < 1) {
        return NULL;
    }
    uint32_t *indexes = calloc(num_entries + 1, sizeof(uint32_t));
    g_assert(indexes);

    for (int i = 0; i < num_entries; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        indexes[i] = db_entry_get_index(entry);
    }
    return indexes;
}

static void
database_file_save_sorted_entries(DatabaseFileWriteCursor *cursor,
                                  DynamicArray *entries,
                                  uint32_t num_entries) {
    if (num_entries < 1) {
        // nothing to write, we're done here
        return;
    }

    g_autofree uint32_t *sorted_entry_index_list = build_sorted_entry_index_list(entries, num_entries);
    if (!sorted_entry_index_list) {
        cursor->error = true;
        g_debug("[db_save] failed to create sorted index list");
        return;
    }

    cursor_write(cursor, sorted_entry_index_list, sizeof(uint32_t) * num_entries);
}

static void
database_file_save_sorted_arrays(DatabaseFileWriteCursor *cursor,
                                 FsearchDatabaseIndexStore *store,
                                 uint32_t num_files,
                                 uint32_t num_folders) {
    uint32_t num_sorted_arrays = fsearch_database_index_store_get_num_fast_sort_indices(store);

    cursor_write(cursor, &num_sorted_arrays, sizeof(num_sorted_arrays));

    if (num_sorted_arrays < 1 || cursor->error) {
        return;
    }

    for (uint32_t id = DATABASE_INDEX_PROPERTY_NAME; id < NUM_DATABASE_INDEX_PROPERTIES; id++) {
        g_autoptr(FsearchDatabaseEntriesContainer) folder_container =
            fsearch_database_index_store_get_folders(store, id);
        g_autoptr(FsearchDatabaseEntriesContainer) file_container = fsearch_database_index_store_get_files(store, id);
        if (!folder_container || !file_container) {
            continue;
        }
        g_autoptr(DynamicArray) folders = fsearch_database_entries_container_get_joined(folder_container);
        g_autoptr(DynamicArray) files = fsearch_database_entries_container_get_joined(file_container);
        if (!files || !folders) {
            continue;
        }

        // id: this is the id of the sorted files
        cursor_write(cursor, &id, sizeof(id));

        database_file_save_sorted_entries(cursor, folders, num_folders);
        if (cursor->error) {
            g_debug("[db_save] failed to save sorted folders");
            return;
        }
        database_file_save_sorted_entries(cursor, files, num_files);
        if (cursor->error) {
            g_debug("[db_save] failed to save sorted files");
            return;
        }
    }
}

static void
database_file_save_folders(DatabaseFileWriteCursor *cursor,
                           FsearchDatabaseIndexPropertyFlags index_flags,
                           DynamicArray *folders,
                           uint32_t num_folders) {
    g_autoptr(GString) name_prev = g_string_sized_new(1024);
    g_autoptr(GString) name_new = g_string_sized_new(1024);

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(folders, i);

        const uint16_t db_index = db_entry_get_db_index(entry);
        cursor_write(cursor, &db_index, sizeof(db_index));

        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = parent ? db_entry_get_index(parent) : db_entry_get_index(entry);
        database_file_save_entry(cursor,
                                 index_flags,
                                 entry,
                                 parent_idx,
                                 name_prev,
                                 name_new);
        if (cursor->error) {
            return;
        }
    }

    return;
}

static void
database_file_save_includes(DatabaseFileWriteCursor *cursor,
                            FsearchDatabaseIndexStore *store) {
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_index_store_get_include_manager(store);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    const uint32_t num_includes = includes->len;
    cursor_write(cursor, &num_includes, sizeof(num_includes));

    for (int i = 0; i < num_includes; ++i) {
        if (cursor->error) {
            g_debug("[db_save] failed to save includes");
            return;
        }
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);

        const uint32_t type = 0;
        cursor_write(cursor, &type, sizeof(type));

        const int32_t id = fsearch_database_include_get_id(include);
        cursor_write(cursor, &id, sizeof(id));

        const char *path = fsearch_database_include_get_path(include);
        const uint32_t path_len = strlen(path);
        cursor_write_string(cursor, path, path_len);

        const uint8_t one_file_system = fsearch_database_include_get_one_file_system(include);
        cursor_write(cursor, &one_file_system, sizeof(one_file_system));

        const uint8_t is_active = fsearch_database_include_get_active(include);
        cursor_write(cursor, &is_active, sizeof(is_active));

        const uint8_t is_monitored = fsearch_database_include_get_monitored(include);
        cursor_write(cursor, &is_monitored, sizeof(is_monitored));

        const uint8_t scan_after_launch = fsearch_database_include_get_scan_after_launch(include);
        cursor_write(cursor, &scan_after_launch, sizeof(scan_after_launch));
    }
}

static void
database_file_save_excludes(DatabaseFileWriteCursor *cursor,
                            FsearchDatabaseIndexStore *store) {
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_index_store_get_exclude_manager(store);
    g_autoptr(GPtrArray) excludes = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    const uint32_t num_excludes = excludes->len;
    cursor_write(cursor, &num_excludes, sizeof(num_excludes));

    for (int i = 0; i < num_excludes; ++i) {
        if (cursor->error) {
            g_debug("[db_save] failed to save excludes");
            return;
        }
        FsearchDatabaseExclude *exclude = g_ptr_array_index(excludes, i);

        const uint32_t record_type = 0;
        cursor_write(cursor, &record_type, sizeof(record_type));

        const uint8_t exclude_type = fsearch_database_exclude_get_exclude_type(exclude);
        cursor_write(cursor, &exclude_type, sizeof(exclude_type));

        const uint8_t match_scope = fsearch_database_exclude_get_match_scope(exclude);
        cursor_write(cursor, &match_scope, sizeof(match_scope));

        const uint8_t target = fsearch_database_exclude_get_target(exclude);
        cursor_write(cursor, &target, sizeof(target));

        const char *pattern = fsearch_database_exclude_get_pattern(exclude);
        const uint32_t pattern_len = strlen(pattern);
        cursor_write_string(cursor, pattern, pattern_len);

        const uint8_t is_active = fsearch_database_exclude_get_active(exclude);
        cursor_write(cursor, &is_active, sizeof(is_active));
    }

    const uint8_t exclude_hidden = fsearch_database_exclude_manager_get_exclude_hidden(exclude_manager);
    cursor_write(cursor, &exclude_hidden, sizeof(exclude_hidden));
}

// endregion

bool
fsearch_database_file_save(FsearchDatabaseIndexStore *store, const char *file_path) {
    g_return_val_if_fail(file_path, false);
    g_return_val_if_fail(store, false);

    g_debug("[db_save] saving database to file...");

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    g_autoptr(GString) file_tmp_path = g_string_new(file_path);
    g_string_append(file_tmp_path, ".tmp");

    g_autoptr(FsearchDatabaseEntriesContainer) folder_container = NULL;
    g_autoptr(FsearchDatabaseEntriesContainer) file_container = NULL;

    g_autoptr(DynamicArray) files = NULL;
    g_autoptr(DynamicArray) folders = NULL;

    g_debug("[db_save] trying to open temporary database file: %s", file_tmp_path->str);

    g_autoptr(FILE) fp = file_open_locked(file_tmp_path->str, "wb");

    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_MD5);
    DatabaseFileWriteCursor cursor = {.fp = fp, .error = false, .bytes_written = 0, .checksum = checksum};

    if (!fp) {
        g_debug("[db_save] failed to open temporary database file: %s", file_tmp_path->str);
        goto save_fail;
    }

    g_debug("[db_save] saving database header...");
    database_file_save_header(&cursor);
    if (cursor.error == true) {
        g_debug("[db_save] failed saving header");
        goto save_fail;
    }

    g_debug("[db_save] saving database index flags...");
    const uint64_t index_flags = fsearch_database_index_store_get_flags(store);
    cursor_write(&cursor, &index_flags, sizeof(index_flags));
    if (cursor.error == true) {
        g_debug("[db_save] failed saving index flags");
        goto save_fail;
    }

    g_debug("[db_save] saving indices...");
    database_file_save_includes(&cursor, store);
    if (cursor.error == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving excludes...");
    database_file_save_excludes(&cursor, store);
    if (cursor.error == true) {
        goto save_fail;
    }

    g_debug("[db_save] updating folder indices...");
    folder_container = fsearch_database_index_store_get_folders(store, DATABASE_INDEX_PROPERTY_NAME);
    folders = fsearch_database_entries_container_get_joined(folder_container);
    update_folder_indices(folders);

    const uint32_t num_folders = darray_get_num_items(folders);
    cursor_write(&cursor, &num_folders, sizeof(num_folders));
    if (cursor.error == true) {
        g_debug("[db_save] failed saving number of folders: %d", num_folders);
        goto save_fail;
    }

    file_container = fsearch_database_index_store_get_files(store, DATABASE_INDEX_PROPERTY_NAME);
    files = fsearch_database_entries_container_get_joined(file_container);

    const uint32_t num_files = darray_get_num_items(files);
    cursor_write(&cursor, &num_files, sizeof(num_files));
    if (cursor.error == true) {
        g_debug("[db_save] failed saving number of files: %d", num_files);
        goto save_fail;
    }

    // Set checksum to NULL. For performance reasons we don't want to compute the checksum of the larger arrays.
    // It also must be set to NULL before writing placeholder data for folder and file block sizes and checksum itself
    cursor.checksum = NULL;

    uint64_t folder_block_size = 0;
    const uint64_t folder_block_size_offset = cursor.bytes_written;
    g_debug("[db_save] saving folder block size...");
    cursor_write(&cursor, &folder_block_size, sizeof(folder_block_size));
    if (cursor.error == true) {
        g_debug("[db_save] failed saving folder block size");
        goto save_fail;
    }

    uint64_t file_block_size = 0;
    const uint64_t file_block_size_offset = cursor.bytes_written;
    g_debug("[db_save] saving file block size...");
    cursor_write(&cursor, &file_block_size, sizeof(file_block_size));
    if (cursor.error == true) {
        g_debug("[db_save] failed saving file block size");
        goto save_fail;
    }

    // Store placeholder for checksum
    const uint64_t checksum_offset = cursor.bytes_written;
    uint8_t checksum_placeholder[DATABASE_CHECKSUM_SIZE] = {};
    cursor_write(&cursor, checksum_placeholder, sizeof(checksum_placeholder));

    g_debug("[db_save] saving folders...");
    uint64_t current_cursor_size = cursor.bytes_written;
    database_file_save_folders(&cursor, index_flags, folders, num_folders);
    if (cursor.error == true) {
        goto save_fail;
    }
    folder_block_size = cursor.bytes_written - current_cursor_size;

    g_debug("[db_save] saving files...");
    current_cursor_size = cursor.bytes_written;
    database_file_save_files(&cursor, index_flags, files, num_files);
    if (cursor.error == true) {
        goto save_fail;
    }
    file_block_size = cursor.bytes_written - current_cursor_size;

    g_debug("[db_save] saving sorted arrays...");
    database_file_save_sorted_arrays(&cursor, store, num_files, num_folders);
    if (cursor.error == true) {
        goto save_fail;
    }

    // now that we know the size of the file/folder block we've written, store it in the file header
    // Make also sure to set the cursor checksum again, so folder and file block size are hashed as well
    cursor.checksum = checksum;
    if (fseeko(fp, (off64_t)folder_block_size_offset, SEEK_SET) != 0) {
        goto save_fail;
    }
    g_debug("[db_save] updating file and folder block size: %" PRIu64 ", %" PRIu64, folder_block_size, file_block_size);
    cursor_write(&cursor, &folder_block_size, sizeof(folder_block_size));
    cursor_write(&cursor, &file_block_size, sizeof(file_block_size));

    // after writing the folder and file block size, we can
    if (fseeko(fp, (off64_t)checksum_offset, SEEK_SET) != 0) {
        goto save_fail;
    }
    gsize checksum_buffer_len = DATABASE_CHECKSUM_SIZE;
    uint8_t checksum_bytes[DATABASE_CHECKSUM_SIZE] = {};
    g_checksum_get_digest(checksum, checksum_bytes, &checksum_buffer_len);

    // Before writing checksum, unset it again.
    cursor.checksum = NULL;
    cursor_write(&cursor, checksum_bytes, sizeof(checksum_bytes));

    if (cursor.error == true) {
        goto save_fail;
    }

    g_debug("[db_save] removing current database file...");
    // remove current database file
    unlink(file_path);

    g_clear_pointer(&fp, fclose);

    g_debug("[db_save] renaming temporary database file: %s -> %s", file_tmp_path->str, file_path);
    // rename temporary fsearch.db.tmp to fsearch.db
    if (rename(file_tmp_path->str, file_path) != 0) {
        goto save_fail;
    }

    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_stop(timer);

    g_debug("[db_save] database file saved in: %f ms", seconds * 1000);

    return true;

save_fail:
    g_warning("[db_save] saving failed");

    // remove temporary fsearch.db.tmp file
    unlink(file_tmp_path->str);

    return false;
}

bool
fsearch_database_file_load_config(const char *file_path,
                                  FsearchDatabaseIncludeManager **include_manager_out,
                                  FsearchDatabaseExcludeManager **exclude_manager_out,
                                  FsearchDatabaseIndexPropertyFlags *flags_out) {
    g_return_val_if_fail(file_path, false);

    g_autoptr(FILE) fp = file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_MD5);

    if (!database_file_load_header(fp, checksum)) {
        goto load_fail;
    }

    uint64_t index_flags = 0;
    if (!database_file_read_element(&index_flags, sizeof(index_flags), fp, checksum)) {
        g_debug("[db_load] failed to read index flags");
        goto load_fail;
    }

    if (!database_file_load_includes(fp, include_manager, checksum)) {
        g_debug("[db_load] failed to load includes");
        goto load_fail;
    }
    if (!database_file_load_excludes(fp, exclude_manager, checksum)) {
        g_debug("[db_load] excludes not loaded");
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (!database_file_read_element(&num_folders, sizeof(num_folders), fp, checksum)) {
        g_debug("[db_load] failed to read num_folders");
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (!database_file_read_element(&num_files, sizeof(num_files), fp, checksum)) {
        g_debug("[db_load] failed to read num_files");
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    uint64_t folder_block_size = 0;
    if (!database_file_read_element(&folder_block_size, sizeof(folder_block_size), fp, checksum)) {
        g_debug("[db_load] failed to read folder block size");
        goto load_fail;
    }

    uint64_t file_block_size = 0;
    if (!database_file_read_element(&file_block_size, sizeof(file_block_size), fp, checksum)) {
        g_debug("[db_load] loading file block size: %" PRIu64, file_block_size);
        goto load_fail;
    }
    g_debug("[db_load] folder size: %" PRIu64 ", file size: %" PRIu64, folder_block_size, file_block_size);

    gsize checksum_computed_len = DATABASE_CHECKSUM_SIZE;
    uint8_t checksum_computed[DATABASE_CHECKSUM_SIZE] = {};
    g_checksum_get_digest(checksum, checksum_computed, &checksum_computed_len);

    uint8_t checksum_stored[DATABASE_CHECKSUM_SIZE] = {};
    if (!database_file_read_element(checksum_stored, sizeof(checksum_stored), fp, NULL)) {
        g_debug("[db_load] loading checksum failed");
        goto load_fail;
    }

    if (memcmp(checksum_stored, checksum_computed, DATABASE_CHECKSUM_SIZE) != 0) {
        g_warning("[db_load] Database Metadata Corrupted! MD5 Checksum mismatch.");
        goto load_fail;
    }

    *include_manager_out = g_steal_pointer(&include_manager);
    *exclude_manager_out = g_steal_pointer(&exclude_manager);
    *flags_out = index_flags;

    g_clear_pointer(&fp, fclose);

    return true;

load_fail:
    g_debug("[db_load] load failed");

    return false;

}

bool
fsearch_database_file_load(const char *file_path,
                           void (*status_cb)(const char *),
                           FsearchDatabaseIndexStore **store_out,
                           FsearchDatabaseIndexStoreEventFunc event_func,
                           void *event_func_user_data) {
    g_return_val_if_fail(file_path, false);
    g_return_val_if_fail(store_out, false);

    g_autoptr(FILE) fp = file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    DynamicArray *folders = NULL;
    DynamicArray *files = NULL;
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_PROPERTIES] = {NULL};
    DynamicArray *sorted_files[NUM_DATABASE_INDEX_PROPERTIES] = {NULL};
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    g_autoptr(GPtrArray) indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    g_autoptr(GHashTable) folder_index_arrays = g_hash_table_new_full(g_direct_hash,
                                                                      g_direct_equal,
                                                                      NULL,
                                                                      (GDestroyNotify)darray_unref);
    g_autoptr(GHashTable) file_index_arrays = g_hash_table_new_full(g_direct_hash,
                                                                    g_direct_equal,
                                                                    NULL,
                                                                    (GDestroyNotify)darray_unref);

    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_MD5);

    if (!database_file_load_header(fp, checksum)) {
        goto load_fail;
    }

    uint64_t index_flags = 0;
    if (!database_file_read_element(&index_flags, sizeof(index_flags), fp, checksum)) {
        g_debug("[db_load] failed to read index flags");
        goto load_fail;
    }

    if (!database_file_load_includes(fp, include_manager, checksum)) {
        g_debug("[db_load] failed to load includes");
        goto load_fail;
    }
    if (!database_file_load_excludes(fp, exclude_manager, checksum)) {
        g_debug("[db_load] excludes not loaded");
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (!database_file_read_element(&num_folders, sizeof(num_folders), fp, checksum)) {
        g_debug("[db_load] failed to read num_folders");
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (!database_file_read_element(&num_files, sizeof(num_files), fp, checksum)) {
        g_debug("[db_load] failed to read num_files");
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    uint64_t folder_block_size = 0;
    if (!database_file_read_element(&folder_block_size, sizeof(folder_block_size), fp, checksum)) {
        g_debug("[db_load] failed to read folder block size");
        goto load_fail;
    }

    uint64_t file_block_size = 0;
    if (!database_file_read_element(&file_block_size, sizeof(file_block_size), fp, checksum)) {
        g_debug("[db_load] loading file block size: %" PRIu64, file_block_size);
        goto load_fail;
    }
    g_debug("[db_load] folder size: %" PRIu64 ", file size: %" PRIu64, folder_block_size, file_block_size);

    gsize checksum_computed_len = DATABASE_CHECKSUM_SIZE;
    uint8_t checksum_computed[DATABASE_CHECKSUM_SIZE] = {};
    g_checksum_get_digest(checksum, checksum_computed, &checksum_computed_len);

    uint8_t checksum_stored[DATABASE_CHECKSUM_SIZE] = {};
    if (!database_file_read_element(checksum_stored, sizeof(checksum_stored), fp, NULL)) {
        g_debug("[db_load] loading checksum failed");
        goto load_fail;
    }

    if (memcmp(checksum_stored, checksum_computed, DATABASE_CHECKSUM_SIZE) != 0) {
        g_warning("[db_load] Database Metadata Corrupted! MD5 Checksum mismatch.");
        goto load_fail;
    }

    // pre-allocate the folders array, so we can later map parent indices to the corresponding pointers
    sorted_folders[DATABASE_INDEX_PROPERTY_NAME] = darray_new(num_folders);
    folders = sorted_folders[DATABASE_INDEX_PROPERTY_NAME];

    if (status_cb) {
        status_cb(_("Loading folders…"));
    }
    // load folders
    if (!database_file_load_folders(fp, index_flags, folders, num_folders, folder_block_size)) {
        g_debug("[db_load] failed to load folders");
        goto load_fail;
    }
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders, i);
        const uint32_t parent_idx = GPOINTER_TO_UINT(db_entry_get_parent(folder));
        db_entry_set_parent_no_update(folder, parent_idx == UINT32_MAX ? NULL : darray_get_item(folders, parent_idx));
    }

    if (status_cb) {
        status_cb(_("Loading files…"));
    }
    // load files
    sorted_files[DATABASE_INDEX_PROPERTY_NAME] = darray_new(num_files);
    files = sorted_files[DATABASE_INDEX_PROPERTY_NAME];
    if (!database_file_load_files(fp, index_flags, folders, files, num_files, file_block_size)) {
        g_debug("[db_load] failed to load files");
        goto load_fail;
    }

    if (!database_file_load_sorted_arrays(fp, sorted_folders, sorted_files)) {
        g_debug("[db_load] failed to load sorted arrays");
        goto load_fail;
    }

    DynamicArray *folders_sorted_by_path = sorted_folders[DATABASE_INDEX_PROPERTY_PATH];
    for (uint32_t i = 0; i < darray_get_num_items(folders_sorted_by_path); i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders_sorted_by_path, i);
        database_file_load_add_to_index_array(folder_index_arrays, folder);
    }
    DynamicArray *files_sorted_by_path = sorted_files[DATABASE_INDEX_PROPERTY_PATH];
    for (uint32_t i = 0; i < darray_get_num_items(files_sorted_by_path); i++) {
        FsearchDatabaseEntry *file = darray_get_item(files_sorted_by_path, i);
        database_file_load_add_to_index_array(file_index_arrays, file);
    }

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        const uint32_t id = fsearch_database_include_get_id(include);
        DynamicArray *folder_array_index = g_hash_table_lookup(folder_index_arrays, GINT_TO_POINTER(id));
        DynamicArray *file_array_index = g_hash_table_lookup(file_index_arrays, GINT_TO_POINTER(id));
        if (folder_array_index && file_array_index) {
            FsearchDatabaseIndex *index = fsearch_database_index_new_with_content(
                id,
                include,
                exclude_manager,
                folder_array_index,
                file_array_index,
                index_flags);
            g_ptr_array_add(indices, index);
        }
    }
    *store_out = fsearch_database_index_store_new_with_content(indices,
                                                               sorted_files,
                                                               sorted_folders,
                                                               include_manager,
                                                               exclude_manager,
                                                               index_flags,
                                                               event_func,
                                                               event_func_user_data);

    g_clear_pointer(&fp, fclose);

    return true;

load_fail:
    g_debug("[db_load] load failed");

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; i++) {
        g_clear_pointer(&sorted_folders[i], darray_unref);
        g_clear_pointer(&sorted_files[i], darray_unref);
    }
    return false;
}
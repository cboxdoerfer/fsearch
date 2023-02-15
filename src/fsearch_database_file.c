#include "fsearch_database_file.h"

#include "fsearch_database_entry.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <sys/file.h>

#define DATABASE_MAJOR_VERSION 0
#define DATABASE_MINOR_VERSION 9
#define DATABASE_MAGIC_NUMBER "FSDB"

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

static void
update_folder_indices(FsearchDatabaseIndex *index) {
    if (!index || !index->folders[DATABASE_INDEX_TYPE_NAME]) {
        return;
    }
    const uint32_t num_folders = darray_get_num_items(index->folders[DATABASE_INDEX_TYPE_NAME]);
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntryFolder *folder = darray_get_item(index->folders[DATABASE_INDEX_TYPE_NAME], i);
        if (!folder) {
            continue;
        }
        db_entry_set_idx((FsearchDatabaseEntry *)folder, i);
    }
}

static uint8_t
get_name_offset(const char *old, const char *new) {
    if (!old || !new) {
        return 0;
    }

    uint8_t offset = 0;
    while (old[offset] == new[offset] && old[offset] != '\0' && new[offset] != '\0' && offset < 255) {
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

static const uint8_t *
copy_bytes_and_return_new_src(void *dest, const uint8_t *src, size_t len) {
    memcpy(dest, src, len);
    return src + len;
}

static const uint8_t *
load_entry_super_elements_from_memory(const uint8_t *data_block,
                                         FsearchDatabaseIndexFlags index_flags,
                                         FsearchDatabaseEntry *entry,
                                         GString *previous_entry_name) {
    // name_offset: character position after which previous_entry_name and entry_name differ
    uint8_t name_offset = *data_block++;

    // name_len: length of the new name characters
    uint8_t name_len = *data_block++;

    // erase previous name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);

    char name[256] = "";
    // name: new characters to be appended to previous_entry_name
    if (name_len > 0) {
        data_block = copy_bytes_and_return_new_src(name, data_block, name_len);
        name[name_len] = '\0';
    }

    // now we can build the new full file name
    g_string_append(previous_entry_name, name);
    db_entry_set_name(entry, previous_entry_name->str);

    if ((index_flags & DATABASE_INDEX_FLAG_SIZE) != 0) {
        // size: size of file/folder
        off_t size = 0;
        data_block = copy_bytes_and_return_new_src(&size, data_block, 8);

        db_entry_set_size(entry, size);
    }

    if ((index_flags & DATABASE_INDEX_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time file/folder
        time_t mtime = 0;
        data_block = copy_bytes_and_return_new_src(&mtime, data_block, 8);

        db_entry_set_mtime(entry, mtime);
    }

    return data_block;
}

static bool
read_element_from_file(void *restrict ptr, size_t size, FILE *restrict stream) {
    return fread(ptr, size, 1, stream) == 1 ? true : false;
}

static bool
load_entry_super_elements(FILE *fp, FsearchDatabaseEntry *entry, GString *previous_entry_name) {
    // name_offset: character position after which previous_entry_name and entry_name differ
    uint8_t name_offset = 0;
    if (!read_element_from_file(&name_offset, 1, fp)) {
        g_debug("[db_load] failed to load name offset");
        return false;
    }

    // name_len: length of the new name characters
    uint8_t name_len = 0;
    if (!read_element_from_file(&name_len, 1, fp)) {
        g_debug("[db_load] failed to load name length");
        return false;
    }

    // erase previous name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);

    char name[256] = "";
    // name: new characters to be appended to previous_entry_name
    if (name_len > 0) {
        if (!read_element_from_file(name, name_len, fp)) {
            g_debug("[db_load] failed to load name");
            return false;
        }
        name[name_len] = '\0';
    }

    // now we can build the new full file name
    g_string_append(previous_entry_name, name);
    db_entry_set_name(entry, previous_entry_name->str);

    // size: size of file/folder
    uint64_t size = 0;
    if (!read_element_from_file(&size, 8, fp)) {
        g_debug("[db_load] failed to load size");
        return false;
    }
    db_entry_set_size(entry, (off_t)size);

    return true;
}

static bool
load_header(FILE *fp) {
    char magic[5] = "";
    if (!read_element_from_file(magic, strlen(DATABASE_MAGIC_NUMBER), fp)) {
        return false;
    }
    magic[4] = '\0';
    if (strcmp(magic, DATABASE_MAGIC_NUMBER) != 0) {
        g_debug("[db_load] invalid magic number: %s", magic);
        return false;
    }

    uint8_t majorver = 0;
    if (!read_element_from_file(&majorver, 1, fp)) {
        return false;
    }
    if (majorver != DATABASE_MAJOR_VERSION) {
        g_debug("[db_load] invalid major version: %d", majorver);
        g_debug("[db_load] expected major version: %d", DATABASE_MAJOR_VERSION);
        return false;
    }

    uint8_t minorver = 0;
    if (!read_element_from_file(&minorver, 1, fp)) {
        return false;
    }
    if (minorver > DATABASE_MINOR_VERSION) {
        g_debug("[db_load] invalid minor version: %d", minorver);
        g_debug("[db_load] expected minor version: <= %d", DATABASE_MINOR_VERSION);
        return false;
    }

    return true;
}

static bool
load_parent_idx(FILE *fp, uint32_t *parent_idx) {
    if (!read_element_from_file(parent_idx, 4, fp)) {
        g_debug("[db_load] failed to load parent_idx");
        return false;
    }
    return true;
}

static bool
load_folders(FILE *fp,
                FsearchDatabaseIndexFlags index_flags,
                DynamicArray *folders,
                uint32_t num_folders,
                uint64_t folder_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(256);

    g_autofree uint8_t *folder_block = calloc(folder_block_size + 1, sizeof(uint8_t));
    g_assert(folder_block);

    if (fread(folder_block, sizeof(uint8_t), folder_block_size, fp) != folder_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    const uint8_t *fb = folder_block;
    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_folders; idx++) {
        FsearchDatabaseEntryFolder *folder = darray_get_item(folders, idx);
        FsearchDatabaseEntry *entry = (FsearchDatabaseEntry *)folder;

        // TODO: db_index is currently unused
        // db_index: the database index this folder belongs to
        uint16_t db_index = 0;
        fb = copy_bytes_and_return_new_src(&db_index, fb, 2);

        fb = load_entry_super_elements_from_memory(fb, index_flags, entry, previous_entry_name);

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        fb = copy_bytes_and_return_new_src(&parent_idx, fb, 4);

        if (parent_idx != db_entry_get_idx(entry)) {
            FsearchDatabaseEntryFolder *parent = darray_get_item(folders, parent_idx);
            db_entry_set_parent(entry, parent);
        }
        else {
            // parent_idx and idx are the same (i.e. folder is a root index) so it has no parent
            db_entry_set_parent(entry, NULL);
        }
    }

    // fail if we didn't read the correct number of bytes
    if (fb - folder_block != folder_block_size) {
        g_debug("[db_load] wrong amount of memory read: %lu != %lu", fb - folder_block, folder_block_size);
        return false;
    }

    // fail if we didn't read the correct number of folders
    if (idx != num_folders) {
        g_debug("[db_load] failed to read folders (read %d of %d)", idx, num_folders);
        return false;
    }

    return true;
}

static bool
load_files(FILE *fp,
              FsearchDatabaseIndexFlags index_flags,
              FsearchMemoryPool *pool,
              DynamicArray *folders,
              DynamicArray *files,
              uint32_t num_files,
              uint64_t file_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(256);
    g_autofree uint8_t *file_block = calloc(file_block_size + 1, sizeof(uint8_t));
    g_assert(file_block);

    if (fread(file_block, sizeof(uint8_t), file_block_size, fp) != file_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    const uint8_t *fb = file_block;
    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_files; idx++) {
        FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(pool);
        db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FILE);
        db_entry_set_idx(entry, idx);

        fb = load_entry_super_elements_from_memory(fb, index_flags, entry, previous_entry_name);

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        fb = copy_bytes_and_return_new_src(&parent_idx, fb, 4);

        FsearchDatabaseEntryFolder *parent = darray_get_item(folders, parent_idx);
        db_entry_set_parent(entry, parent);

        darray_add_item(files, entry);
    }
    if (fb - file_block != file_block_size) {
        g_debug("[db_load] wrong amount of memory read: %lu != %lu", fb - file_block, file_block_size);
        return false;
    }

    // fail if we didn't read the correct number of files
    if (idx != num_files) {
        g_debug("[db_load] failed to read files (read %d of %d)", idx, num_files);
        return false;
    }

    return true;
}

static bool
load_sorted_entries(FILE *fp, DynamicArray *src, uint32_t num_src_entries, DynamicArray *dest) {

    g_autofree uint32_t *indexes = calloc(num_src_entries + 1, sizeof(uint32_t));
    g_assert(indexes);

    if (fread(indexes, 4, num_src_entries, fp) != num_src_entries) {
        return false;
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
    return true;
}

static bool
load_sorted_arrays(FILE *fp, DynamicArray **sorted_folders, DynamicArray **sorted_files) {
    uint32_t num_sorted_arrays = 0;

    DynamicArray *files = sorted_files[0];
    DynamicArray *folders = sorted_folders[0];

    if (!read_element_from_file(&num_sorted_arrays, 4, fp)) {
        g_debug("[db_load] failed to load number of sorted arrays");
        return false;
    }

    for (uint32_t i = 0; i < num_sorted_arrays; i++) {
        uint32_t sorted_array_id = 0;
        if (!read_element_from_file(&sorted_array_id, 4, fp)) {
            g_debug("[db_load] failed to load sorted array id");
            return false;
        }

        if (sorted_array_id < 1 || sorted_array_id >= NUM_DATABASE_INDEX_TYPES) {
            g_debug("[db_load] sorted array id is not supported: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_folders = darray_get_num_items(folders);
        sorted_folders[sorted_array_id] = darray_new(num_folders);
        if (!load_sorted_entries(fp, folders, num_folders, sorted_folders[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted folder indexes: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_files = darray_get_num_items(files);
        sorted_files[sorted_array_id] = darray_new(num_files);
        if (!load_sorted_entries(fp, files, num_files, sorted_files[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted file indexes: %d", sorted_array_id);
            return false;
        }
    }

    return true;
}

static size_t
write_data_to_file(FILE *fp, const void *data, size_t data_size, size_t num_elements, bool *write_failed) {
    if (data_size == 0 || num_elements == 0) {
        return 0;
    }
    if (fwrite(data, data_size, num_elements, fp) != num_elements) {
        *write_failed = true;
        return 0;
    }
    return data_size * num_elements;
}

static size_t
save_entry_super_elements(FILE *fp,
                             FsearchDatabaseIndexFlags index_flags,
                             FsearchDatabaseEntry *entry,
                             uint32_t parent_idx,
                             GString *previous_entry_name,
                             GString *new_entry_name,
                             bool *write_failed) {
    // init new_entry_name with the name of the current entry
    g_string_erase(new_entry_name, 0, -1);
    g_string_append(new_entry_name, db_entry_get_name_raw(entry));

    size_t bytes_written = 0;
    // name_offset: character position after which previous_entry_name and new_entry_name differ
    const uint8_t name_offset = get_name_offset(previous_entry_name->str, new_entry_name->str);
    bytes_written += write_data_to_file(fp, &name_offset, 1, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save name offset");
        goto out;
    }

    // name_len: length of the new name characters
    const uint8_t name_len = new_entry_name->len - name_offset;
    bytes_written += write_data_to_file(fp, &name_len, 1, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save name length");
        goto out;
    }

    // append new unique characters to previous_entry_name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);
    g_string_append(previous_entry_name, new_entry_name->str + name_offset);

    if (name_len > 0) {
        // name: new characters to be written to file
        const char *name = previous_entry_name->str + name_offset;
        bytes_written += write_data_to_file(fp, name, name_len, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save name");
            goto out;
        }
    }

    if ((index_flags & DATABASE_INDEX_FLAG_SIZE) != 0) {
        // size: file or folder size (folder size: sum of all children sizes)
        const uint64_t size = db_entry_get_size(entry);
        bytes_written += write_data_to_file(fp, &size, 8, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save size");
            goto out;
        }
    }

    if ((index_flags & DATABASE_INDEX_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time of file/folder
        const uint64_t mtime = db_entry_get_mtime(entry);
        bytes_written += write_data_to_file(fp, &mtime, 8, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save modification time");
            goto out;
        }
    }

    // parent_idx: index of parent folder
    bytes_written += write_data_to_file(fp, &parent_idx, 4, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save parent_idx");
        goto out;
    }

    out:
    return bytes_written;
}

static size_t
save_header(FILE *fp, bool *write_failed) {
    size_t bytes_written = 0;

    const char magic[] = DATABASE_MAGIC_NUMBER;
    bytes_written += write_data_to_file(fp, magic, strlen(magic), 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save magic number");
        goto out;
    }

    const uint8_t majorver = DATABASE_MAJOR_VERSION;
    bytes_written += write_data_to_file(fp, &majorver, 1, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save major version number");
        goto out;
    }

    const uint8_t minorver = DATABASE_MINOR_VERSION;
    bytes_written += write_data_to_file(fp, &minorver, 1, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save minor version number");
        goto out;
    }

    out:
    return bytes_written;
}

static size_t
save_files(FILE *fp, FsearchDatabaseIndexFlags index_flags, DynamicArray *files, uint32_t num_files, bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(GString) name_prev = g_string_sized_new(256);
    g_autoptr(GString) name_new = g_string_sized_new(256);

    for (uint32_t i = 0; i < num_files; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(files, i);

        // let's also update the idx of the file here while we're at it to make sure we have the correct
        // idx set when we store the fast sort indexes
        db_entry_set_idx(entry, i);

        FsearchDatabaseEntryFolder *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = db_entry_get_idx((FsearchDatabaseEntry *)parent);
        bytes_written += save_entry_super_elements(fp, index_flags, entry, parent_idx, name_prev, name_new, write_failed);
        if (*write_failed == true)
            return bytes_written;
    }
    return bytes_written;
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
        indexes[i] = db_entry_get_idx(entry);
    }
    return indexes;
}

static size_t
save_sorted_entries(FILE *fp, DynamicArray *entries, uint32_t num_entries, bool *write_failed) {
    if (num_entries < 1) {
        // nothing to write, we're done here
        return 0;
    }

    g_autofree uint32_t *sorted_entry_index_list = build_sorted_entry_index_list(entries, num_entries);
    if (!sorted_entry_index_list) {
        *write_failed = true;
        g_debug("[db_save] failed to create sorted index list");
        return 0;
    }

    size_t bytes_written = write_data_to_file(fp, sorted_entry_index_list, 4, num_entries, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save sorted index list");
    }

    return bytes_written;
}

static size_t
save_sorted_arrays(FILE *fp, FsearchDatabaseIndex *index, uint32_t num_files, uint32_t num_folders, bool *write_failed) {
    size_t bytes_written = 0;
    uint32_t num_sorted_arrays = 0;
    for (uint32_t i = 1; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (index->folders[i] && index->files[i]) {
            num_sorted_arrays++;
        }
    }

    bytes_written += write_data_to_file(fp, &num_sorted_arrays, 4, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save number of sorted arrays: %d", num_sorted_arrays);
        goto out;
    }

    if (num_sorted_arrays < 1) {
        goto out;
    }

    for (uint32_t id = 1; id < NUM_DATABASE_INDEX_TYPES; id++) {
        DynamicArray *folders = index->folders[id];
        DynamicArray *files = index->files[id];
        if (!files || !folders) {
            continue;
        }

        // id: this is the id of the sorted files
        bytes_written += write_data_to_file(fp, &id, 4, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save sorted arrays id: %d", id);
            goto out;
        }

        bytes_written += save_sorted_entries(fp, folders, num_folders, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save sorted folders");
            goto out;
        }
        bytes_written += save_sorted_entries(fp, files, num_files, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save sorted files");
            goto out;
        }
    }

    out:
    return bytes_written;
}

static size_t
save_folders(FILE *fp,
                FsearchDatabaseIndexFlags index_flags,
                DynamicArray *folders,
                uint32_t num_folders,
                bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(GString) name_prev = g_string_sized_new(256);
    g_autoptr(GString) name_new = g_string_sized_new(256);

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(folders, i);

        const uint16_t db_index = db_entry_get_db_index(entry);
        bytes_written += write_data_to_file(fp, &db_index, 2, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save folder's database index: %d", db_index);
            return bytes_written;
        }

        FsearchDatabaseEntryFolder *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = parent ? db_entry_get_idx((FsearchDatabaseEntry *)parent) : db_entry_get_idx(entry);
        bytes_written += save_entry_super_elements(fp, index_flags, entry, parent_idx, name_prev, name_new, write_failed);
        if (*write_failed == true) {
            return bytes_written;
        }
    }

    return bytes_written;
}

static size_t
save_indexes(FILE *fp, FsearchDatabaseIndex *index, bool *write_failed) {
    size_t bytes_written = 0;

    // TODO: actually implement storing all index information
    const uint32_t num_indexes = 0;
    bytes_written += write_data_to_file(fp, &num_indexes, 4, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save number of indexes: %d", num_indexes);
        goto out;
    }
    out:
    return bytes_written;
}

static size_t
save_excludes(FILE *fp, FsearchDatabaseIndex *index, bool *write_failed) {
    size_t bytes_written = 0;

    // TODO: actually implement storing all exclude information
    const uint32_t num_excludes = 0;
    bytes_written += write_data_to_file(fp, &num_excludes, 4, 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save number of indexes: %d", num_excludes);
        goto out;
    }
    out:
    return bytes_written;
}

static size_t
save_exclude_pattern(FILE *fp, FsearchDatabaseIndex *index, bool *write_failed) {
    // TODO
    return 0;
}

bool
db_file_save(FsearchDatabaseIndex *index, const char *path) {
    g_assert(path);
    g_assert(index);

    g_debug("[db_save] saving database to file...");

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_debug("[db_save] database path doesn't exist: %s", path);
        return false;
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    g_autoptr(GString) path_full = g_string_new(path);
    g_string_append_c(path_full, G_DIR_SEPARATOR);
    g_string_append(path_full, "fsearch.db");

    g_autoptr(GString) path_full_temp = g_string_new(path_full->str);
    g_string_append(path_full_temp, ".tmp");

    g_debug("[db_save] trying to open temporary database file: %s", path_full_temp->str);

    FILE *fp = file_open_locked(path_full_temp->str, "wb");
    if (!fp) {
        g_debug("[db_save] failed to open temporary database file: %s", path_full_temp->str);
        goto save_fail;
    }

    g_debug("[db_save] updating folder indices...");
    update_folder_indices(index);

    bool write_failed = false;

    size_t bytes_written = 0;

    g_debug("[db_save] saving database header...");
    bytes_written += save_header(fp, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    g_debug("[db_save] saving database index flags...");
    const uint64_t index_flags = index->flags;
    bytes_written += write_data_to_file(fp, &index_flags, 8, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    DynamicArray *files = index->files[DATABASE_INDEX_TYPE_NAME];
    DynamicArray *folders = index->folders[DATABASE_INDEX_TYPE_NAME];

    const uint32_t num_folders = darray_get_num_items(folders);
    g_debug("[db_save] saving number of folders: %d", num_folders);
    bytes_written += write_data_to_file(fp, &num_folders, 4, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    const uint32_t num_files = darray_get_num_items(files);
    g_debug("[db_save] saving number of files: %d", num_files);
    bytes_written += write_data_to_file(fp, &num_files, 4, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    uint64_t folder_block_size = 0;
    const uint64_t folder_block_size_offset = bytes_written;
    g_debug("[db_save] saving folder block size...");
    bytes_written += write_data_to_file(fp, &folder_block_size, 8, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    uint64_t file_block_size = 0;
    const uint64_t file_block_size_offset = bytes_written;
    g_debug("[db_save] saving file block size...");
    bytes_written += write_data_to_file(fp, &file_block_size, 8, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    g_debug("[db_save] saving indices...");
    bytes_written += save_indexes(fp, index, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving excludes...");
    bytes_written += save_excludes(fp, index, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving exclude pattern...");
    bytes_written += save_exclude_pattern(fp, index, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving folders...");
    folder_block_size = save_folders(fp, index_flags, folders, num_folders, &write_failed);
    bytes_written += folder_block_size;
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving files...");
    file_block_size = save_files(fp, index_flags, files, num_files, &write_failed);
    bytes_written += file_block_size;
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving sorted arrays...");
    bytes_written += save_sorted_arrays(fp, index, num_files, num_folders, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    // now that we know the size of the file/folder block we've written, store it in the file header
    if (fseek(fp, (long int)folder_block_size_offset, SEEK_SET) != 0) {
        goto save_fail;
    }
    g_debug("[db_save] updating file and folder block size: %lu, %lu", folder_block_size, file_block_size);
    bytes_written += write_data_to_file(fp, &folder_block_size, 8, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    bytes_written += write_data_to_file(fp, &file_block_size, 8, 1, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    g_debug("[db_save] removing current database file...");
    // remove current database file
    unlink(path_full->str);

    g_clear_pointer(&fp, fclose);

    g_debug("[db_save] renaming temporary database file: %s -> %s", path_full_temp->str, path_full->str);
    // rename temporary fsearch.db.tmp to fsearch.db
    if (rename(path_full_temp->str, path_full->str) != 0) {
        goto save_fail;
    }

    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_stop(timer);

    g_debug("[db_save] database file saved in: %f ms", seconds * 1000);

    return true;

    save_fail:
    g_warning("[db_save] saving failed");

    g_clear_pointer(&fp, fclose);

    // remove temporary fsearch.db.tmp file
    unlink(path_full_temp->str);

    return false;
}

FsearchDatabaseIndex *
db_file_load(const char *file_path, void (*status_cb)(const char *)) {
    g_assert(file_path);

    FILE *fp = file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    DynamicArray *folders = NULL;
    DynamicArray *files = NULL;
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_TYPES] = {NULL};
    DynamicArray *sorted_files[NUM_DATABASE_INDEX_TYPES] = {NULL};
    FsearchMemoryPool *file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                           db_entry_get_sizeof_file_entry(),
                                                           (GDestroyNotify)db_entry_destroy);
    FsearchMemoryPool *folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                             db_entry_get_sizeof_folder_entry(),
                                                             (GDestroyNotify)db_entry_destroy);

    if (!load_header(fp)) {
        goto load_fail;
    }

    uint64_t index_flags = 0;
    if (!read_element_from_file(&index_flags, 8, fp)) {
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (!read_element_from_file(&num_folders, 4, fp)) {
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (!read_element_from_file(&num_files, 4, fp)) {
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    uint64_t folder_block_size = 0;
    if (!read_element_from_file(&folder_block_size, 8, fp)) {
        goto load_fail;
    }

    uint64_t file_block_size = 0;
    if (!read_element_from_file(&file_block_size, 8, fp)) {
        goto load_fail;
    }
    g_debug("[db_load] folder size: %lu, file size: %lu", folder_block_size, file_block_size);

    // TODO: implement index loading
    uint32_t num_indexes = 0;
    if (!read_element_from_file(&num_indexes, 4, fp)) {
        goto load_fail;
    }

    // TODO: implement exclude loading
    uint32_t num_excludes = 0;
    if (!read_element_from_file(&num_excludes, 4, fp)) {
        goto load_fail;
    }

    // pre-allocate the folders array so we can later map parent indices to the corresponding pointers
    sorted_folders[DATABASE_INDEX_TYPE_NAME] = darray_new(num_folders);
    folders = sorted_folders[DATABASE_INDEX_TYPE_NAME];

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntryFolder *folder = fsearch_memory_pool_malloc(folder_pool);
        FsearchDatabaseEntry *entry = (FsearchDatabaseEntry *)folder;
        db_entry_set_idx(entry, i);
        db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
        db_entry_set_parent(entry, NULL);
        darray_add_item(folders, folder);
    }

    if (status_cb) {
        status_cb(_("Loading folders…"));
    }
    // load folders
    if (!load_folders(fp, index_flags, folders, num_folders, folder_block_size)) {
        goto load_fail;
    }

    if (status_cb) {
        status_cb(_("Loading files…"));
    }
    // load files
    sorted_files[DATABASE_INDEX_TYPE_NAME] = darray_new(num_files);
    files = sorted_files[DATABASE_INDEX_TYPE_NAME];
    if (!load_files(fp, index_flags, file_pool, folders, files, num_files, file_block_size)) {
        goto load_fail;
    }

    if (!load_sorted_arrays(fp, sorted_folders, sorted_files)) {
        goto load_fail;
    }

    FsearchDatabaseIndex *index = calloc(1, sizeof(FsearchDatabaseIndex));
    g_assert(index);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        index->files[i] = g_steal_pointer(&sorted_files[i]);
        index->folders[i] = g_steal_pointer(&sorted_folders[i]);
    }
    index->file_pool = g_steal_pointer(&file_pool);
    index->folder_pool = g_steal_pointer(&folder_pool);

    index->flags = index_flags;

    g_clear_pointer(&fp, fclose);

    return index;

    load_fail:
    g_debug("[db_load] load failed");

    g_clear_pointer(&fp, fclose);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        g_clear_pointer(&sorted_folders[i], darray_unref);
        g_clear_pointer(&sorted_files[i], darray_unref);
    }
    g_clear_pointer(&file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&folder_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&index, free);

    return NULL;
}

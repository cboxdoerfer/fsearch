#include "fsearch_database_entry.h"
#include "fsearch_file_utils.h"
#include "fsearch_string_utils.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MACH__
#include "strverscmp.h"
#endif

typedef struct FsearchDatabaseEntryBase {
    FsearchDatabaseEntryBase *parent;

    // idx: index of this entry in the sorted list at pos DATABASE_INDEX_TYPE_NAME
    uint32_t attribute_flags;
    uint32_t index;
    uint8_t type : 4;
    uint8_t mark : 4;
    uint8_t deleted;
    uint8_t data[];
} FsearchDatabaseEntryBase;

struct FsearchDatabaseEntry {
    FsearchDatabaseEntryFolder *parent;
    char *name;
    off_t size;
    time_t mtime;

    // idx: index of this entry in the sorted list at pos DATABASE_INDEX_TYPE_NAME
    uint32_t attribute_flags;
    uint32_t idx;
    uint8_t type;
    uint8_t mark;
    // uint8_t data[];
};

struct FsearchDatabaseEntryFile {
    struct FsearchDatabaseEntry super;
};

struct FsearchDatabaseEntryFolder {
    struct FsearchDatabaseEntry super;
    // db_idx: the database index this folder belongs to
    uint32_t db_idx;
    uint32_t num_files;
    uint32_t num_folders;
};

static size_t entry_base_size = 0;

static size_t
entry_get_size_for_flags(FsearchDatabaseIndexPropertyFlags attribute_flags, const char *name, size_t name_len);

static void
build_path_recursively(FsearchDatabaseEntryBase *folder, GString *str, size_t name_offset) {
    if (G_UNLIKELY(!folder)) {
        return;
    }
    g_assert(folder->type == DATABASE_ENTRY_TYPE_FOLDER);
    g_assert(folder->data != NULL);

    if (G_LIKELY(folder->parent)) {
        build_path_recursively(folder->parent, str, name_offset);
    }
    const char *name = db_entry_get_attribute_name_for_offset(folder, name_offset);
    if (G_LIKELY(strcmp(name, "") != 0)) {
        g_string_append(str, name);
    }
    g_string_append_c(str, G_DIR_SEPARATOR);
}

void
db_entry_compare_context_free(FsearchDatabaseEntryCompareContext *ctx) {
    g_return_if_fail(ctx);

    g_clear_pointer(&ctx->file_type_table, g_hash_table_unref);
    g_clear_pointer(&ctx->entry_to_file_type_table, g_hash_table_unref);
    if (ctx->next_comp_func_data_free_func) {
        g_clear_pointer(&ctx->next_comp_func_data, ctx->next_comp_func_data_free_func);
    }
    g_clear_pointer(&ctx, free);
}

FsearchDatabaseEntryCompareContext *
db_entry_compare_context_new(DynamicArrayCompareDataFunc next_comp_func,
                             void *next_comp_func_data,
                             GDestroyNotify next_comp_func_data_free_func) {
    FsearchDatabaseEntryCompareContext *ctx = calloc(1, sizeof(FsearchDatabaseEntryCompareContext));
    g_assert(ctx);

    ctx->file_type_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx->entry_to_file_type_table = g_hash_table_new(NULL, NULL);
    ctx->next_comp_func = next_comp_func;
    ctx->next_comp_func_data = next_comp_func_data;
    ctx->next_comp_func_data_free_func = next_comp_func_data_free_func;
    return ctx;
}

bool
db_entry_is_folder(FsearchDatabaseEntryBase *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FOLDER;
}

bool
db_entry_is_file(FsearchDatabaseEntryBase *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FILE;
}

bool
db_entry_is_descendant(FsearchDatabaseEntryBase *entry, FsearchDatabaseEntryBase *maybe_ancestor) {
    while (entry) {
        if (entry->parent == maybe_ancestor) {
            return true;
        }
        entry = entry->parent;
    }
    return false;
}

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntryBase *entry) {
    g_assert(entry->type == DATABASE_ENTRY_TYPE_FOLDER);
    return db_entry_folder_get_num_files(entry) + db_entry_folder_get_num_folders(entry);
}

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntryBase *entry) {
    g_assert(entry->type == DATABASE_ENTRY_TYPE_FOLDER);
    uint32_t num_files = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
    return num_files;
}

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntryBase *entry) {
    g_assert(entry->type == DATABASE_ENTRY_TYPE_FOLDER);
    uint32_t num_folders = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
    return num_folders;
}

size_t
db_entry_get_sizeof_folder_entry() {
    return sizeof(FsearchDatabaseEntryFolder);
}

size_t
db_entry_get_sizeof_file_entry() {
    return sizeof(FsearchDatabaseEntryFile);
}

GString *
db_entry_get_path(FsearchDatabaseEntryBase *entry) {
    GString *path = g_string_new(NULL);
    db_entry_append_path(entry, path);
    return path;
}

GString *
db_entry_get_path_full(FsearchDatabaseEntryBase *entry) {
    GString *path_full = g_string_new(NULL);
    db_entry_append_full_path(entry, path_full);
    return path_full;
}

void
db_entry_append_path(FsearchDatabaseEntryBase *entry, GString *str) {
    if (entry->parent) {
        size_t name_offset = 0;
        db_entry_get_attribute_offset(entry->parent->attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset);
        build_path_recursively(entry->parent, str, name_offset);
    }
    if (str->len > 1) {
        g_string_set_size(str, str->len - 1);
    }
}

void
db_entry_append_full_path(FsearchDatabaseEntryBase *entry, GString *str) {
    if (entry->parent) {
        size_t name_offset = 0;
        db_entry_get_attribute_offset(entry->parent->attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset);
        build_path_recursively(entry->parent, str, name_offset);
    }

    const char *name = db_entry_get_name_raw(entry);
    g_string_append(str, name[0] == '\0' ? G_DIR_SEPARATOR_S : name);
}

time_t
db_entry_get_mtime(FsearchDatabaseEntryBase *entry) {
    time_t mtime = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &mtime, sizeof(mtime));
    return mtime;
}

off_t
db_entry_get_size(FsearchDatabaseEntryBase *entry) {
    off_t size = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size));
    return size;
}

const char *
db_entry_get_extension(FsearchDatabaseEntryBase *entry) {
    if (G_UNLIKELY(!entry)) {
        return NULL;
    }
    if (db_entry_is_folder(entry)) {
        return NULL;
    }
    const char *name = NULL;
    db_entry_get_attribute_name(entry, &name);
    return name ? fsearch_string_get_extension(name) : NULL;
}

const char *
db_entry_get_name_raw_for_display(FsearchDatabaseEntryBase *entry) {
    if (G_UNLIKELY(!entry)) {
        return NULL;
    }
    const char *name = NULL;
    db_entry_get_attribute_name(entry, &name);
    if (name && strcmp(name, "") != 0) {
        return name;
    }
    return G_DIR_SEPARATOR_S;
}

GString *
db_entry_get_name_for_display(FsearchDatabaseEntryBase *entry) {
    const char *name = db_entry_get_name_raw_for_display(entry);
    return name ? g_string_new(name) : NULL;
}

const char *
db_entry_get_name_raw(FsearchDatabaseEntryBase *entry) {
    const char *name = NULL;
    db_entry_get_attribute_name(entry, &name);
    return name;
}

FsearchDatabaseEntryBase *
db_entry_get_parent(FsearchDatabaseEntryBase *entry) {
    return entry ? entry->parent : NULL;
}

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntryBase *entry) {
    return entry ? entry->type : DATABASE_ENTRY_TYPE_NONE;
}

void
db_entry_free(FsearchDatabaseEntryBase *entry) {
    g_return_if_fail(entry);
    db_entry_set_parent(entry, NULL);
    entry->deleted = 1;
}

void
db_entry_free_full(FsearchDatabaseEntryBase *entry) {
    while (entry) {
        FsearchDatabaseEntryBase *parent = entry->parent;
        g_clear_pointer(&entry, db_entry_free);
        entry = parent;
    }
}

FsearchDatabaseEntryBase *
db_entry_get_deep_copy(FsearchDatabaseEntryBase *entry) {
    const char *name = db_entry_get_name_raw(entry);
    const size_t entry_size = entry_get_size_for_flags(entry->attribute_flags, name, strlen(name));

    FsearchDatabaseEntryBase *copy = calloc(1, entry_size);
    g_assert_nonnull(copy);

    memcpy(copy, entry, entry_size);

    copy->parent = entry->parent ? db_entry_get_deep_copy(entry->parent) : NULL;
    return copy;
}

void
db_entry_append_content_type(FsearchDatabaseEntryBase *entry, GString *str) {
    g_autoptr(GString) path = db_entry_get_path_full(entry);
    g_autoptr(GFile) file = g_file_new_for_path(path->str);
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo)
        info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, &error);
    const char *content_type = NULL;
    if (info) {
        content_type = g_file_info_get_content_type(info);
    }
    g_string_append(str, content_type ? content_type : "unknown");
}

uint8_t
db_entry_get_mark(FsearchDatabaseEntryBase *entry) {
    return entry ? entry->mark : 0;
}

void
db_entry_set_mark(FsearchDatabaseEntryBase *entry, uint8_t mark) {
    g_return_if_fail(entry);
    entry->mark = mark;
}

uint32_t
db_entry_get_attribute_flags(FsearchDatabaseEntryBase *entry) {
    return entry ? entry->attribute_flags : 0;
}

uint32_t
db_entry_get_depth(FsearchDatabaseEntryBase *entry) {
    uint32_t depth = 0;
    while (entry && entry->parent) {
        entry = entry->parent;
        depth++;
    }
    return depth;
}

uint32_t
db_entry_get_db_index(FsearchDatabaseEntryBase *entry) {
    if (!db_entry_is_folder(entry)) {
        entry = entry->parent;
    }
    if (G_UNLIKELY(!entry)) {
        return 0;
    }
    uint32_t db_index = 0;
    if (db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_DB_INDEX, &db_index, sizeof(db_index))) {
        return db_index;
    }
    return 0;
}

static FsearchDatabaseEntryBase *
db_entry_get_parent_nth(FsearchDatabaseEntryBase *entry, uint32_t nth) {
    while (entry && nth > 0) {
        entry = entry->parent;
        nth--;
    }
    return entry;
}

static void
sort_entry_by_path_recursive(FsearchDatabaseEntryBase *entry_1,
                             FsearchDatabaseEntryBase *entry_2,
                             size_t name_offset,
                             int *res) {
    if (G_UNLIKELY(!entry_1 || !entry_2)) {
        return;
    }
    if (entry_1->parent) {
        sort_entry_by_path_recursive(entry_1->parent, entry_2->parent, name_offset, res);
    }
    if (*res != 0) {
        return;
    }
    const char *name_1 = db_entry_get_attribute_name_for_offset(entry_1, name_offset);
    const char *name_2 = db_entry_get_attribute_name_for_offset(entry_2, name_offset);
    *res = strverscmp(name_1, name_2);
}

int
db_entry_compare_entries_by_size(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    const off_t size_a = db_entry_get_size(*a);
    const off_t size_b = db_entry_get_size(*b);
    if (size_a > size_b) {
        return 1;
    }
    else if (size_a < size_b) {
        return -1;
    }
    return 0;
}

static const char *
get_file_type(FsearchDatabaseEntryBase *entry, GHashTable *file_type_table, GHashTable *entry_table) {
    char *cached_type = g_hash_table_lookup(entry_table, entry);
    if (cached_type) {
        return cached_type;
    }

    const char *name = db_entry_get_name_raw_for_display(entry);
    g_autofree char *type = fsearch_file_utils_get_file_type_non_localized(name,
                                                                           db_entry_is_folder(entry) ? TRUE : FALSE);
    cached_type = g_hash_table_lookup(file_type_table, type);
    if (!cached_type) {
        g_hash_table_add(file_type_table, type);
        cached_type = g_steal_pointer(&type);
    }
    g_hash_table_insert(entry_table, entry, cached_type);
    return cached_type;
}

int
db_entry_compare_entries_by_type(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b, gpointer data) {
    FsearchDatabaseEntryCompareContext *comp_ctx = data;

    const char *file_type_a = get_file_type(*a, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);
    const char *file_type_b = get_file_type(*b, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);

    int res = strcmp(file_type_a, file_type_b);
    if (res != 0) {
        return res;
    }
    return comp_ctx->next_comp_func ? comp_ctx->next_comp_func((void *)a, (void *)b, comp_ctx->next_comp_func_data) : res;
}

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    const time_t time_a = db_entry_get_mtime(*a);
    const time_t time_b = db_entry_get_mtime(*b);
    if (time_a > time_b) {
        return 1;
    }
    else if (time_a < time_b) {
        return -1;
    }
    return 0;
}

int
db_entry_compare_entries_by_position(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    return 0;
}

int
db_entry_compare_entries_by_full_path(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    FsearchDatabaseEntryBase *entry_a = *a;
    FsearchDatabaseEntryBase *entry_b = *b;
    const uint32_t a_n_path_elements = db_entry_get_depth(entry_a) + 1;
    const uint32_t b_n_path_elements = db_entry_get_depth(entry_b) + 1;

    const char *a_path[a_n_path_elements];
    const char *b_path[b_n_path_elements];
    FsearchDatabaseEntryBase *tmp = (FsearchDatabaseEntryBase *)entry_a;
    for (uint32_t i = 0; i < a_n_path_elements; i++) {
        a_path[a_n_path_elements - i - 1] = db_entry_get_name_raw(tmp);
        tmp = tmp->parent;
    }
    tmp = (FsearchDatabaseEntryBase *)entry_b;
    for (uint32_t i = 0; i < b_n_path_elements; i++) {
        b_path[b_n_path_elements - i - 1] = db_entry_get_name_raw(tmp);
        tmp = tmp->parent;
    }

    const uint32_t limit = MIN(a_n_path_elements, b_n_path_elements);
    for (uint32_t i = 0; i < limit; ++i) {
        const int res = strverscmp(a_path[i], b_path[i]);
        if (res != 0) {
            return res;
        }
    }
    if (a_n_path_elements < b_n_path_elements) {
        return -1;
    }
    else if (a_n_path_elements > b_n_path_elements) {
        return 1;
    }
    else {
        return 0;
    }
}

int
db_entry_compare_entries_by_path(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    FsearchDatabaseEntryBase *entry_a = *a;
    FsearchDatabaseEntryBase *entry_b = *b;
    const uint32_t a_depth = db_entry_get_depth(entry_a);
    const uint32_t b_depth = db_entry_get_depth(entry_b);

#if (0)
    const char *a_path[a_depth];
    const char *b_path[b_depth];
    FsearchDatabaseEntry *tmp = (FsearchDatabaseEntry *)entry_a->parent;
    for (uint32_t i = 0; i < a_depth; i++) {
        a_path[a_depth - i - 1] = tmp->name;
        tmp = (FsearchDatabaseEntry *)tmp->parent;
    }
    tmp = (FsearchDatabaseEntry *)entry_b->parent;
    for (uint32_t i = 0; i < b_depth; i++) {
        b_path[b_depth - i - 1] = tmp->name;
        tmp = (FsearchDatabaseEntry *)tmp->parent;
    }

    const uint32_t limit = MIN(a_depth, b_depth);
    for (uint32_t i = 0; i < limit; ++i) {
        const int res = strverscmp(a_path[i], b_path[i]);
        if (res != 0) {
            return res;
        }
    }
    if (a_depth < b_depth) {
        return -1;
    }
    else if (a_depth > b_depth) {
        return 1;
    }
    else {
        return db_entry_compare_entries_by_name(a, b);
    }
#endif

    size_t name_offset = 0;
    if (!db_entry_get_attribute_offset(entry_a->attribute_flags | DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS
                                           | DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES,
                                       DATABASE_INDEX_PROPERTY_NAME,
                                       &name_offset)) {
        return 0;
    }

    int res = 0;
    if (a_depth == b_depth) {
        sort_entry_by_path_recursive(entry_a->parent, entry_b->parent, name_offset, &res);
        return res == 0 ? db_entry_compare_entries_by_name(a, b) : res;
    }
    else if (a_depth > b_depth) {
        const uint32_t diff = a_depth - b_depth;
        FsearchDatabaseEntryBase *parent_a = db_entry_get_parent_nth(entry_a->parent, diff);
        sort_entry_by_path_recursive(parent_a, entry_b->parent, name_offset, &res);
        return res == 0 ? 1 : res;
    }
    else {
        const uint32_t diff = b_depth - a_depth;
        FsearchDatabaseEntryBase *parent_b = db_entry_get_parent_nth(entry_b->parent, diff);
        sort_entry_by_path_recursive(entry_a->parent, parent_b, name_offset, &res);
        return res == 0 ? -1 : res;
    }
}

static void
db_entry_update_folder_size(FsearchDatabaseEntryBase *folder, off_t size) {
    if (!folder) {
        return;
    }
    g_assert(db_entry_is_folder(folder));
    off_t old_size = 0;
    db_entry_get_attribute(folder, DATABASE_INDEX_PROPERTY_SIZE, &old_size, sizeof(old_size));
    old_size += size;
    db_entry_set_attribute(folder, DATABASE_INDEX_PROPERTY_SIZE, &old_size, sizeof(old_size));
    db_entry_update_folder_size(folder->parent, size);
}

int
db_entry_compare_entries_by_extension(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    const char *ext_a = db_entry_get_extension(*a);
    const char *ext_b = db_entry_get_extension(*b);
    return strcmp(ext_a ? ext_a : "", ext_b ? ext_b : "");
}

int
db_entry_compare_entries_by_name(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b) {
    if (G_UNLIKELY(*a == NULL || *b == NULL)) {
        return 0;
    }
    const char *name_a = db_entry_get_name_raw(*a);
    const char *name_b = db_entry_get_name_raw(*b);
    return strverscmp(name_a ? name_a : "", name_b ? name_b : "");
}

void
db_entry_set_mtime(FsearchDatabaseEntryBase *entry, time_t mtime) {
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &mtime, sizeof(mtime));
}

void
db_entry_set_size(FsearchDatabaseEntryBase *entry, off_t size) {
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size));
}

void
db_entry_set_name(FsearchDatabaseEntryBase *entry, const char *name) {
    // TODO
    // g_clear_pointer(&entry->name, free);
    // entry->name = strdup(name ? name : "");
}

static inline void
decrement_num_files(FsearchDatabaseEntryBase *entry) {
    uint32_t num_files = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
    if (num_files > 0) {
        num_files -= 1;
        db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
    }
}

static inline void
decrement_num_folders(FsearchDatabaseEntryBase *entry) {
    uint32_t num_folders = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
    if (num_folders > 0) {
        num_folders -= 1;
        db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
    }
}

static inline void
increment_num_files(FsearchDatabaseEntryBase *entry) {
    uint32_t num_files = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
    num_files += 1;
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
}

static inline void
increment_num_folders(FsearchDatabaseEntryBase *entry) {
    uint32_t num_folders = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
    num_folders += 1;
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
}

void
db_entry_set_parent(FsearchDatabaseEntryBase *entry, FsearchDatabaseEntryBase *parent) {
    g_return_if_fail(entry != NULL);
    if (entry->parent) {
        // The entry already has a parent. First un-parent it and update its current parents state:
        // * Decrement file/folder count
        FsearchDatabaseEntryBase *p = entry->parent;
        if (db_entry_is_folder(entry)) {
            decrement_num_folders(p);
        }
        else if (db_entry_is_file(entry)) {
            decrement_num_files(p);
        }
        // * Update the size
        // while (p) {
        //    p->super.size = p->super.size > entry->size ? p->super.size - entry->size : 0;
        //    p = p->super.parent;
        //}
    }

    if (parent) {
        // parent is non-NULL, increment its file/folder count
        g_assert(db_entry_is_folder(parent));
        if (db_entry_is_folder(entry)) {
            increment_num_folders(parent);
        }
        else if (db_entry_is_file(entry)) {
            increment_num_files(parent);
        }
        // * Update the size
        // FsearchDatabaseEntryFolder *p = parent;
        // while (p) {
        //    p->super.size += entry->size;
        //    p = p->super.parent;
        //}
    }
    entry->parent = parent;
}

void
db_entry_set_db_index(FsearchDatabaseEntryBase *entry, uint32_t db_index) {
    if (!db_entry_is_folder(entry)) {
        return;
    }
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_DB_INDEX, &db_index, sizeof(db_index));
}

bool
db_entry_get_attribute_offset(FsearchDatabaseIndexPropertyFlags attribute_flags,
                              FsearchDatabaseIndexProperty attribute,
                              size_t *offset) {
    size_t offset_tmp = 0;
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_SIZE) {
            goto out;
        }
        offset_tmp += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_MODIFICATION_TIME) {
            goto out;
        }
        offset_tmp += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_ACCESS_TIME) {
            goto out;
        }
        offset_tmp += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME) {
            goto out;
        }
        offset_tmp += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_DB_INDEX) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_DB_INDEX) {
            goto out;
        }
        offset_tmp += sizeof(int32_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_NUM_FILES) {
            goto out;
        }
        offset_tmp += sizeof(int32_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS) != 0) {
        if (attribute == DATABASE_INDEX_PROPERTY_NUM_FOLDERS) {
            goto out;
        }
        offset_tmp += sizeof(int32_t);
    }
    if (attribute == DATABASE_INDEX_PROPERTY_NAME) {
        goto out;
    }
    return false;

out:
    *offset = offset_tmp;
    return true;
}

static size_t
entry_get_size_for_flags(FsearchDatabaseIndexPropertyFlags attribute_flags, const char *name, size_t name_len) {
    size_t size = sizeof(FsearchDatabaseEntryBase);
    if (name) {
        // Length of string + 1 (zero delimiter)
        size += name_len + 1;
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        size += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        size += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME) != 0) {
        size += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME) != 0) {
        size += sizeof(int64_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_DB_INDEX) != 0) {
        size += sizeof(int32_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES) != 0) {
        size += sizeof(int32_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS) != 0) {
        size += sizeof(int32_t);
    }
    return size;
}

FsearchDatabaseEntryBase *
db_entry_new(uint32_t attribute_flags, const char *name, FsearchDatabaseEntryBase *parent, FsearchDatabaseEntryType type) {
    if (type == DATABASE_ENTRY_TYPE_FOLDER) {
        attribute_flags = attribute_flags | DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS
                        | DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES;
    }
    const size_t name_len = name ? strlen(name) : 0;
    const size_t entry_size = entry_get_size_for_flags(attribute_flags, name, name_len);
    FsearchDatabaseEntryBase *entry = calloc(1, entry_size);
    g_assert_nonnull(entry);

    entry->type = type;
    entry->attribute_flags = attribute_flags;

    size_t name_offset = 0;
    if (db_entry_get_attribute_offset(attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset)) {
        memcpy(entry->data + name_offset, name, name_len + 1);
    }
    entry_base_size = name_offset;

    if (parent) {
        // set parent must happen after entry->type was set, so best set it at the end
        db_entry_set_parent(entry, parent);
    }
    return entry;
}

FsearchDatabaseEntryBase *
db_entry_new_with_attributes(uint32_t attribute_flags,
                             const char *name,
                             FsearchDatabaseEntryBase *parent,
                             FsearchDatabaseEntryType type,
                             ...) {
    va_list args;
    va_start(args, type);

    FsearchDatabaseEntryBase *entry = db_entry_new(attribute_flags, name, parent, type);

    FsearchDatabaseIndexProperty attribute = va_arg(args, int);
    while (attribute != DATABASE_INDEX_PROPERTY_NONE) {
        int32_t attribute_val_i32 = 0;
        int64_t attribute_val_i64 = 0;
        int32_t attribute_val_test_i32 = 0;
        int64_t attribute_val_test_i64 = 0;
        switch (attribute) {
        case DATABASE_INDEX_PROPERTY_SIZE:
        case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        case DATABASE_INDEX_PROPERTY_ACCESS_TIME:
        case DATABASE_INDEX_PROPERTY_CREATION_TIME:
        case DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME:
            attribute_val_i64 = va_arg(args, int64_t);
            db_entry_set_attribute(entry, attribute, &attribute_val_i64, sizeof(int64_t));
            // db_entry_get_attribute(entry, attribute, &attribute_val_test_i64, sizeof(attribute_val_test_i64));
            // g_assert(attribute_val_test_i64 == attribute_val_i64);
            break;
        case DATABASE_INDEX_PROPERTY_DB_INDEX:
        case DATABASE_INDEX_PROPERTY_NUM_FILES:
        case DATABASE_INDEX_PROPERTY_NUM_FOLDERS:
            attribute_val_i32 = va_arg(args, int32_t);
            db_entry_set_attribute(entry, attribute, &attribute_val_i32, sizeof(int32_t));
            // db_entry_get_attribute(entry, attribute, &attribute_val_test_i32, sizeof(attribute_val_test_i32));
            // g_assert(attribute_val_test_i32 == attribute_val_i32);
            break;
        case DATABASE_INDEX_PROPERTY_NONE:
        case DATABASE_INDEX_PROPERTY_NAME:
        case DATABASE_INDEX_PROPERTY_PATH:
        case DATABASE_INDEX_PROPERTY_PATH_FULL:
        case DATABASE_INDEX_PROPERTY_FILETYPE:
        case DATABASE_INDEX_PROPERTY_EXTENSION:
        case NUM_DATABASE_INDEX_PROPERTIES:
            g_assert_not_reached();
        }
        attribute = va_arg(args, int);
    }

    va_end(args);

    return entry;
}

bool
db_entry_get_attribute_name(FsearchDatabaseEntryBase *entry, const char **name) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(entry, name);
    size_t offset = 0;

    if (entry->deleted) {
        GString *path = db_entry_get_path_full(entry);
        g_print("%s\n", path->str);
        g_assert(entry->deleted != 1);
    }
    if (entry->parent) {
        if (entry->parent->deleted == 1) {
            GString *path = db_entry_get_path_full(entry);
            g_print("%s\n", path->str);
            g_assert(entry->parent->deleted != 1);
        }
    }
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &offset)) {
        *name = (const char *)(entry->data + offset);
        return true;
    }
    return false;
}

const char *
db_entry_get_attribute_name_for_offset(FsearchDatabaseEntryBase *entry, size_t offset) {
    g_return_val_if_fail(entry, false);
    g_assert(entry->deleted != 1);
    if (entry->parent) {
        g_assert(entry->parent->deleted != 1);
    }
    return (const char *)(entry->data + offset);
}

void
db_entry_get_attribute_for_offest(FsearchDatabaseEntryBase *entry, size_t offset, void *dest, size_t size) {
    g_return_if_fail(entry);
    g_return_if_fail(dest);
    memcpy(dest, entry->data + offset, size);
}

bool
db_entry_get_attribute(FsearchDatabaseEntryBase *entry, FsearchDatabaseIndexProperty attribute, void *dest, size_t size) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(dest, false);
    g_assert(entry->deleted != 1);
    if (entry->parent) {
        g_assert(entry->parent->deleted != 1);
    }
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, attribute, &offset)) {
        memcpy(dest, entry->data + offset, size);
        return true;
    }
    return false;
}

bool
db_entry_set_attribute(FsearchDatabaseEntryBase *entry, FsearchDatabaseIndexProperty attribute, void *src, size_t size) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(src, false);
    g_assert(entry->deleted != 1);
    if (entry->parent) {
        g_assert(entry->parent->deleted != 1);
    }
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, attribute, &offset)) {
        memcpy(entry->data + offset, src, size);
        return true;
    }
    return false;
}

uint32_t
db_entry_get_index(FsearchDatabaseEntryBase *entry) {
    g_return_val_if_fail(entry, 0);
    g_assert(entry->deleted != 1);
    if (entry->parent) {
        g_assert(entry->parent->deleted != 1);
    }
    return entry->index;
}

void
db_entry_set_index(FsearchDatabaseEntryBase *entry, uint32_t index) {
    g_return_if_fail(entry);
    g_assert(entry->deleted != 1);
    if (entry->parent) {
        g_assert(entry->parent->deleted != 1);
    }
    entry->index = index;
}

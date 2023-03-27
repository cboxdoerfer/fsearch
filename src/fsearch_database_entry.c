#include "fsearch_database_entry.h"
#include "fsearch_file_utils.h"
#include "fsearch_string_utils.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MACH__
#include "strverscmp.h"
#endif

struct FsearchDatabaseEntry {
    FsearchDatabaseEntryFolder *parent;
    char *name;
    off_t size;
    time_t mtime;

    // idx: index of this entry in the sorted list at pos DATABASE_INDEX_TYPE_NAME
    uint32_t idx;
    uint8_t type;
    uint8_t mark;
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
    int32_t wd;
};

static void
build_path_recursively(FsearchDatabaseEntryFolder *folder, GString *str) {
    if (G_UNLIKELY(!folder)) {
        return;
    }
    FsearchDatabaseEntry *entry = (FsearchDatabaseEntry *)folder;
    if (G_LIKELY(entry->parent)) {
        build_path_recursively(entry->parent, str);
    }
    if (G_LIKELY(strcmp(entry->name, "") != 0)) {
        g_string_append(str, entry->name);
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
db_entry_is_folder(FsearchDatabaseEntry *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FOLDER;
}

bool
db_entry_is_file(FsearchDatabaseEntry *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FILE;
}

bool
db_entry_is_descendant(FsearchDatabaseEntry *entry, FsearchDatabaseEntryFolder *maybe_ancestor) {
    while (entry) {
        if (entry->parent == maybe_ancestor) {
            return true;
        }
        entry = (FsearchDatabaseEntry *)entry->parent;
    }
    return false;
}

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntryFolder *entry) {
    g_assert(entry->super.type == DATABASE_ENTRY_TYPE_FOLDER);
    return entry->num_files + entry->num_folders;
}

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntryFolder *entry) {
    g_assert(entry->super.type == DATABASE_ENTRY_TYPE_FOLDER);
    return entry->num_files;
}

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntryFolder *entry) {
    g_assert(entry->super.type == DATABASE_ENTRY_TYPE_FOLDER);
    return entry->num_folders;
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
db_entry_get_path(FsearchDatabaseEntry *entry) {
    GString *path = g_string_new(NULL);
    db_entry_append_path(entry, path);
    return path;
}

GString *
db_entry_get_path_full(FsearchDatabaseEntry *entry) {
    GString *path_full = g_string_new(NULL);
    db_entry_append_full_path(entry, path_full);
    return path_full;
}

void
db_entry_append_path(FsearchDatabaseEntry *entry, GString *str) {
    build_path_recursively(entry->parent, str);
    if (str->len > 1) {
        g_string_set_size(str, str->len - 1);
    }
}

void
db_entry_append_full_path(FsearchDatabaseEntry *entry, GString *str) {
    build_path_recursively(entry->parent, str);
    g_string_append(str, entry->name[0] == '\0' ? G_DIR_SEPARATOR_S : entry->name);
}

time_t
db_entry_get_mtime(FsearchDatabaseEntry *entry) {
    return entry ? entry->mtime : 0;
}

off_t
db_entry_get_size(FsearchDatabaseEntry *entry) {
    return entry ? entry->size : 0;
}

const char *
db_entry_get_extension(FsearchDatabaseEntry *entry) {
    if (G_UNLIKELY(!entry)) {
        return NULL;
    }
    if (db_entry_is_folder(entry)) {
        return NULL;
    }
    return fsearch_string_get_extension(entry->name);
}

const char *
db_entry_get_name_raw_for_display(FsearchDatabaseEntry *entry) {
    if (G_UNLIKELY(!entry)) {
        return NULL;
    }
    if (strcmp(entry->name, "") != 0) {
        return entry->name;
    }
    return G_DIR_SEPARATOR_S;
}

GString *
db_entry_get_name_for_display(FsearchDatabaseEntry *entry) {
    const char *name = db_entry_get_name_raw_for_display(entry);
    return name ? g_string_new(name) : NULL;
}

const char *
db_entry_get_name_raw(FsearchDatabaseEntry *entry) {
    return entry ? entry->name : NULL;
}

FsearchDatabaseEntryFolder *
db_entry_get_parent(FsearchDatabaseEntry *entry) {
    return entry ? entry->parent : NULL;
}

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry) {
    return entry ? entry->type : DATABASE_ENTRY_TYPE_NONE;
}

void
db_entry_free_deep_copy(FsearchDatabaseEntry *entry) {
    while (entry) {
        FsearchDatabaseEntryFolder *parent = entry->parent;
        db_entry_destroy(entry);
        g_clear_pointer(&entry, free);
        entry = (FsearchDatabaseEntry *)parent;
    }
}

FsearchDatabaseEntry *
db_entry_get_deep_copy(FsearchDatabaseEntry *entry) {
    FsearchDatabaseEntry *copy = calloc(1,
                                        entry->type == DATABASE_ENTRY_TYPE_FOLDER ? sizeof(FsearchDatabaseEntryFolder)
                                                                                  : sizeof(FsearchDatabaseEntryFile));
    copy->name = g_strdup(entry->name);
    copy->type = entry->type;
    copy->mtime = entry->mtime;
    copy->size = entry->size;
    copy->idx = entry->idx;
    copy->mark = entry->mark;
    if (entry->type == DATABASE_ENTRY_TYPE_FOLDER) {
        FsearchDatabaseEntryFolder *copy_folder = (FsearchDatabaseEntryFolder *)copy;
        FsearchDatabaseEntryFolder *entry_folder = (FsearchDatabaseEntryFolder *)entry;
        copy_folder->num_files = entry_folder->num_files;
        copy_folder->num_folders = entry_folder->num_folders;
        copy_folder->db_idx = entry_folder->db_idx;
        copy_folder->wd = entry_folder->wd;
    }
    copy->parent = entry->parent
                     ? (FsearchDatabaseEntryFolder *)db_entry_get_deep_copy((FsearchDatabaseEntry *)entry->parent)
                     : NULL;
    return copy;
}

FsearchDatabaseEntry *
db_entry_get_dummy_for_name_and_parent(FsearchDatabaseEntry *parent, const char *name, FsearchDatabaseEntryType type) {
    g_return_val_if_fail(name, NULL);
    if (parent) {
        g_return_val_if_fail(parent->type == DATABASE_ENTRY_TYPE_FOLDER, NULL);
    }

    FsearchDatabaseEntry *entry = calloc(1,
                                         type == DATABASE_ENTRY_TYPE_FOLDER ? sizeof(FsearchDatabaseEntryFolder)
                                                                            : sizeof(FsearchDatabaseEntryFile));
    entry->parent = (FsearchDatabaseEntryFolder *)parent;
    entry->name = g_strdup(name);
    entry->type = type;

    return entry;
}

void
db_entry_append_content_type(FsearchDatabaseEntry *entry, GString *str) {
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

int32_t
db_entry_get_wd(FsearchDatabaseEntryFolder *entry) {
    return entry ? entry->wd : 0;
}

uint8_t
db_entry_get_mark(FsearchDatabaseEntry *entry) {
    return entry ? entry->mark : 0;
}

uint32_t
db_entry_get_idx(FsearchDatabaseEntry *entry) {
    return entry ? entry->idx : 0;
}

void
db_entry_destroy(FsearchDatabaseEntry *entry) {
    if (G_UNLIKELY(!entry)) {
        return;
    }
    g_clear_pointer(&entry->name, free);
}

uint32_t
db_entry_get_depth(FsearchDatabaseEntry *entry) {
    uint32_t depth = 0;
    while (entry && entry->parent) {
        entry = (FsearchDatabaseEntry *)entry->parent;
        depth++;
    }
    return depth;
}

uint32_t
db_entry_get_db_index(FsearchDatabaseEntry *entry) {
    if (db_entry_is_folder(entry)) {
        return ((FsearchDatabaseEntryFolder *)entry)->db_idx;
    }
    FsearchDatabaseEntryFolder *parent = entry->parent;
    if (G_UNLIKELY(!parent)) {
        return 0;
    }
    return parent->db_idx;
}

static FsearchDatabaseEntryFolder *
db_entry_get_parent_nth(FsearchDatabaseEntryFolder *entry, uint32_t nth) {
    while (entry && nth > 0) {
        entry = entry->super.parent;
        nth--;
    }
    return entry;
}

static void
sort_entry_by_path_recursive(FsearchDatabaseEntryFolder *entry_a, FsearchDatabaseEntryFolder *entry_b, int *res) {
    if (G_UNLIKELY(!entry_a || !entry_b)) {
        return;
    }
    if (entry_a->super.parent) {
        sort_entry_by_path_recursive(entry_a->super.parent, entry_b->super.parent, res);
    }
    if (*res != 0) {
        return;
    }
    *res = strverscmp(entry_a->super.name, entry_b->super.name);
}

int
db_entry_compare_entries_by_size(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
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
get_file_type(FsearchDatabaseEntry *entry, GHashTable *file_type_table, GHashTable *entry_table) {
    const char *name = db_entry_get_name_raw_for_display(entry);
    g_autofree char *type = fsearch_file_utils_get_file_type_non_localized(name,
                                                                           db_entry_is_folder(entry) ? TRUE : FALSE);
    char *cached_type = g_hash_table_lookup(file_type_table, type);
    if (!cached_type) {
        g_hash_table_add(file_type_table, type);
        cached_type = g_steal_pointer(&type);
    }
    g_hash_table_insert(entry_table, entry, cached_type);
    return cached_type;
}

int
db_entry_compare_entries_by_type(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b, gpointer data) {
    FsearchDatabaseEntryCompareContext *comp_ctx = data;
    const char *file_type_a = g_hash_table_lookup(comp_ctx->entry_to_file_type_table, *a);
    const char *file_type_b = g_hash_table_lookup(comp_ctx->entry_to_file_type_table, *b);
    if (!file_type_a) {
        file_type_a = get_file_type(*a, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);
    }
    if (!file_type_b) {
        file_type_b = get_file_type(*b, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);
    }
    return strcmp(file_type_a, file_type_b);
}

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const time_t time_a = (*a)->mtime;
    const time_t time_b = (*b)->mtime;
    if (time_a > time_b) {
        return 1;
    }
    else if (time_a < time_b) {
        return -1;
    }
    return 0;
}

int
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return 0;
}

int
db_entry_compare_entries_by_full_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    FsearchDatabaseEntry *entry_a = *a;
    FsearchDatabaseEntry *entry_b = *b;
    const uint32_t a_n_path_elements = db_entry_get_depth(entry_a) + 1;
    const uint32_t b_n_path_elements = db_entry_get_depth(entry_b) + 1;

    const char *a_path[a_n_path_elements];
    const char *b_path[b_n_path_elements];
    FsearchDatabaseEntry *tmp = (FsearchDatabaseEntry *)entry_a;
    for (uint32_t i = 0; i < a_n_path_elements; i++) {
        a_path[a_n_path_elements - i - 1] = tmp->name;
        tmp = (FsearchDatabaseEntry *)tmp->parent;
    }
    tmp = (FsearchDatabaseEntry *)entry_b;
    for (uint32_t i = 0; i < b_n_path_elements; i++) {
        b_path[b_n_path_elements - i - 1] = tmp->name;
        tmp = (FsearchDatabaseEntry *)tmp->parent;
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
db_entry_compare_entries_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    FsearchDatabaseEntry *entry_a = *a;
    FsearchDatabaseEntry *entry_b = *b;
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

    int res = 0;
    if (a_depth == b_depth) {
        sort_entry_by_path_recursive(entry_a->parent, entry_b->parent, &res);
        return res == 0 ? db_entry_compare_entries_by_name(a, b) : res;
    }
    else if (a_depth > b_depth) {
        const uint32_t diff = a_depth - b_depth;
        FsearchDatabaseEntryFolder *parent_a = db_entry_get_parent_nth(entry_a->parent, diff);
        sort_entry_by_path_recursive(parent_a, entry_b->parent, &res);
        return res == 0 ? 1 : res;
    }
    else {
        const uint32_t diff = b_depth - a_depth;
        FsearchDatabaseEntryFolder *parent_b = db_entry_get_parent_nth(entry_b->parent, diff);
        sort_entry_by_path_recursive(entry_a->parent, parent_b, &res);
        return res == 0 ? -1 : res;
    }
}

static void
db_entry_update_folder_size(FsearchDatabaseEntryFolder *folder, off_t size) {
    if (!folder) {
        return;
    }
    folder->super.size += size;
    db_entry_update_folder_size(folder->super.parent, size);
}

int
db_entry_compare_entries_by_extension(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const char *ext_a = db_entry_get_extension(*a);
    const char *ext_b = db_entry_get_extension(*b);
    return strcmp(ext_a ? ext_a : "", ext_b ? ext_b : "");
}

int
db_entry_compare_entries_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    if (G_UNLIKELY(*a == NULL || *b == NULL)) {
        return 0;
    }
    const char *name_a = (*a)->name;
    const char *name_b = (*b)->name;
    return strverscmp(name_a ? name_a : "", name_b ? name_b : "");
}

void
db_entry_set_mtime(FsearchDatabaseEntry *entry, time_t mtime) {
    entry->mtime = mtime;
}

void
db_entry_set_size(FsearchDatabaseEntry *entry, off_t size) {
    entry->size = size;
}

void
db_entry_set_name(FsearchDatabaseEntry *entry, const char *name) {
    if (entry->name) {
        free(entry->name);
    }
    entry->name = strdup(name ? name : "");
}

void
db_entry_set_parent(FsearchDatabaseEntry *entry, FsearchDatabaseEntryFolder *parent) {
    if (entry->parent) {
        // The entry already has a parent. First un-parent it and update its current parents state:
        // * Decrement file/folder count
        FsearchDatabaseEntryFolder *p = entry->parent;
        if (db_entry_is_folder(entry)) {
            if (p->num_folders > 0) {
                p->num_folders--;
            }
        }
        else if (db_entry_is_file(entry)) {
            if (p->num_files > 0) {
                p->num_files--;
            }
        }
        // * Update the size
        // while (p) {
        //    p->super.size = p->super.size > entry->size ? p->super.size - entry->size : 0;
        //    p = p->super.parent;
        //}
    }

    if (parent) {
        // parent is non-NULL, increment its file/folder count
        g_assert(parent->super.type == DATABASE_ENTRY_TYPE_FOLDER);
        if (db_entry_is_folder(entry)) {
            parent->num_folders++;
        }
        else if (db_entry_is_file(entry)) {
            parent->num_files++;
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
db_entry_set_db_index(FsearchDatabaseEntry *entry, uint32_t db_index) {
    if (!db_entry_is_folder(entry)) {
        return;
    }
    ((FsearchDatabaseEntryFolder *)entry)->db_idx = db_index;
}

void
db_entry_set_type(FsearchDatabaseEntry *entry, FsearchDatabaseEntryType type) {
    entry->type = type;
}

void
db_entry_set_idx(FsearchDatabaseEntry *entry, uint32_t idx) {
    entry->idx = idx;
}

void
db_entry_set_mark(FsearchDatabaseEntry *entry, uint8_t mark) {
    entry->mark = mark;
}

void
db_entry_set_wd(FsearchDatabaseEntryFolder *entry, int32_t wd) {
    if (entry->super.type != DATABASE_ENTRY_TYPE_FOLDER) {
        return;
    }
    entry->wd = wd;
}

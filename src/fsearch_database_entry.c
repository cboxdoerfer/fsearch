#include "fsearch_database_entry.h"

#ifdef _WIN32
#include "win32_compat.h"
#endif
#include "fsearch_file_utils.h"
#include "fsearch_string_utils.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__MACH__) || defined(_WIN32)
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

bool
db_entry_is_folder(FsearchDatabaseEntry *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FOLDER;
}

bool
db_entry_is_file(FsearchDatabaseEntry *entry) {
    return entry->type == DATABASE_ENTRY_TYPE_FILE;
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
    if (entry->type == DATABASE_ENTRY_TYPE_FOLDER) {
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
    return (size_a > size_b) ? 1 : -1;
}

static const char *
get_file_type(FsearchDatabaseEntry *entry, GHashTable *file_type_table, GHashTable *entry_table) {
    const FsearchDatabaseEntryType type_a = db_entry_get_type(entry);

    const char *name = db_entry_get_name_raw_for_display(entry);
    g_autofree char *type =
        fsearch_file_utils_get_file_type_non_localized(name, type_a == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
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
    return ((*a)->mtime > (*b)->mtime) ? 1 : -1;
}

int
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return 0;
}

int
db_entry_compare_entries_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    FsearchDatabaseEntry *entry_a = *a;
    FsearchDatabaseEntry *entry_b = *b;
    const uint32_t a_depth = db_entry_get_depth(entry_a);
    const uint32_t b_depth = db_entry_get_depth(entry_b);

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
    entry->parent = parent;
    if (parent) {
        g_assert(parent->super.type == DATABASE_ENTRY_TYPE_FOLDER);
        if (entry->type == DATABASE_ENTRY_TYPE_FOLDER) {
            parent->num_folders++;
        }
        else if (entry->type == DATABASE_ENTRY_TYPE_FILE) {
            parent->num_files++;
        }
    }
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
db_entry_update_parent_size(FsearchDatabaseEntry *entry) {
    db_entry_update_folder_size(entry->parent, entry->size);
}

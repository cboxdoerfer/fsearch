/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

#include "fsearch_database_entry.h"
#include "fsearch_database_entry_flags.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_file_utils.h"
#include "fsearch_string_utils.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define DATABASE_INDEX_PROPERTY_FLAG_FOLDER_DEFAULTS                                                                   \
    (DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS | DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES)

typedef struct FsearchDatabaseEntry {
    FsearchDatabaseEntry *parent;

    uint32_t attribute_flags;
    uint16_t flags;
    // Make sure the attributes member is aligned to its largest data type
    alignas(int64_t) uint8_t attributes[];
} FsearchDatabaseEntry;

static size_t
entry_get_size_for_flags(FsearchDatabaseIndexPropertyFlags attribute_flags, const char *name, size_t name_len);

static void
build_path_recursively(FsearchDatabaseEntry *folder, GString *str, size_t name_offset) {
    if (G_UNLIKELY(!folder)) {
        return;
    }
    g_assert(folder->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER);
    g_assert(folder->attributes != NULL);

    if (G_LIKELY(folder->parent)) {
        build_path_recursively(folder->parent, str, name_offset);
    }
    const char *name = db_entry_get_attribute_name_for_offset(folder, name_offset);
    if (G_LIKELY(name[0] != '\0' && strcmp(name, G_DIR_SEPARATOR_S) != 0)) {
        g_string_append(str, name);
    }
    g_string_append_c(str, G_DIR_SEPARATOR);
}

void
db_entry_compare_context_free(FsearchDatabaseEntryCompareContext *ctx) {
    g_return_if_fail(ctx);

    g_clear_pointer(&ctx->file_type_table, g_hash_table_unref);
    g_clear_pointer(&ctx->entry_to_file_type_table, g_hash_table_unref);
    g_clear_pointer(&ctx, free);
}

FsearchDatabaseEntryCompareContext *
db_entry_compare_context_new(FsearchDatabaseSortOrderChain chain) {
    FsearchDatabaseEntryCompareContext *ctx = calloc(1, sizeof(FsearchDatabaseEntryCompareContext));
    g_assert(ctx);

    ctx->file_type_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx->entry_to_file_type_table = g_hash_table_new(NULL, NULL);
    ctx->chain = chain;
    return ctx;
}

bool
db_entry_is_folder(FsearchDatabaseEntry *entry) {
    return entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER;
}

bool
db_entry_is_file(FsearchDatabaseEntry *entry) {
    return entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FILE;
}

bool
db_entry_is_sibling(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *maybe_sibling) {
    if (entry->parent && entry->parent == maybe_sibling->parent) {
        return true;
    }
    return false;
}

bool
db_entry_is_descendant(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *maybe_ancestor) {
    while (entry) {
        if (entry->parent == maybe_ancestor) {
            return true;
        }
        entry = entry->parent;
    }
    return false;
}

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntry *entry) {
    g_assert(entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER);
    return db_entry_folder_get_num_files(entry) + db_entry_folder_get_num_folders(entry);
}

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntry *entry) {
    g_assert(entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER);
    uint32_t num_files = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FILES, &num_files, sizeof(num_files));
    return num_files;
}

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntry *entry) {
    g_assert(entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER);
    uint32_t num_folders = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &num_folders, sizeof(num_folders));
    return num_folders;
}

const char *
db_entry_get_root_path(FsearchDatabaseEntry *entry) {
    if (!entry) {
        return NULL;
    }

    while (entry->parent) {
        entry = entry->parent;
    }
    return db_entry_get_name_raw(entry);
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
db_entry_append_full_path(FsearchDatabaseEntry *entry, GString *str) {
    if (entry->parent) {
        size_t name_offset = 0;
        db_entry_get_attribute_offset(entry->parent->attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset);
        build_path_recursively(entry->parent, str, name_offset);
    }

    const char *name = db_entry_get_name_raw(entry);
    g_string_append(str, name[0] == '\0' ? G_DIR_SEPARATOR_S : name);
}

time_t
db_entry_get_mtime(FsearchDatabaseEntry *entry) {
    time_t mtime = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &mtime, sizeof(mtime));
    return mtime;
}

off_t
db_entry_get_size(FsearchDatabaseEntry *entry) {
    off_t size = 0;
    db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size));
    return size;
}

const char *
db_entry_get_extension(FsearchDatabaseEntry *entry) {
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
db_entry_get_name_raw_for_display(FsearchDatabaseEntry *entry) {
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
db_entry_get_name_for_display(FsearchDatabaseEntry *entry) {
    const char *name = db_entry_get_name_raw_for_display(entry);
    return name ? g_string_new(name) : NULL;
}

const char *
db_entry_get_name_raw(FsearchDatabaseEntry *entry) {
    const char *name = NULL;
    db_entry_get_attribute_name(entry, &name);
    return name;
}

FsearchDatabaseEntry *
db_entry_get_parent(FsearchDatabaseEntry *entry) {
    return entry ? entry->parent : NULL;
}

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(entry, DATABASE_ENTRY_TYPE_NONE);

    if (db_entry_is_folder(entry)) {
        return DATABASE_ENTRY_TYPE_FOLDER;
    }
    if (db_entry_is_file(entry)) {
        return DATABASE_ENTRY_TYPE_FILE;
    }
    return DATABASE_ENTRY_TYPE_NONE;
}

void
db_entry_free_no_unparent(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_clear_pointer(&entry, free);
}

void
db_entry_free(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    db_entry_set_parent(entry, NULL);
    g_clear_pointer(&entry, free);
}

void
db_entry_free_full(FsearchDatabaseEntry *entry) {
    while (entry) {
        FsearchDatabaseEntry *parent = entry->parent;
        g_clear_pointer(&entry, db_entry_free);
        entry = parent;
    }
}

FsearchDatabaseEntry *
db_entry_get_deep_copy(FsearchDatabaseEntry *entry) {
    const char *name = db_entry_get_name_raw(entry);
    const size_t entry_size = entry_get_size_for_flags(entry->attribute_flags, name, strlen(name));

    FsearchDatabaseEntry *copy = calloc(1, entry_size);
    g_assert_nonnull(copy);

    memcpy(copy, entry, entry_size);

    copy->parent = entry->parent ? db_entry_get_deep_copy(entry->parent) : NULL;
    return copy;
}

void
db_entry_append_content_type(FsearchDatabaseEntry *entry, GString *str) {
    g_autoptr(GString) path = db_entry_get_path_full(entry);
    g_autoptr(GFile) file = g_file_new_for_path(path->str);
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = g_file_query_info(file,
                                                  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                                  G_FILE_QUERY_INFO_NONE,
                                                  NULL,
                                                  &error);
    const char *content_type = NULL;
    if (info) {
        content_type = g_file_info_get_content_type(info);
    }
    g_string_append(str, content_type ? content_type : "unknown");
}

uint8_t
db_entry_get_mark(FsearchDatabaseEntry *entry) {
    return entry ? ((entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_MARKED) != 0) : 0;
    return entry ? entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_MARKED : 0;
}

void
db_entry_set_mark(FsearchDatabaseEntry *entry, uint8_t mark) {
    g_return_if_fail(entry);
    if (mark) {
        entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_MARKED;
    }
    else {
        entry->flags &= ~FSEARCH_DATABASE_ENTRY_FLAG_MARKED;
    }
}

uint32_t
db_entry_get_attribute_flags(FsearchDatabaseEntry *entry) {
    return entry ? entry->attribute_flags : 0;
}

uint32_t
db_entry_get_depth(FsearchDatabaseEntry *entry) {
    uint32_t depth = 0;
    while (entry && entry->parent) {
        entry = entry->parent;
        depth++;
    }
    return depth;
}

static FsearchDatabaseEntry *
db_entry_get_parent_nth(FsearchDatabaseEntry *entry, uint32_t nth) {
    while (entry && nth > 0) {
        entry = entry->parent;
        nth--;
    }
    return entry;
}

static void
sort_entry_by_path_recursive(FsearchDatabaseEntry *entry_1, FsearchDatabaseEntry *entry_2, size_t name_offset, int *res) {
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
    *res = fsearch_file_utils_cmp_paths(name_1, name_2);
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
db_entry_compare_entries_by_type(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b, gpointer data) {
    FsearchDatabaseEntryCompareContext *comp_ctx = data;

    const char *file_type_a = get_file_type(*a, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);
    const char *file_type_b = get_file_type(*b, comp_ctx->file_type_table, comp_ctx->entry_to_file_type_table);

    return strcmp(file_type_a, file_type_b);
}

static int
compare_entries_by_property(FsearchDatabaseEntry **a,
                            FsearchDatabaseEntry **b,
                            FsearchDatabaseIndexProperty property,
                            FsearchDatabaseEntryCompareContext *ctx) {
    switch (property) {
    case DATABASE_INDEX_PROPERTY_NAME:
        return db_entry_compare_entries_by_name(a, b);
    case DATABASE_INDEX_PROPERTY_PATH:
        return db_entry_compare_entries_by_path(a, b);
    case DATABASE_INDEX_PROPERTY_PATH_FULL:
        return db_entry_compare_entries_by_full_path(a, b);
    case DATABASE_INDEX_PROPERTY_SIZE:
        return db_entry_compare_entries_by_size(a, b);
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        return db_entry_compare_entries_by_extension(a, b);
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        return db_entry_compare_entries_by_modification_time(a, b);
    case DATABASE_INDEX_PROPERTY_FILETYPE:
        return db_entry_compare_entries_by_type(a, b, ctx);
    default:
        return 0;
    }
}

int
db_entry_compare_entries_by_chain(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b, gpointer data) {
    FsearchDatabaseEntryCompareContext *ctx = data;
    for (uint32_t i = 0; i < ctx->chain.length; ++i) {
        const int res = compare_entries_by_property(a, b, ctx->chain.properties[i], ctx);
        if (res != 0) {
            return res;
        }
    }
    return 0;
}

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
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
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    return 0;
}

int
db_entry_compare_entries_by_full_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    if (db_entry_is_sibling(*a, *b)) {
        // same parent hence same path -> sort by name
        return db_entry_compare_entries_by_name(a, b);
    }
    FsearchDatabaseEntry *entry_a = *a;
    FsearchDatabaseEntry *entry_b = *b;
    const uint32_t a_n_path_elements = db_entry_get_depth(entry_a) + 1;
    const uint32_t b_n_path_elements = db_entry_get_depth(entry_b) + 1;

    const char *a_path[a_n_path_elements];
    const char *b_path[b_n_path_elements];
    FsearchDatabaseEntry *tmp = (FsearchDatabaseEntry *)entry_a;
    for (uint32_t i = 0; i < a_n_path_elements; i++) {
        a_path[a_n_path_elements - i - 1] = db_entry_get_name_raw(tmp);
        tmp = tmp->parent;
    }
    tmp = (FsearchDatabaseEntry *)entry_b;
    for (uint32_t i = 0; i < b_n_path_elements; i++) {
        b_path[b_n_path_elements - i - 1] = db_entry_get_name_raw(tmp);
        tmp = tmp->parent;
    }

    const uint32_t limit = MIN(a_n_path_elements, b_n_path_elements);
    for (uint32_t i = 0; i < limit; ++i) {
        const int res = fsearch_file_utils_cmp_paths(a_path[i], b_path[i]);
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
        const int res = fsearch_file_utils_cmp_paths(a_path[i], b_path[i]);
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
    FsearchDatabaseEntry *folder_ref = entry_a->parent ? entry_a->parent : entry_b->parent;
    const uint32_t folder_flags = folder_ref ? folder_ref->attribute_flags
                                             : (entry_a->attribute_flags | DATABASE_INDEX_PROPERTY_FLAG_FOLDER_DEFAULTS);
    if (!db_entry_get_attribute_offset(folder_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset)) {
        return 0;
    }

    int res = 0;
    if (a_depth == b_depth) {
        sort_entry_by_path_recursive(entry_a->parent, entry_b->parent, name_offset, &res);
        return res;
    }
    else if (a_depth > b_depth) {
        const uint32_t diff = a_depth - b_depth;
        FsearchDatabaseEntry *parent_a = db_entry_get_parent_nth(entry_a->parent, diff);
        sort_entry_by_path_recursive(parent_a, entry_b->parent, name_offset, &res);
        return res == 0 ? 1 : res;
    }
    else {
        const uint32_t diff = b_depth - a_depth;
        FsearchDatabaseEntry *parent_b = db_entry_get_parent_nth(entry_b->parent, diff);
        sort_entry_by_path_recursive(entry_a->parent, parent_b, name_offset, &res);
        return res == 0 ? -1 : res;
    }
}

static void
db_entry_update_folder_size(FsearchDatabaseEntry *folder, off_t size) {
    if (!folder) {
        return;
    }
    g_assert(db_entry_is_folder(folder));
    size_t offset = 0;
    if (db_entry_get_attribute_offset(folder->attribute_flags, DATABASE_INDEX_PROPERTY_SIZE, &offset)) {
        off_t old_size = 0;

        db_entry_get_attribute_for_offset(folder, offset, &old_size, sizeof(old_size));

        if (size < 0 && old_size + size < 0) {
            g_warning("[db_entry] size to be set below zero. Set to zero instead.");
            old_size = 0;
        }
        else {
            old_size += size;
        }
        db_entry_set_attribute_for_offset(folder, offset, &old_size, sizeof(old_size));
        db_entry_update_folder_size(folder->parent, size);
    }
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
    const char *name_a = db_entry_get_name_raw(*a);
    const char *name_b = db_entry_get_name_raw(*b);
    return fsearch_file_utils_cmp_paths(name_a ? name_a : "", name_b ? name_b : "");
}

void
db_entry_set_mtime(FsearchDatabaseEntry *entry, time_t mtime) {
    db_entry_set_attribute(entry, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &mtime, sizeof(mtime));
}

void
db_entry_set_size(FsearchDatabaseEntry *entry, off_t size) {
    off_t old_size = 0;
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_SIZE, &offset)) {
        db_entry_get_attribute_for_offset(entry, offset, &old_size, sizeof(old_size));
        if (old_size != size) {
            db_entry_set_attribute_for_offset(entry, offset, &size, sizeof(size));
            db_entry_update_folder_size(entry->parent, size - old_size);
        }
    }
}

void
db_entry_set_name(FsearchDatabaseEntry *entry, const char *name) {
    // TODO
    // g_clear_pointer(&entry->name, free);
    // entry->name = strdup(name ? name : "");
}

static inline void
decrement_num_files(FsearchDatabaseEntry *entry) {
    uint32_t num_files = 0;
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NUM_FILES, &offset)) {
        db_entry_get_attribute_for_offset(entry, offset, &num_files, sizeof(num_files));
        if (num_files > 0) {
            num_files -= 1;
            db_entry_set_attribute_for_offset(entry, offset, &num_files, sizeof(num_files));
        }
    }
}

static inline void
decrement_num_folders(FsearchDatabaseEntry *entry) {
    uint32_t num_folders = 0;
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &offset)) {
        db_entry_get_attribute_for_offset(entry, offset, &num_folders, sizeof(num_folders));
        if (num_folders > 0) {
            num_folders -= 1;
            db_entry_set_attribute_for_offset(entry, offset, &num_folders, sizeof(num_folders));
        }
    }
}

static inline void
increment_num_files(FsearchDatabaseEntry *entry) {
    uint32_t num_files = 0;
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NUM_FILES, &offset)) {
        db_entry_get_attribute_for_offset(entry, offset, &num_files, sizeof(num_files));
        num_files += 1;
        db_entry_set_attribute_for_offset(entry, offset, &num_files, sizeof(num_files));
    }
}

static inline void
increment_num_folders(FsearchDatabaseEntry *entry) {
    uint32_t num_folders = 0;
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &offset)) {
        db_entry_get_attribute_for_offset(entry, offset, &num_folders, sizeof(num_folders));
        num_folders += 1;
        db_entry_set_attribute_for_offset(entry, offset, &num_folders, sizeof(num_folders));
    }
}

void
db_entry_set_parent_no_update(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *parent) {
    g_return_if_fail(entry != NULL);
    entry->parent = parent;
}

void
db_entry_increment_childcount(FsearchDatabaseEntry *entry, FsearchDatabaseEntryType type) {
    if (!entry) {
        return;
    }
    g_assert(db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER);
    if (type == DATABASE_ENTRY_TYPE_FOLDER) {
        increment_num_folders(entry);
    }
    else if (type == DATABASE_ENTRY_TYPE_FILE) {
        increment_num_files(entry);
    }
}

void
db_entry_set_parent_update_childcount(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *parent) {
    g_return_if_fail(entry != NULL);
    if (entry->parent) {
        // The entry already has a parent. First un-parent it and update its current parents state:
        // * Decrement file/folder count
        FsearchDatabaseEntry *p = entry->parent;
        if (db_entry_is_folder(entry)) {
            decrement_num_folders(p);
        }
        else if (db_entry_is_file(entry)) {
            decrement_num_files(p);
        }
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
    }
    entry->parent = parent;
}

void
db_entry_set_parent(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *parent) {
    g_return_if_fail(entry != NULL);
    if (entry->parent) {
        // The entry already has a parent. First un-parent it and update its current parents state:
        // * Decrement file/folder count
        FsearchDatabaseEntry *p = entry->parent;
        if (db_entry_is_folder(entry)) {
            decrement_num_folders(p);
        }
        else if (db_entry_is_file(entry)) {
            decrement_num_files(p);
        }
        off_t size = 0;
        db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size));
        db_entry_update_folder_size(p, -size);
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

        off_t size = 0;
        db_entry_get_attribute(entry, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size));
        db_entry_update_folder_size(parent, size);
    }
    entry->parent = parent;
}

bool
db_entry_get_attribute_offset(FsearchDatabaseIndexPropertyFlags attribute_flags,
                              FsearchDatabaseIndexProperty attribute,
                              size_t *offset) {
    // TODO: Faster attribute lookup
    // It's probably much faster to use __builtin_popcount if available to calculate the number of bits set in the
    // flags. From that we can easily calculate the byte offset to the attribute.
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

size_t *
db_entry_get_attribute_offsets(FsearchDatabaseIndexPropertyFlags attribute_flags) {
    size_t *offsets = calloc(NUM_DATABASE_INDEX_PROPERTIES, sizeof(size_t));
    g_assert(offsets != NULL);

    for (int i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (!db_entry_get_attribute_offset(attribute_flags, i, &offsets[i])) {
            offsets[i] = -1;
        }
    }
    return offsets;
}

static size_t
entry_get_size_for_flags(FsearchDatabaseIndexPropertyFlags attribute_flags, const char *name, size_t name_len) {
    size_t size = sizeof(FsearchDatabaseEntry);
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
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES) != 0) {
        size += sizeof(int32_t);
    }
    if ((attribute_flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS) != 0) {
        size += sizeof(int32_t);
    }
    return size;
}

FsearchDatabaseEntry *
db_entry_new(FsearchDatabaseIndexPropertyFlags attribute_flags,
             const char *name,
             FsearchDatabaseEntry *parent,
             FsearchDatabaseEntryType type) {
    if (type == DATABASE_ENTRY_TYPE_FOLDER) {
        attribute_flags = attribute_flags | DATABASE_INDEX_PROPERTY_FLAG_FOLDER_DEFAULTS;
    }
    const size_t name_len = name ? strlen(name) : 0;
    const size_t entry_size = entry_get_size_for_flags(attribute_flags, name, name_len);
    FsearchDatabaseEntry *entry = calloc(1, entry_size);
    g_assert_nonnull(entry);

    if (type == DATABASE_ENTRY_TYPE_FOLDER) {
        entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER;
    }
    else if (type == DATABASE_ENTRY_TYPE_FILE) {
        entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FILE;
    }
    else {
        g_assert_not_reached();
    }

    entry->attribute_flags = attribute_flags;

    size_t name_offset = 0;
    if (db_entry_get_attribute_offset(attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &name_offset)) {
        memcpy(entry->attributes + name_offset, name, name_len + 1);
    }

    if (parent) {
        // set parent must happen after entry->type was set, so best set it at the end
        db_entry_set_parent(entry, parent);
    }
    return entry;
}

FsearchDatabaseEntry *
db_entry_new_with_attributes(FsearchDatabaseIndexPropertyFlags attribute_flags,
                             const char *name,
                             FsearchDatabaseEntry *parent,
                             FsearchDatabaseEntryType type,
                             ...) {
    va_list args;
    va_start(args, type);

    // Set Parent to NULL. We will set the parent anyway after setting all the attributes
    FsearchDatabaseEntry *entry = db_entry_new(attribute_flags, name, NULL, type);

    FsearchDatabaseIndexProperty attribute = va_arg(args, int);
    while (attribute != DATABASE_INDEX_PROPERTY_NONE) {
        int32_t attribute_val_i32 = 0;
        int64_t attribute_val_i64 = 0;
        switch (attribute) {
        case DATABASE_INDEX_PROPERTY_SIZE:
        case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        case DATABASE_INDEX_PROPERTY_ACCESS_TIME:
        case DATABASE_INDEX_PROPERTY_CREATION_TIME:
        case DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME:
            attribute_val_i64 = va_arg(args, int64_t);
            db_entry_set_attribute(entry, attribute, &attribute_val_i64, sizeof(int64_t));
            break;
        case DATABASE_INDEX_PROPERTY_NUM_FILES:
        case DATABASE_INDEX_PROPERTY_NUM_FOLDERS:
            attribute_val_i32 = va_arg(args, int32_t);
            db_entry_set_attribute(entry, attribute, &attribute_val_i32, sizeof(int32_t));
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

    // Set parent at the end after all properties have ben set. This ensures that the entry has the correct size
    // and the parent entry size is updated properly
    if (parent) {
        db_entry_set_parent(entry, parent);
    }

    return entry;
}

bool
db_entry_get_attribute_name(FsearchDatabaseEntry *entry, const char **name) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(entry, name);
    size_t offset = 0;

    if (db_entry_get_attribute_offset(entry->attribute_flags, DATABASE_INDEX_PROPERTY_NAME, &offset)) {
        *name = (const char *)(entry->attributes + offset);
        return true;
    }
    return false;
}

const char *
db_entry_get_attribute_name_for_offset(FsearchDatabaseEntry *entry, size_t offset) {
    g_return_val_if_fail(entry, false);
    return (const char *)(entry->attributes + offset);
}

void
db_entry_get_attribute_for_offset(FsearchDatabaseEntry *entry, size_t offset, void *dest, size_t size) {
    g_return_if_fail(entry);
    g_return_if_fail(dest);
    memcpy(dest, entry->attributes + offset, size);
}

bool
db_entry_get_attribute(FsearchDatabaseEntry *entry, FsearchDatabaseIndexProperty attribute, void *dest, size_t size) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(dest, false);
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, attribute, &offset)) {
        memcpy(dest, entry->attributes + offset, size);
        return true;
    }
    return false;
}

void
db_entry_set_attribute_for_offset(FsearchDatabaseEntry *entry, size_t offset, void *src, size_t size) {
    memcpy(entry->attributes + offset, src, size);
}

bool
db_entry_set_attribute(FsearchDatabaseEntry *entry, FsearchDatabaseIndexProperty attribute, void *src, size_t size) {
    g_return_val_if_fail(entry, false);
    g_return_val_if_fail(src, false);
    size_t offset = 0;
    if (db_entry_get_attribute_offset(entry->attribute_flags, attribute, &offset)) {
        memcpy(entry->attributes + offset, src, size);
        return true;
    }
    return false;
}

FsearchDatabaseEntryFlags
db_entry_get_flags(FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(entry, 0);
    return entry->flags;
}

void
db_entry_set_unmonitored_fanotify(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_assert(db_entry_is_folder(entry));
    entry->flags &= ~FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FANOTIFY;
}

void
db_entry_set_monitored_fanotify(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_assert(db_entry_is_folder(entry));
    entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FANOTIFY;
}

void
db_entry_set_monitored_failed(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_assert(db_entry_is_folder(entry));
    entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FAILED;
}

bool
db_entry_is_monitored_failed(FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(entry, false);
    if (db_entry_is_file(entry)) {
        entry = entry->parent;
    }
    return entry ? (entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FAILED) != 0 : false;
}

bool
db_entry_is_monitored_fanotify(FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(entry, false);
    if (db_entry_is_file(entry)) {
        entry = entry->parent;
    }
    return entry ? (entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_FANOTIFY) != 0 : false;
}

void
db_entry_set_unmonitored_inotify(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_assert(db_entry_is_folder(entry));
    entry->flags &= ~FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_INOTIFY;
}

void
db_entry_set_monitored_inotify(FsearchDatabaseEntry *entry) {
    g_return_if_fail(entry);
    g_assert(db_entry_is_folder(entry));
    entry->flags |= FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_INOTIFY;
}

bool
db_entry_is_monitored_inotify(FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(entry, false);
    if (db_entry_is_file(entry)) {
        entry = entry->parent;
    }
    return entry ? (entry->flags & FSEARCH_DATABASE_ENTRY_FLAG_MONITORED_INOTIFY) != 0 : false;
}
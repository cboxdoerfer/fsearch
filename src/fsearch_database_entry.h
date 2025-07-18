#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "fsearch_array.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_entry_flags.h"

typedef enum {
    DATABASE_ENTRY_TYPE_NONE,
    DATABASE_ENTRY_TYPE_FOLDER,
    DATABASE_ENTRY_TYPE_FILE,
    NUM_DATABASE_ENTRY_TYPES,
} FsearchDatabaseEntryType;

typedef struct FsearchDatabaseEntry FsearchDatabaseEntry;

typedef struct FsearchDatabaseEntryCompareContext {
    GHashTable *file_type_table;
    GHashTable *entry_to_file_type_table;
    DynamicArrayCompareDataFunc next_comp_func;
    void *next_comp_func_data;
    GDestroyNotify next_comp_func_data_free_func;
} FsearchDatabaseEntryCompareContext;

void
db_entry_compare_context_free(FsearchDatabaseEntryCompareContext *ctx);

FsearchDatabaseEntryCompareContext *
db_entry_compare_context_new(DynamicArrayCompareDataFunc next_comp_func,
                             void *next_comp_func_data,
                             GDestroyNotify next_comp_func_data_free_func);

bool
db_entry_is_folder(FsearchDatabaseEntry *entry);

bool
db_entry_is_file(FsearchDatabaseEntry *entry);

bool
db_entry_is_descendant(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *maybe_ancestor);

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntry *entry);

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntry *entry);

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntry *entry);

void
db_entry_set_index(FsearchDatabaseEntry *entry, uint32_t idx);

void
db_entry_set_mtime(FsearchDatabaseEntry *entry, time_t mtime);

void
db_entry_set_size(FsearchDatabaseEntry *entry, off_t size);

void
db_entry_set_mark(FsearchDatabaseEntry *entry, uint8_t mark);

void
db_entry_set_name(FsearchDatabaseEntry *entry, const char *name);

void
db_entry_set_parent(FsearchDatabaseEntry *entry, FsearchDatabaseEntry *parent);

void
db_entry_set_db_index(FsearchDatabaseEntry *entry, uint32_t db_index);

uint8_t
db_entry_get_mark(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_attribute_flags(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_idx(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_depth(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_db_index(FsearchDatabaseEntry *entry);

GString *
db_entry_get_path(FsearchDatabaseEntry *entry);

GString *
db_entry_get_path_full(FsearchDatabaseEntry *entry);

void
db_entry_append_path(FsearchDatabaseEntry *entry, GString *str);

void
db_entry_append_full_path(FsearchDatabaseEntry *entry, GString *str);

time_t
db_entry_get_mtime(FsearchDatabaseEntry *entry);

off_t
db_entry_get_size(FsearchDatabaseEntry *entry);

const char *
db_entry_get_extension(FsearchDatabaseEntry *entry);

GString *
db_entry_get_name_for_display(FsearchDatabaseEntry *entry);

const char *
db_entry_get_name_raw_for_display(FsearchDatabaseEntry *entry);

const char *
db_entry_get_name_raw(FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
db_entry_get_parent(FsearchDatabaseEntry *entry);

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry);

void
db_entry_free_no_unparent(FsearchDatabaseEntry *entry);

void
db_entry_free(FsearchDatabaseEntry *entry);

void
db_entry_free_full(FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
db_entry_get_deep_copy(FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
db_entry_get_dummy_for_name_and_parent(FsearchDatabaseEntry *parent, const char *name, FsearchDatabaseEntryType type);

void
db_entry_append_content_type(FsearchDatabaseEntry *entry, GString *str);

int
db_entry_compare_entries_by_extension(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_size(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_type(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b, gpointer data);

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_full_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

FsearchDatabaseEntry *
db_entry_new(FsearchDatabaseIndexPropertyFlags attribute_flags,
             const char *name,
             FsearchDatabaseEntry *parent,
             FsearchDatabaseEntryType type);

FsearchDatabaseEntry *
db_entry_new_with_attributes(FsearchDatabaseIndexPropertyFlags attribute_flags,
                             const char *name,
                             FsearchDatabaseEntry *parent,
                             FsearchDatabaseEntryType type,
                             ...);

bool
db_entry_get_attribute_name(FsearchDatabaseEntry *entry, const char **name);

bool
db_entry_get_attribute(FsearchDatabaseEntry *entry, FsearchDatabaseIndexProperty attribute, void *dest, size_t size);

bool
db_entry_set_attribute(FsearchDatabaseEntry *entry, FsearchDatabaseIndexProperty attribute, void *src, size_t size);

size_t *
db_entry_get_attribute_offsets(FsearchDatabaseIndexPropertyFlags attribute_flags);

uint32_t
db_entry_get_member_flags(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_index(FsearchDatabaseEntry *entry);

const char *
db_entry_get_attribute_name_for_offset(FsearchDatabaseEntry *entry, size_t offset);

void
db_entry_get_attribute_for_offest(FsearchDatabaseEntry *entry, size_t offset, void *dest, size_t size);

bool
db_entry_get_attribute_offset(FsearchDatabaseIndexPropertyFlags attribute_flags,
                              FsearchDatabaseIndexProperty attribute,
                              size_t *offset);
void
db_entry_set_unmonitored_fanotify(FsearchDatabaseEntry *entry);

void
db_entry_set_monitored_fanotify(FsearchDatabaseEntry *entry);

bool
db_entry_is_monitored_fanotify(FsearchDatabaseEntry *entry);

void
db_entry_set_unmonitored_inotify(FsearchDatabaseEntry *entry);

void
db_entry_set_monitored_inotify(FsearchDatabaseEntry *entry);

bool
db_entry_is_monitored_inotify(FsearchDatabaseEntry *entry);

FsearchDatabaseEntryFlags
db_entry_get_flags(FsearchDatabaseEntry *entry);
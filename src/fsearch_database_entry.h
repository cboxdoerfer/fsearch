#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "fsearch_array.h"
#include "fsearch_database_index_properties.h"

typedef enum {
    DATABASE_ENTRY_TYPE_NONE,
    DATABASE_ENTRY_TYPE_FOLDER,
    DATABASE_ENTRY_TYPE_FILE,
    NUM_DATABASE_ENTRY_TYPES,
} FsearchDatabaseEntryType;

typedef struct FsearchDatabaseEntry FsearchDatabaseEntry;
typedef struct FsearchDatabaseEntryFile FsearchDatabaseEntryFile;
typedef struct FsearchDatabaseEntryFolder FsearchDatabaseEntryFolder;
typedef struct FsearchDatabaseEntryBase FsearchDatabaseEntryBase;

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
db_entry_is_folder(FsearchDatabaseEntryBase *entry);

bool
db_entry_is_file(FsearchDatabaseEntryBase *entry);

bool
db_entry_is_descendant(FsearchDatabaseEntryBase *entry, FsearchDatabaseEntryBase *maybe_ancestor);

size_t
db_entry_get_sizeof_folder_entry();

size_t
db_entry_get_sizeof_file_entry();

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntryBase *entry);

void
db_entry_set_index(FsearchDatabaseEntryBase *entry, uint32_t idx);

void
db_entry_set_mtime(FsearchDatabaseEntryBase *entry, time_t mtime);

void
db_entry_set_size(FsearchDatabaseEntryBase *entry, off_t size);

void
db_entry_set_mark(FsearchDatabaseEntryBase *entry, uint8_t mark);

void
db_entry_set_name(FsearchDatabaseEntryBase *entry, const char *name);

void
db_entry_set_parent(FsearchDatabaseEntryBase *entry, FsearchDatabaseEntryBase *parent);

void
db_entry_set_type(FsearchDatabaseEntryBase *entry, FsearchDatabaseEntryType type);

void
db_entry_set_db_index(FsearchDatabaseEntryBase *entry, uint32_t db_index);

uint8_t
db_entry_get_mark(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_get_attribute_flags(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_get_idx(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_get_depth(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_get_db_index(FsearchDatabaseEntryBase *entry);

GString *
db_entry_get_path(FsearchDatabaseEntryBase *entry);

GString *
db_entry_get_path_full(FsearchDatabaseEntryBase *entry);

void
db_entry_append_path(FsearchDatabaseEntryBase *entry, GString *str);

void
db_entry_append_full_path(FsearchDatabaseEntryBase *entry, GString *str);

time_t
db_entry_get_mtime(FsearchDatabaseEntryBase *entry);

off_t
db_entry_get_size(FsearchDatabaseEntryBase *entry);

const char *
db_entry_get_extension(FsearchDatabaseEntryBase *entry);

GString *
db_entry_get_name_for_display(FsearchDatabaseEntryBase *entry);

const char *
db_entry_get_name_raw_for_display(FsearchDatabaseEntryBase *entry);

const char *
db_entry_get_name_raw(FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryBase *
db_entry_get_parent(FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntryBase *entry);

void
db_entry_free_full(FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryBase *
db_entry_get_deep_copy(FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryBase *
db_entry_get_dummy_for_name_and_parent(FsearchDatabaseEntryBase *parent, const char *name, FsearchDatabaseEntryType type);

void
db_entry_append_content_type(FsearchDatabaseEntryBase *entry, GString *str);

void
db_entry_destroy(FsearchDatabaseEntryBase *entry);

int
db_entry_compare_entries_by_extension(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_size(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_type(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b, gpointer data);

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_position(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_path(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_full_path(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

int
db_entry_compare_entries_by_name(FsearchDatabaseEntryBase **a, FsearchDatabaseEntryBase **b);

FsearchDatabaseEntryBase *
db_entry_new(uint32_t attribute_flags, const char *name, FsearchDatabaseEntryBase *parent, FsearchDatabaseEntryType type);

bool
db_entry_get_attribute_name(FsearchDatabaseEntryBase *entry, const char **name);

bool
db_entry_get_attribute(FsearchDatabaseEntryBase *entry, FsearchDatabaseIndexProperty attribute, void *dest, size_t size);

bool
db_entry_set_attribute(FsearchDatabaseEntryBase *entry, FsearchDatabaseIndexProperty attribute, void *src, size_t size);

uint32_t
db_entry_get_member_flags(FsearchDatabaseEntryBase *entry);

uint32_t
db_entry_get_index(FsearchDatabaseEntryBase *entry);

const char *
db_entry_get_attribute_name_for_offset(FsearchDatabaseEntryBase *entry, size_t offset);

void
db_entry_get_attribute_for_offest(FsearchDatabaseEntryBase *entry, size_t offset, void *dest, size_t size);

bool
db_entry_get_attribute_offset(FsearchDatabaseIndexPropertyFlags attribute_flags,
                              FsearchDatabaseIndexProperty attribute,
                              size_t *offset);

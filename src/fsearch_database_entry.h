#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DATABASE_ENTRY_TYPE_NONE,
    DATABASE_ENTRY_TYPE_FOLDER,
    DATABASE_ENTRY_TYPE_FILE,
    NUM_DATABASE_ENTRY_TYPES,
} FsearchDatabaseEntryType;

typedef struct FsearchDatabaseEntry FsearchDatabaseEntry;
typedef struct FsearchDatabaseEntryFile FsearchDatabaseEntryFile;
typedef struct FsearchDatabaseEntryFolder FsearchDatabaseEntryFolder;

typedef struct FsearchDatabaseEntryCompareContext {
    GHashTable *file_type_table;
    GHashTable *entry_to_file_type_table;
} FsearchDatabaseEntryCompareContext;

bool
db_entry_is_folder(FsearchDatabaseEntry *entry);

bool
db_entry_is_file(FsearchDatabaseEntry *entry);

size_t
db_entry_get_sizeof_folder_entry();

size_t
db_entry_get_sizeof_file_entry();

uint32_t
db_entry_folder_get_num_children(FsearchDatabaseEntryFolder *entry);

uint32_t
db_entry_folder_get_num_files(FsearchDatabaseEntryFolder *entry);

uint32_t
db_entry_folder_get_num_folders(FsearchDatabaseEntryFolder *entry);

void
db_entry_set_idx(FsearchDatabaseEntry *entry, uint32_t idx);

void
db_entry_set_mtime(FsearchDatabaseEntry *entry, time_t mtime);

void
db_entry_set_size(FsearchDatabaseEntry *entry, off_t size);

void
db_entry_set_mark(FsearchDatabaseEntry *entry, uint8_t mark);

void
db_entry_set_name(FsearchDatabaseEntry *entry, const char *name);

void
db_entry_set_parent(FsearchDatabaseEntry *entry, FsearchDatabaseEntryFolder *parent);

void
db_entry_set_type(FsearchDatabaseEntry *entry, FsearchDatabaseEntryType type);

void
db_entry_update_parent_size(FsearchDatabaseEntry *entry);

// Decrement parent file/folder count when removing entry
// Call before removing entry from database
void
db_entry_unset_parent(FsearchDatabaseEntry *entry);

// Subtract size from parent chain (reverse of db_entry_update_parent_size)
// Call when removing entry from database
void
db_entry_subtract_parent_size(FsearchDatabaseEntry *entry);

// Clear entry for reuse (called before returning to memory pool)
void
db_entry_clear(FsearchDatabaseEntry *entry);

uint8_t
db_entry_get_mark(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_idx(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_depth(FsearchDatabaseEntry *entry);

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

FsearchDatabaseEntryFolder *
db_entry_get_parent(FsearchDatabaseEntry *entry);

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry);

void
db_entry_append_content_type(FsearchDatabaseEntry *entry, GString *str);

void
db_entry_destroy(FsearchDatabaseEntry *entry);

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
db_entry_compare_entries_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

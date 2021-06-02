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

typedef struct FsearchDatabaseEntryFile FsearchDatabaseEntry;
typedef struct FsearchDatabaseEntryFile FsearchDatabaseEntryFile;
typedef struct FsearchDatabaseEntryFolder FsearchDatabaseEntryFolder;

size_t
db_entry_get_sizeof_folder_entry();

size_t
db_entry_get_sizeof_file_entry();

void
db_entry_set_idx(FsearchDatabaseEntry *entry, uint32_t idx);

void
db_entry_set_mtime(FsearchDatabaseEntry *entry, time_t mtime);

void
db_entry_set_size(FsearchDatabaseEntry *entry, off_t size);

void
db_entry_set_name(FsearchDatabaseEntry *entry, const char *name);

void
db_entry_set_parent(FsearchDatabaseEntry *entry, FsearchDatabaseEntryFolder *parent);

void
db_entry_set_type(FsearchDatabaseEntry *entry, FsearchDatabaseEntryType type);

void
db_entry_update_parent_size(FsearchDatabaseEntry *entry);

uint32_t
db_entry_get_idx(FsearchDatabaseEntry *entry);

GString *
db_entry_get_path(FsearchDatabaseEntry *entry);

GString *
db_entry_get_path_full(FsearchDatabaseEntry *entry);

void
db_entry_append_path(FsearchDatabaseEntry *entry, GString *str);

time_t
db_entry_get_mtime(FsearchDatabaseEntry *entry);

off_t
db_entry_get_size(FsearchDatabaseEntry *entry);

const char *
db_entry_get_extension(FsearchDatabaseEntry *entry);

const char *
db_entry_get_name(FsearchDatabaseEntry *entry);

const char *
db_entry_get_name_raw(FsearchDatabaseEntry *entry);

FsearchDatabaseEntryFolder *
db_entry_get_parent(FsearchDatabaseEntry *entry);

FsearchDatabaseEntryType
db_entry_get_type(FsearchDatabaseEntry *entry);

void
db_file_entry_destroy(FsearchDatabaseEntryFolder *entry);

void
db_folder_entry_destroy(FsearchDatabaseEntryFolder *entry);

int
db_entry_compare_entries_by_extension(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_size(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_type(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_position(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

int
db_entry_compare_entries_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b);

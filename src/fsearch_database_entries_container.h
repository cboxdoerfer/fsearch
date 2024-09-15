#pragma once

#include <gio/gio.h>

#include "fsearch_array.h"
#include "fsearch_database_sort.h"

G_BEGIN_DECLS

#define FSEARCH_DATABASE_ENTRIES_CONTAINER (fsearch_database_entries_container_get_type())

typedef struct _FsearchDatabaseEntriesContainer FsearchDatabaseEntriesContainer;

GType
fsearch_database_entries_container_get_type(void);

FsearchDatabaseEntriesContainer *
fsearch_database_entries_container_new(DynamicArray *array,
                                       gboolean is_array_sorted,
                                       FsearchDatabaseIndexProperty sort_order,
                                       FsearchDatabaseIndexProperty secondary_sort_order,
                                       FsearchDatabaseEntryType entry_type,
                                       GCancellable *cancellable);

FsearchDatabaseEntriesContainer *
fsearch_database_entries_container_ref(FsearchDatabaseEntriesContainer *self);

void
fsearch_database_entries_container_unref(FsearchDatabaseEntriesContainer *self);

void
fsearch_database_entries_container_balance(FsearchDatabaseEntriesContainer *self);

void
fsearch_database_entries_container_insert(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryBase *
fsearch_database_entries_container_steal(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntryBase *entry);

DynamicArray *
fsearch_database_entries_container_steal_descendants(FsearchDatabaseEntriesContainer *self,
                                                     FsearchDatabaseEntryBase *folder,
                                                     int32_t num_known_descendants);

FsearchDatabaseEntryBase *
fsearch_database_entries_container_find(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntryBase *entry);

FsearchDatabaseEntryBase *
fsearch_database_entries_container_get_entry(FsearchDatabaseEntriesContainer *self, uint32_t idx);

uint32_t
fsearch_database_entries_container_get_num_entries(FsearchDatabaseEntriesContainer *self);

DynamicArray *
fsearch_database_entries_container_get_containers(FsearchDatabaseEntriesContainer *self);

DynamicArray *
fsearch_database_entries_container_get_joined(FsearchDatabaseEntriesContainer *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseEntriesContainer, fsearch_database_entries_container_unref)

G_END_DECLS

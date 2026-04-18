#pragma once

#include <gio/gio.h>

#include "fsearch_array.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_entry.h"

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_CHUNKED_ARRAY (fsearch_database_chunked_array_get_type())

typedef struct _FsearchDatabaseChunkedArray FsearchDatabaseChunkedArray;

GType
fsearch_database_chunked_array_get_type(void);

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_new(DynamicArray *array,
                                   gboolean is_array_sorted,
                                   FsearchDatabaseIndexProperty sort_order,
                                   FsearchDatabaseIndexProperty secondary_sort_order,
                                   FsearchDatabaseEntryType entry_type,
                                   GCancellable *cancellable,
                                   GDestroyNotify entry_free_func);

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_ref(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_unref(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_balance(FsearchDatabaseChunkedArray *self);

void
fsearch_database_chunked_array_insert(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

void
fsearch_database_chunked_array_insert_array(FsearchDatabaseChunkedArray *self, DynamicArray *array);

FsearchDatabaseEntry *
fsearch_database_chunked_array_steal(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

DynamicArray *
fsearch_database_chunked_array_steal_descendants(FsearchDatabaseChunkedArray *self,
                                                 FsearchDatabaseEntry *folder,
                                                 int32_t num_known_descendants);

FsearchDatabaseEntry *
fsearch_database_chunked_array_find(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry);

FsearchDatabaseEntry *
fsearch_database_chunked_array_get_entry(FsearchDatabaseChunkedArray *self, uint32_t idx);

uint32_t
fsearch_database_chunked_array_get_num_entries(FsearchDatabaseChunkedArray *self);

DynamicArray *
fsearch_database_chunked_array_get_chunks(FsearchDatabaseChunkedArray *self);

DynamicArray *
fsearch_database_chunked_array_get_joined(FsearchDatabaseChunkedArray *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseChunkedArray, fsearch_database_chunked_array_unref)

G_END_DECLS
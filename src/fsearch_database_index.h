#pragma once

#include "fsearch_array.h"
#include "fsearch_memory_pool.h"

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_INDEX (fsearch_database_index_get_type())

typedef struct {
    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;
    DynamicArray *files[NUM_DATABASE_INDEX_PROPERTIES];
    DynamicArray *folders[NUM_DATABASE_INDEX_PROPERTIES];

    FsearchDatabaseIndexPropertyFlags flags;

    uint32_t id;
} FsearchDatabaseIndex;

void
fsearch_database_index_free(FsearchDatabaseIndex *index);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndex, fsearch_database_index_free)

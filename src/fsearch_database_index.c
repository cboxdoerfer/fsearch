#include "fsearch_database_index.h"

void
fsearch_database_index_free(FsearchDatabaseIndex *index) {
    g_return_if_fail(index);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        g_clear_pointer(&index->files[i], darray_unref);
        g_clear_pointer(&index->folders[i], darray_unref);
    }

    g_clear_pointer(&index->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&index->folder_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&index, free);
}

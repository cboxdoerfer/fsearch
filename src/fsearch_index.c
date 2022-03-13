#define _GNU_SOURCE
#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_index.h"

FsearchIndex *
fsearch_index_new(FsearchIndexType type,
                  const char *path,
                  bool search_in,
                  bool update,
                  bool one_filesystem,
                  time_t last_updated) {
    FsearchIndex *index = calloc(1, sizeof(FsearchIndex));
    g_assert_nonnull(index);

    index->type = type;
    index->path = path ? strdup(path) : strdup("");
    index->enabled = search_in;
    index->update = update;
    index->one_filesystem = one_filesystem;
    index->last_updated = last_updated;

    return index;
}

FsearchIndex *
fsearch_index_copy(FsearchIndex *index) {
    if (!index) {
        return NULL;
    }
    return fsearch_index_new(index->type,
                             index->path,
                             index->enabled,
                             index->update,
                             index->one_filesystem,
                             index->last_updated);
}

void
fsearch_index_free(FsearchIndex *index) {
    if (!index) {
        return;
    }

    g_clear_pointer(&index->path, free);
    g_clear_pointer(&index, free);
}

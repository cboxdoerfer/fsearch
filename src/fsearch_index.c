#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_index.h"

FsearchIndex *
fsearch_index_new(FsearchIndexType type, const char *path, bool search_in, bool update, time_t last_updated) {
    FsearchIndex *index = calloc(1, sizeof(FsearchIndex));
    assert(index != NULL);

    index->type = type;
    index->path = path ? strdup(path) : strdup("");
    index->enabled = search_in;
    index->update = update;
    index->last_updated = last_updated;

    return index;
}

FsearchIndex *
fsearch_index_copy(FsearchIndex *index) {
    if (!index) {
        return NULL;
    }
    return fsearch_index_new(index->type, index->path, index->enabled, index->update, index->last_updated);
}

void
fsearch_index_free(FsearchIndex *index) {
    if (!index) {
        return;
    }

    if (index->path) {
        free(index->path);
        index->path = NULL;
    }
    free(index);
    index = NULL;

    return;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef enum {
    FSEARCH_INDEX_FOLDER_TYPE,
    NUM_FSEARCH_INDEX_TYPES,
} FsearchIndexType;

typedef struct _FsearchIndex {
    FsearchIndexType type;

    char *path;
    bool enabled;
    bool update;

    time_t last_updated;
} FsearchIndex;

FsearchIndex *
fsearch_index_new(FsearchIndexType type, const char *path, bool search_in, bool update, time_t last_updated);

FsearchIndex *
fsearch_index_copy(FsearchIndex *index);

void
fsearch_index_free(FsearchIndex *index);

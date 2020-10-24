#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct _FsearchIncludePath {
    char *path;
    bool enabled;
    bool update;

    uint32_t num_items;
    time_t last_updated;
} FsearchIncludePath;

FsearchIncludePath *
fsearch_include_path_new(const char *path, bool search_in, bool update, uint32_t num_items, time_t last_updated);

FsearchIncludePath *
fsearch_include_path_copy(FsearchIncludePath *src);

void
fsearch_include_path_free(FsearchIncludePath *fs_path);


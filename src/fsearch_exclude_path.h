#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct _FsearchExcludePath {
    char *path;
    bool enabled;
} FsearchExcludePath;

FsearchExcludePath *
fsearch_exclude_path_new(const char *path, bool enabled);

FsearchExcludePath *
fsearch_exclude_path_copy(FsearchExcludePath *src);

void
fsearch_exclude_path_free(FsearchExcludePath *fs_path);


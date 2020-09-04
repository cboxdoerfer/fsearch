#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_exclude_path.h"

FsearchExcludePath *
fsearch_exclude_path_new(const char *path, bool enabled) {
    FsearchExcludePath *fs_path = calloc(1, sizeof(FsearchExcludePath));
    assert(fs_path != NULL);

    if (path) {
        fs_path->path = strdup(path);
    }
    fs_path->enabled = enabled;

    return fs_path;
}

FsearchExcludePath *
fsearch_exclude_path_copy(FsearchExcludePath *src) {
    if (!src) {
        return NULL;
    }
    return fsearch_exclude_path_new(src->path, src->enabled);
}

void
fsearch_exclude_path_free(FsearchExcludePath *fs_path) {
    if (!fs_path) {
        return;
    }

    if (fs_path->path) {
        free(fs_path->path);
        fs_path->path = NULL;
    }
    free(fs_path);
    fs_path = NULL;

    return;
}


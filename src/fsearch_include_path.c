#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_include_path.h"

FsearchIncludePath *
fsearch_include_path_new(const char *path, bool enabled, bool update, time_t last_updated) {
    FsearchIncludePath *fs_path = calloc(1, sizeof(FsearchIncludePath));
    assert(fs_path != NULL);

    if (path) {
        fs_path->path = strdup(path);
    }
    fs_path->enabled = enabled;
    fs_path->update = update;
    fs_path->last_updated = last_updated;

    return fs_path;
}

FsearchIncludePath *
fsearch_include_path_copy(FsearchIncludePath *src) {
    if (!src) {
        return NULL;
    }
    return fsearch_include_path_new(src->path, src->enabled, src->update, src->last_updated);
}

void
fsearch_include_path_free(FsearchIncludePath *fs_path) {
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


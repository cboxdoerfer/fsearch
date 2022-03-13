#define _GNU_SOURCE
#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_exclude_path.h"

FsearchExcludePath *
fsearch_exclude_path_new(const char *path, bool enabled) {
    FsearchExcludePath *fs_path = calloc(1, sizeof(FsearchExcludePath));
    g_assert_nonnull(fs_path);

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

    g_clear_pointer(&fs_path->path, free);
    g_clear_pointer(&fs_path, free);
}

#pragma once

#include <glib.h>
#include <stdbool.h>

#include "fsearch_query_flags.h"

typedef enum FsearchFilterFileType {
    FSEARCH_FILTER_NONE,
    FSEARCH_FILTER_FOLDERS,
    FSEARCH_FILTER_FILES,
} FsearchFilterFileType;

typedef struct FsearchFilter {
    FsearchFilterFileType type;
    char *name;
    char *query;
    FsearchQueryFlags flags;

    volatile int ref_count;
} FsearchFilter;

FsearchFilter *
fsearch_filter_new(FsearchFilterFileType type, const char *name, const char *query, FsearchQueryFlags flags);

FsearchFilter *
fsearch_filter_ref(FsearchFilter *filter);

void
fsearch_filter_unref(FsearchFilter *filter);

GList *
fsearch_filter_get_default();

#pragma once

#include <glib.h>
#include <stdbool.h>

#include "fsearch_query_flags.h"

typedef struct FsearchFilter {
    char *name;
    char *query;
    FsearchQueryFlags flags;

    volatile int ref_count;
} FsearchFilter;

FsearchFilter *
fsearch_filter_new(const char *name, const char *query, FsearchQueryFlags flags);

FsearchFilter *
fsearch_filter_ref(FsearchFilter *filter);

void
fsearch_filter_unref(FsearchFilter *filter);

GList *
fsearch_filter_get_default();

#pragma once

#include <glib.h>
#include <stdbool.h>

typedef enum FsearchFilterFileType {
    FSEARCH_FILTER_NONE,
    FSEARCH_FILTER_FOLDERS,
    FSEARCH_FILTER_FILES,
} FsearchFilterFileType;

typedef struct FsearchFilter {
    FsearchFilterFileType type;
    char *name;
    char *query;
    bool match_case;
    bool enable_regex;
    bool search_in_path;
} FsearchFilter;

FsearchFilter *
fsearch_filter_new(FsearchFilterFileType type,
                   const char *name,
                   const char *query,
                   bool match_case,
                   bool enable_regex,
                   bool search_in_path);

void
fsearch_filter_free(FsearchFilter *filter);

GList *
fsearch_filter_get_default();


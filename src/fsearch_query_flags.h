#pragma once

#include <stdbool.h>

typedef enum FsearchQueryFlags {
    QUERY_FLAG_MATCH_CASE = 1 << 0,
    QUERY_FLAG_AUTO_MATCH_CASE = 1 << 1,
    QUERY_FLAG_REGEX = 1 << 2,
    QUERY_FLAG_SEARCH_IN_PATH = 1 << 3,
    QUERY_FLAG_AUTO_SEARCH_IN_PATH = 1 << 4,
} FsearchQueryFlags;

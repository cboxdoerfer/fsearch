#pragma once

#include <stdbool.h>

typedef struct FsearchQueryFlags {
    bool match_case;
    bool auto_match_case;
    bool enable_regex;
    bool search_in_path;
    bool auto_search_in_path;
} FsearchQueryFlags;

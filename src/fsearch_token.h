
#pragma once

#include <pcre.h>
#include <stdbool.h>
#include <stdint.h>

#define OVECCOUNT 3

typedef struct FsearchToken {

    char *text;
    size_t text_len;

    uint32_t has_separator;
    uint32_t (*search_func)(const char *, const char *, void *data);

    pcre *regex;
    pcre_extra *regex_study;
    int ovector[OVECCOUNT];
} FsearchToken;

FsearchToken **
fsearch_tokens_new(const char *query, bool match_case, bool enable_regex, bool auto_match_case);

void
fsearch_tokens_free(FsearchToken **tokens);


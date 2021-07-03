
#pragma once

#include <pcre.h>
#include <stdbool.h>
#include <stdint.h>
#include <unicode/ucasemap.h>

#include "fsearch_query_flags.h"

#define OVECCOUNT 3

typedef struct FsearchToken {

    char *text;
    size_t text_len;

    uint32_t has_separator;
    uint32_t (*search_func)(const char *, const char *, void *token, char *haystack_buffer, size_t haystack_buffer_len);

    pcre *regex;
    pcre_extra *regex_study;
    int ovector[OVECCOUNT];

    UCaseMap *case_map;
    char *needle_down;
    int32_t needle_down_len;
} FsearchToken;

FsearchToken **
fsearch_tokens_new(const char *query, FsearchQueryFlags flags);

void
fsearch_tokens_free(FsearchToken **tokens);


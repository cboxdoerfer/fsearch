
#pragma once

#include "query.h"

#include <pcre.h>

#define OVECCOUNT 3

typedef struct {
    char *text;
    size_t text_len;

    uint32_t has_separator;
    uint32_t (*search_func)(const char *, const char *, void *data);

    pcre *regex;
    pcre_extra *regex_study;
    int ovector[OVECCOUNT];
} FsearchToken;

FsearchToken **
fsearch_tokens_new(FsearchQuery *q);

void
fsearch_tokens_free(FsearchToken **tokens, uint32_t num_token);


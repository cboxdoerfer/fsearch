
#pragma once

#include <pcre.h>
#include <stdbool.h>
#include <stdint.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>

#include "fsearch_query_flags.h"
#include "fsearch_query_match_context.h"
#include "fsearch_utf.h"

#define OVECCOUNT 3

typedef struct FsearchToken FsearchToken;
typedef uint32_t(FsearchTokenSearchFunc)(FsearchToken *, FsearchQueryMatchContext *);

struct FsearchToken {

    char *search_term;
    size_t search_term_len;

    uint32_t has_separator;
    FsearchTokenSearchFunc *search_func;

    UCaseMap *case_map;
    const UNormalizer2 *normalizer;

    FsearchUtfConversionBuffer *needle_buffer;

    uint32_t fold_options;

    pcre *regex;
    pcre_extra *regex_study;
    int ovector[OVECCOUNT];

    int32_t wildcard_flags;
    int32_t is_utf;
};

FsearchToken **
fsearch_tokens_new(const char *search_term, FsearchQueryFlags flags, uint32_t *num_token);

void
fsearch_tokens_free(FsearchToken **tokens);

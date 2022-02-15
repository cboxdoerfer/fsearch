
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

typedef enum FsearchTokenType {
    FSEARCH_TOKEN_TYPE_NORMAL,
    FSEARCH_TOKEN_TYPE_FUNC_SIZE,
    NUM_FSEARCH_TOKEN_TYPES,
} FsearchTokenType;

typedef enum FsearchTokenSizeComparisonType {
    FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL,
    FSEARCH_TOKEN_SIZE_COMPARISON_GREATER,
    FSEARCH_TOKEN_SIZE_COMPARISON_GREATER_EQ,
    FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER,
    FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER_EQ,
} FsearchTokenSizeComparisonType;

struct FsearchToken {
    FsearchTokenType type;

    char *search_term;
    size_t search_term_len;

    off_t size;
    FsearchTokenSizeComparisonType size_comparison_type;

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


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

typedef struct FsearchQueryNode FsearchQueryNode;
typedef uint32_t(FsearchQueryNodeSearchFunc)(FsearchQueryNode *, FsearchQueryMatchContext *);

typedef enum FsearchQueryNodeType {
    FSEARCH_QUERY_NODE_TYPE_OPERATOR,
    FSEARCH_QUERY_NODE_TYPE_QUERY,
    NUM_FSEARCH_QUERY_NODE_TYPES,
} FsearchQueryNodeType;

typedef enum FsearchTokenComparisonType {
    FSEARCH_TOKEN_COMPARISON_EQUAL,
    FSEARCH_TOKEN_COMPARISON_GREATER,
    FSEARCH_TOKEN_COMPARISON_GREATER_EQ,
    FSEARCH_TOKEN_COMPARISON_SMALLER,
    FSEARCH_TOKEN_COMPARISON_SMALLER_EQ,
    FSEARCH_TOKEN_COMPARISON_RANGE,
} FsearchTokenComparisonType;

typedef enum FsearchQueryNodeOperator {
    FSEARCH_TOKEN_OPERATOR_AND,
    FSEARCH_TOKEN_OPERATOR_OR,
    FSEARCH_TOKEN_OPERATOR_NOT,
    NUM_FSEARCH_TOKEN_OPERATORS,
} FsearchQueryNodeOperator;

struct FsearchQueryNode {
    FsearchQueryNodeType type;

    FsearchQueryNodeOperator operator;

    char *search_term;
    size_t search_term_len;

    int64_t size;
    int64_t size_upper_limit;
    FsearchTokenComparisonType size_comparison_type;

    uint32_t has_separator;
    FsearchQueryNodeSearchFunc *search_func;

    UCaseMap *case_map;
    const UNormalizer2 *normalizer;

    FsearchUtfConversionBuffer *needle_buffer;

    uint32_t fold_options;

    pcre *regex;
    pcre_extra *regex_study;
    int ovector[OVECCOUNT];

    int32_t wildcard_flags;

    FsearchQueryFlags flags;
};

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchQueryFlags flags);

void
fsearch_query_node_tree_free(GNode *node);

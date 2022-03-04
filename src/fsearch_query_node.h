
#pragma once

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pango/pango-attributes.h>
#include <pcre2.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>

#include "fsearch_database_index.h"
#include "fsearch_filter_manager.h"
#include "fsearch_query_flags.h"
#include "fsearch_query_match_data.h"
#include "fsearch_utf.h"

typedef struct FsearchQueryNode FsearchQueryNode;
typedef struct FsearchQueryNodeHighlight FsearchQueryNodeHighlight;
typedef uint32_t(FsearchQueryNodeSearchFunc)(FsearchQueryNode *, FsearchQueryMatchData *);
typedef bool(FsearchQueryNodeHighlightFunc)(FsearchQueryNode *, FsearchQueryMatchData *);

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
    GString *query_description;

    FsearchQueryNodeOperator operator;

    char *needle;
    size_t needle_len;

    char **search_term_list;
    uint32_t num_search_term_list_entries;

    int64_t size;
    int64_t size_upper_limit;
    time_t time;
    time_t time_upper_limit;
    FsearchTokenComparisonType comparison_type;

    uint32_t has_separator;
    FsearchQueryNodeSearchFunc *search_func;
    FsearchQueryNodeHighlightFunc *highlight_func;

    FsearchUtfBuilder *needle_builder;

    uint32_t fold_options;

    // Using the pcre2_code with multiple threads is safe.
    // However, pcre2_match_data can't be shared across threads.
    // So to avoid frequent calls to pcre2_match_data_create_from_pattern during the matching process,
    // we simply generate an array which holds a unique instance for each thread per regex node.
    pcre2_code *regex;
    GPtrArray *regex_match_data_for_threads;
    bool regex_jit_available;

    FsearchQueryFlags flags;
};

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchFilterManager *filters, FsearchQueryFlags flags);

void
fsearch_query_node_tree_free(GNode *node);

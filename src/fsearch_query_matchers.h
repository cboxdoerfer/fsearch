#pragma once

#include "fsearch_query_match_data.h"
#include "fsearch_query_node.h"

#include <stdbool.h>
#include <stdint.h>

uint32_t
fsearch_query_matcher_func_false(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_true(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_date_modified(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_utf(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_func_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_func_none(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_func_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_func_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_func_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_func_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

#pragma once

#include "fsearch_query_match_data.h"
#include "fsearch_query_node.h"

#include <stdbool.h>
#include <stdint.h>

uint32_t
fsearch_query_matcher_false(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_true(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_date_modified(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childcount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childfilecount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childfoldercount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_utf_strcasecmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_utf_strcasestr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strstr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcasestr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcasecmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_none(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

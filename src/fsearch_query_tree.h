#pragma once

#include "fsearch_filter_manager.h"

#include <glib.h>

bool
fsearch_query_node_tree_triggers_auto_match_path(GNode *tree);

bool
fsearch_query_node_tree_triggers_auto_match_case(GNode *tree);

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchFilterManager *filters, FsearchQueryFlags flags);

void
fsearch_query_node_tree_free(GNode *node);

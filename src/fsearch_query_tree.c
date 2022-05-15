#define G_LOG_DOMAIN "fsearch-query-tree"

#include "fsearch_query_tree.h"
#include "fsearch_query_node.h"
#include "fsearch_query_parser.h"
#include "fsearch_string_utils.h"

static gboolean
free_tree_node(GNode *node, gpointer data);

static void
free_tree(GNode *root) {
    g_node_traverse(root, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_tree_node, NULL);
    g_clear_pointer(&root, g_node_destroy);
}

static gboolean
free_tree_node(GNode *node, gpointer data) {
    FsearchQueryNode *n = node->data;
    g_clear_pointer(&n, fsearch_query_node_free);
    return FALSE;
}

static GNode *
get_everything_matching_node(FsearchQueryFlags flags) {
    return g_node_new(fsearch_query_node_new_match_everything(flags));
}

static GNode *
build_query_tree_from_suffix_list(GList *postfix_query, FsearchQueryFlags flags) {
    if (!postfix_query) {
        return get_everything_matching_node(flags);
    }

    GQueue *query_stack = g_queue_new();

    for (GList *n = postfix_query; n != NULL; n = n->next) {
        FsearchQueryNode *node = n->data;
        g_assert(node);

        if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
            GNode *op_node = g_node_new(node);
            GNode *right = g_queue_pop_tail(query_stack);
            if (node->operator!= FSEARCH_QUERY_NODE_OPERATOR_NOT) {
                GNode *left = g_queue_pop_tail(query_stack);
                g_node_append(op_node, left ? left : get_everything_matching_node(flags));
            }
            g_node_append(op_node, right ? right : get_everything_matching_node(flags));
            g_queue_push_tail(query_stack, op_node);
        }
        else {
            g_queue_push_tail(query_stack, g_node_new(node));
        }
    }
    GNode *root = g_queue_pop_tail(query_stack);
    if (!g_queue_is_empty(query_stack)) {
        g_critical("[get_query_tree] query stack still has nodes left!!");
    }

    g_queue_free_full(g_steal_pointer(&query_stack), (GDestroyNotify)free_tree);
    return root;
}

static char *
query_flags_to_string_expressive(FsearchQueryFlags flags) {
    GString *flag_string = g_string_sized_new(10);
    uint32_t num_flags = 0;
    const char *sep = ", ";
    if (flags & QUERY_FLAG_EXACT_MATCH) {
        g_string_append_printf(flag_string, "%sExact Match", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_AUTO_MATCH_CASE) {
        g_string_append_printf(flag_string, "%sAuto Match Case", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_MATCH_CASE) {
        g_string_append_printf(flag_string, "%sMatch Case", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_AUTO_SEARCH_IN_PATH) {
        g_string_append_printf(flag_string, "%sAuto Search in Path", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_SEARCH_IN_PATH) {
        g_string_append_printf(flag_string, "%sSearch in Path", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_REGEX) {
        g_string_append_printf(flag_string, "%sRegex", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_FOLDERS_ONLY) {
        g_string_append_printf(flag_string, "%sFolders only", num_flags ? sep : "");
        num_flags++;
    }
    if (flags & QUERY_FLAG_FILES_ONLY) {
        g_string_append_printf(flag_string, "%sFiles only", num_flags ? sep : "");
        num_flags++;
    }
    return g_string_free(flag_string, FALSE);
}

static char *
query_flags_to_string(FsearchQueryFlags flags) {
    GString *flag_string = g_string_sized_new(10);
    if (flags & QUERY_FLAG_EXACT_MATCH) {
        g_string_append_c(flag_string, 'e');
    }
    if (flags & QUERY_FLAG_AUTO_MATCH_CASE) {
        g_string_append_c(flag_string, 'C');
    }
    if (flags & QUERY_FLAG_MATCH_CASE) {
        g_string_append_c(flag_string, 'c');
    }
    if (flags & QUERY_FLAG_AUTO_SEARCH_IN_PATH) {
        g_string_append_c(flag_string, 'P');
    }
    if (flags & QUERY_FLAG_SEARCH_IN_PATH) {
        g_string_append_c(flag_string, 'p');
    }
    if (flags & QUERY_FLAG_REGEX) {
        g_string_append_c(flag_string, 'r');
    }
    if (flags & QUERY_FLAG_FOLDERS_ONLY) {
        g_string_append_c(flag_string, 'F');
    }
    if (flags & QUERY_FLAG_FILES_ONLY) {
        g_string_append_c(flag_string, 'f');
    }
    return g_string_free(flag_string, FALSE);
}

static GPtrArray *
get_filters_with_macros(FsearchFilterManager *manager) {
    GPtrArray *macros = g_ptr_array_sized_new(10);
    g_ptr_array_set_free_func(macros, (GDestroyNotify)fsearch_filter_unref);

    if (manager) {
        for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(manager); ++i) {
            FsearchFilter *filter = fsearch_filter_manager_get_filter(manager, i);
            if (filter && filter->macro && !fsearch_string_is_empty(filter->macro)) {
                g_ptr_array_add(macros, fsearch_filter_ref(filter));
            }
            g_clear_pointer(&filter, fsearch_filter_unref);
        }
    }
    return macros;
}

static void
print_parser_result(const char *input, FsearchQueryFlags flags, GList *result) {
    if (!result) {
        return;
    }
    g_debug("[QueryParser]");
    g_debug(" * global_flags: %s", query_flags_to_string_expressive(flags));
    g_debug(" * input: %s", input);
    g_autoptr(GString) result_str = g_string_new(" * output: ");
    for (GList *n = result; n != NULL; n = n->next) {
        FsearchQueryNode *node = n->data;
        g_assert(node);
        if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
            g_string_append(result_str, node->description->str);
            g_string_append_c(result_str, ' ');
        }
        else {
            g_autofree char *flag_string = query_flags_to_string(node->flags);
            g_string_append_printf(result_str,
                                   "[%s:'%s':%s] ",
                                   node->description ? node->description->str : "unknown query",
                                   node->needle ? node->needle : "",
                                   flag_string);
        }
    }
    g_debug("%s", result_str->str);
}

static GNode *
get_query_tree(const char *input, FsearchFilterManager *filters, FsearchQueryFlags flags) {
    g_assert(input);

    FsearchQueryParseContext *parse_context = calloc(1, sizeof(FsearchQueryParseContext));
    g_assert(parse_context);
    parse_context->lexer = fsearch_query_lexer_new(input);
    parse_context->macro_filters = get_filters_with_macros(filters);
    parse_context->macro_stack = g_queue_new();

    parse_context->last_token = FSEARCH_QUERY_TOKEN_NONE;
    parse_context->operator_stack = g_queue_new();
    GList *suffix_list = fsearch_query_parser_parse_expression(parse_context, false, flags);

    print_parser_result(input, flags, suffix_list);

    GNode *root = build_query_tree_from_suffix_list(suffix_list, flags);

    g_clear_pointer(&suffix_list, g_list_free);
    g_clear_pointer(&parse_context->lexer, fsearch_query_lexer_free);
    g_clear_pointer(&parse_context->macro_stack, g_queue_free);
    g_clear_pointer(&parse_context->operator_stack, g_queue_free);
    g_clear_pointer(&parse_context->macro_filters, g_ptr_array_unref);
    g_clear_pointer(&parse_context, free);
    return root;
}

static gboolean
node_triggers_auto_match_path(GNode *node, gpointer data) {
    g_assert(data);
    FsearchQueryNode *n = node->data;
    bool *triggers_auto_match_path = data;
    if (*triggers_auto_match_path == false) {
        *triggers_auto_match_path = n->triggers_auto_match_path;
    }
    return FALSE;
}

bool
fsearch_query_node_tree_triggers_auto_match_path(GNode *tree) {
    g_assert(tree);
    bool triggers_auto_match_path = false;

    g_node_traverse(tree, G_IN_ORDER, G_TRAVERSE_ALL, -1, node_triggers_auto_match_path, &triggers_auto_match_path);

    return triggers_auto_match_path;
}

static gboolean
node_triggers_auto_match_case(GNode *node, gpointer data) {
    g_assert(data);
    FsearchQueryNode *n = node->data;
    bool *triggers_auto_match_case = data;
    if (*triggers_auto_match_case == false) {
        *triggers_auto_match_case = n->triggers_auto_match_case;
    }
    return FALSE;
}

bool
fsearch_query_node_tree_triggers_auto_match_case(GNode *tree) {
    g_assert(tree);
    bool triggers_auto_match_case = false;

    g_node_traverse(tree, G_IN_ORDER, G_TRAVERSE_ALL, -1, node_triggers_auto_match_case, &triggers_auto_match_case);

    return triggers_auto_match_case;
}

static gboolean
node_wants_single_threaded_search(GNode *node, gpointer data) {
    g_assert(data);
    FsearchQueryNode *n = node->data;
    bool *wants_single_threaded_search = data;
    if (*wants_single_threaded_search == false) {
        *wants_single_threaded_search = n->wants_single_threaded_search;
    }
    return FALSE;
}

bool
fsearch_query_node_tree_wants_single_threaded_search(GNode *tree) {
    g_assert(tree);
    bool wants_single_threaded_search = false;

    g_node_traverse(tree, G_IN_ORDER, G_TRAVERSE_ALL, -1, node_wants_single_threaded_search, &wants_single_threaded_search);

    return wants_single_threaded_search;
}

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchFilterManager *filters, FsearchQueryFlags flags) {
    g_autofree char *query = g_strdup(search_term);
    char *query_stripped = g_strstrip(query);
    GNode *res = NULL;
    if (flags & QUERY_FLAG_REGEX) {
        // If we're in regex mode we're passing the whole query to the regex engine
        // i.e. there's only one query node
        res = g_node_new(fsearch_query_node_new(query_stripped, flags));
    }
    else {
        res = get_query_tree(query_stripped, filters, flags);
    }
    return res;
}

void
fsearch_query_node_tree_free(GNode *node) {
    if (!node) {
        return;
    }
    g_clear_pointer(&node, free_tree);
}

#include "fsearch_query_parser.h"
#include "fsearch_query_lexer.h"
#include "fsearch_query_matchers.h"
#include "fsearch_query_node.h"
#include "fsearch_size_utils.h"
#include "fsearch_string_utils.h"
#include "fsearch_time_utils.h"

#include <assert.h>
#include <stdbool.h>

static GList *
handle_open_bracket_token(FsearchQueryParseContext *parse_ctx);

static GList *
handle_operator_token(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token);

static GList *
parse_field(FsearchQueryLexer *lexer,
            FsearchQueryParseContext *parse_ctx,
            GString *field_name,
            bool is_empty_field,
            FsearchQueryFlags flags);

static GList *
parse_modifier(FsearchQueryLexer *lexer, FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_exact(FsearchQueryLexer *lexer,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags);

static GList *
parse_field_date_modified(FsearchQueryLexer *lexer,
                          FsearchQueryParseContext *parse_ctx,
                          bool is_empty_field,
                          FsearchQueryFlags flags);

static GList *
parse_field_size(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static GList *
parse_field_extension(FsearchQueryLexer *lexer,
                      FsearchQueryParseContext *parse_ctx,
                      bool is_empty_field,
                      FsearchQueryFlags flags);

static GList *
parse_field_regex(FsearchQueryLexer *lexer,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags);

static GList *
parse_field_parent(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static GList *
parse_field_path(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static GList *
parse_field_case(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static GList *
parse_field_nocase(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static GList *
parse_field_noregex(FsearchQueryLexer *lexer,
                    FsearchQueryParseContext *parse_ctx,
                    bool is_empty_field,
                    FsearchQueryFlags flags);

static GList *
parse_field_nopath(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static GList *
parse_field_folder(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static GList *
parse_field_file(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static gboolean
free_tree_node(GNode *node, gpointer data);

static void
free_tree(GNode *root);

static GList *
add_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token);

static void
node_init_needle(FsearchQueryNode *node, const char *needle) {
    assert(node != NULL);
    assert(needle != NULL);
    // node->needle must not be set already
    assert(node->needle == NULL);
    assert(node->needle_builder == NULL);

    node->needle = g_strdup(needle);
    node->needle_len = strlen(needle);

    // set up case folded needle in UTF16 format
    node->needle_builder = calloc(1, sizeof(FsearchUtfBuilder));
    fsearch_utf_builder_init(node->needle_builder, 8 * node->needle_len);
    const bool utf_ready = fsearch_utf_builder_normalize_and_fold_case(node->needle_builder, needle);
    assert(utf_ready == true);
}

typedef GList *(FsearchTokenFieldParser)(FsearchQueryLexer *, FsearchQueryParseContext *parse_ctx, bool, FsearchQueryFlags);

typedef struct FsearchTokenField {
    const char *name;
    FsearchTokenFieldParser *parser;
} FsearchTokenField;

FsearchTokenField supported_fields[] = {
    {"case", parse_field_case},
    {"dm", parse_field_date_modified},
    {"datemodified", parse_field_date_modified},
    {"exact", parse_field_exact},
    {"ext", parse_field_extension},
    {"file", parse_field_file},
    {"files", parse_field_file},
    {"folder", parse_field_folder},
    {"folders", parse_field_folder},
    {"nocase", parse_field_nocase},
    {"nopath", parse_field_nopath},
    {"noregex", parse_field_noregex},
    {"parent", parse_field_parent},
    {"path", parse_field_path},
    {"regex", parse_field_regex},
    {"size", parse_field_size},
};
static bool
string_starts_with_range(char *str, char **end_ptr) {
    if (g_str_has_prefix(str, "..")) {
        *end_ptr = str + 2;
        return true;
    }
    else if (g_str_has_prefix(str, "-")) {
        *end_ptr = str + 1;
        return true;
    }
    return false;
}

static FsearchQueryNode *
parse_size_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    int64_t size_start = 0;
    int64_t size_end = 0;
    if (fsearch_size_parse(string->str, &size_start, &end_ptr)) {
        if (string_starts_with_range(end_ptr, &end_ptr)) {
            if (end_ptr && *end_ptr == '\0') {
                // interpret size:SIZE.. or size:SIZE- with a missing upper bound as size:>=SIZE
                comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ;
            }
            else if (fsearch_size_parse(end_ptr, &size_end, &end_ptr)) {
                comp_type = FSEARCH_QUERY_NODE_COMPARISON_RANGE;
            }
        }
        return fsearch_query_node_new_size(flags, size_start, size_end, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return NULL;
}

static FsearchQueryNode *
parse_size(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    int64_t size = 0;
    if (fsearch_size_parse(string->str, &size, &end_ptr)) {
        return fsearch_query_node_new_size(flags, size, size, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return NULL;
}

static GList *
parse_field_size(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(lexer, &token_value);
    FsearchQueryNodeComparison comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
    FsearchQueryNode *result = NULL;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_EQUAL:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_SMALLER;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER_EQ:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_SMALLER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER_EQ:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_WORD:
        result = parse_size_with_optional_range(token_value, flags, comp_type);
        goto out;
    default:
        g_debug("[size:] invalid or missing argument");
        goto out;
    }

    GString *next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_lexer_get_next_token(lexer, &next_token_value);
    if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
        result = parse_size(next_token_value, flags, comp_type);
    }
    if (next_token_value) {
        g_string_free(g_steal_pointer(&next_token_value), TRUE);
    }

out:
    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
    return g_list_append(NULL, result);
}

static FsearchQueryNode *
parse_date_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    time_t time_start = 0;
    time_t time_end = 0;
    if (fsearch_time_parse_range(string->str, &time_start, &time_end, &end_ptr)) {
        if (string_starts_with_range(end_ptr, &end_ptr)) {
            if (end_ptr && *end_ptr == '\0') {
                // interpret size:SIZE.. or size:SIZE- with a missing upper bound as size:>=SIZE
                comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ;
            }
            else if (fsearch_time_parse_range(end_ptr, NULL, &time_end, &end_ptr)) {
                comp_type = FSEARCH_QUERY_NODE_COMPARISON_RANGE;
            }
        }
        else {
            comp_type = FSEARCH_QUERY_NODE_COMPARISON_RANGE;
        }
        return fsearch_query_node_new_date_modified(flags, time_start, time_end, comp_type);
    }
    g_debug("[date-modified:] invalid argument: %s", string->str);
    return NULL;
}

static FsearchQueryNode *
parse_date(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    time_t date = 0;
    time_t date_end = 0;
    if (fsearch_time_parse_range(string->str, &date, &date_end, &end_ptr)) {
        time_t dm_start = date;
        time_t dm_end = date_end;
        switch (comp_type) {
        case FSEARCH_QUERY_NODE_COMPARISON_EQUAL:
            // Equal actually refers to a time range. E.g. dm:today is the time range from 0:00:00 to 23:59:59
            comp_type = FSEARCH_QUERY_NODE_COMPARISON_RANGE;
            dm_start = date;
            dm_end = date_end;
            break;
        case FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ:
        case FSEARCH_QUERY_NODE_COMPARISON_SMALLER:
            dm_start = date;
            break;
        case FSEARCH_QUERY_NODE_COMPARISON_GREATER:
        case FSEARCH_QUERY_NODE_COMPARISON_SMALLER_EQ:
            dm_start = date_end;
            break;
        default:
            break;
        }

        return fsearch_query_node_new_date_modified(flags, dm_start, dm_end, comp_type);
    }
    g_debug("[date:] invalid argument: %s", string->str);
    return NULL;
}

static GList *
parse_field_date_modified(FsearchQueryLexer *lexer,
                          FsearchQueryParseContext *parse_ctx,
                          bool is_empty_field,
                          FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(lexer, &token_value);
    FsearchQueryNodeComparison comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
    FsearchQueryNode *result = NULL;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_EQUAL:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_SMALLER;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER_EQ:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_SMALLER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER_EQ:
        comp_type = FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_WORD:
        result = parse_date_with_optional_range(token_value, flags, comp_type);
        goto out;
    default:
        g_debug("[size:] invalid or missing argument");
        goto out;
    }

    GString *next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_lexer_get_next_token(lexer, &next_token_value);
    if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
        result = parse_date(next_token_value, flags, comp_type);
    }
    if (next_token_value) {
        g_string_free(g_steal_pointer(&next_token_value), TRUE);
    }

out:
    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
    return g_list_append(NULL, result);
}

static GList *
parse_field_extension(FsearchQueryLexer *lexer,
                      FsearchQueryParseContext *parse_ctx,
                      bool is_empty_field,
                      FsearchQueryFlags flags) {
    if (!is_empty_field && fsearch_query_lexer_peek_next_token(lexer, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return NULL;
    }
    FsearchQueryNode *result = NULL;
    GString *token_value = NULL;
    if (!is_empty_field) {
        fsearch_query_lexer_get_next_token(lexer, &token_value);
    }
    result = fsearch_query_node_new_extension(token_value ? token_value->str : NULL, flags);
    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }

    return g_list_append(NULL, result);
}

static GList *
parse_field_parent(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field || fsearch_query_lexer_peek_next_token(lexer, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return NULL;
    }
    GString *token_value = NULL;
    fsearch_query_lexer_get_next_token(lexer, &token_value);

    FsearchQueryNode *result = fsearch_query_node_new_parent(token_value->str, flags);

    g_string_free(g_steal_pointer(&token_value), TRUE);

    return g_list_append(NULL, result);
}

static GList *
parse_modifier(FsearchQueryLexer *lexer, FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return g_list_append(NULL, fsearch_query_node_new_match_everything(flags));
    }
    GList *res = NULL;
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(lexer, &token_value);
    if (token == FSEARCH_QUERY_TOKEN_WORD) {
        res = g_list_append(NULL, fsearch_query_node_new(token_value->str, flags));
    }
    else if (token == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
        res = handle_open_bracket_token(parse_ctx);
        res = g_list_concat(res, fsearch_query_parser_parse_expression(lexer, parse_ctx, true, flags));
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD) {
        res = parse_field(lexer, parse_ctx, token_value, false, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD_EMPTY) {
        res = parse_field(lexer, parse_ctx, token_value, true, flags);
    }
    // else {
    //      //add_query_to_parse_context(parse_ctx, fsearch_query_node_new_match_everything(flags),
    //      FSEARCH_QUERY_TOKEN_FIELD_EMPTY);
    // }

    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }

    return res;
}

static GList *
parse_field_exact(FsearchQueryLexer *lexer,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_EXACT_MATCH);
}

static GList *
parse_field_path(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_SEARCH_IN_PATH);
}

static GList *
parse_field_nopath(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_SEARCH_IN_PATH);
}

static GList *
parse_field_case(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_MATCH_CASE);
}

static GList *
parse_field_nocase(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_MATCH_CASE);
}

static GList *
parse_field_regex(FsearchQueryLexer *lexer,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_REGEX);
}

static GList *
parse_field_noregex(FsearchQueryLexer *lexer,
                    FsearchQueryParseContext *parse_ctx,
                    bool is_empty_field,
                    FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_REGEX);
}

static GList *
parse_field_folder(FsearchQueryLexer *lexer,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_FOLDERS_ONLY);
}

static GList *
parse_field_file(FsearchQueryLexer *lexer,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    return parse_modifier(lexer, parse_ctx, is_empty_field, flags | QUERY_FLAG_FILES_ONLY);
}

static GList *
parse_filter_macros(FsearchQueryParseContext *parse_ctx, GString *name, FsearchQueryFlags flags) {
    GList *res = NULL;
    for (uint32_t i = 0; i < parse_ctx->macro_filters->len; ++i) {
        FsearchFilter *filter = g_ptr_array_index(parse_ctx->macro_filters, i);
        if (strcmp(name->str, filter->macro) != 0) {
            continue;
        }
        if (g_queue_find(parse_ctx->macro_stack, filter)) {
            g_debug("[expand_filter_macros] nested macro detected. Stop parsing of macro.");
            break;
        }
        if (fs_str_is_empty(filter->query)) {
            // We don't need to process an empty macro query
            break;
        }

        if (filter->flags & QUERY_FLAG_SEARCH_IN_PATH) {
            flags |= QUERY_FLAG_SEARCH_IN_PATH;
        }
        if (filter->flags & QUERY_FLAG_MATCH_CASE) {
            flags |= QUERY_FLAG_MATCH_CASE;
        }
        if (filter->flags & QUERY_FLAG_REGEX) {
            flags |= QUERY_FLAG_REGEX;
        }
        FsearchQueryLexer *macro_lexer = fsearch_query_lexer_new(filter->query);

        g_queue_push_tail(parse_ctx->macro_stack, filter);
        GQueue *main_operator_stack = parse_ctx->operator_stack;
        parse_ctx->operator_stack = g_queue_new();
        res = fsearch_query_parser_parse_expression(macro_lexer, parse_ctx, false, flags);
        if (!g_queue_is_empty(parse_ctx->operator_stack)) {
            g_warning("[parse_macro] operator stack not empty after parsing!\n");
        }
        g_clear_pointer(&parse_ctx->operator_stack, g_queue_free);
        parse_ctx->operator_stack = main_operator_stack;
        g_queue_pop_tail(parse_ctx->macro_stack);

        g_clear_pointer(&macro_lexer, fsearch_query_lexer_free);

        break;
    }
    return res;
}

static GList *
parse_field(FsearchQueryLexer *lexer,
            FsearchQueryParseContext *parse_ctx,
            GString *field_name,
            bool is_empty_field,
            FsearchQueryFlags flags) {
    // g_debug("[field] detected: [%s:]", field_name->str);
    GList *res = parse_filter_macros(parse_ctx, field_name, flags);
    if (!res) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(supported_fields); ++i) {
            if (!strcmp(supported_fields[i].name, field_name->str)) {
                return supported_fields[i].parser(lexer, parse_ctx, is_empty_field, flags);
            }
        }
    }
    return res;
}

static gboolean
free_tree_node(GNode *node, gpointer data) {
    FsearchQueryNode *n = node->data;
    g_clear_pointer(&n, fsearch_query_node_free);
    return FALSE;
}

static void
free_tree(GNode *root) {
    g_node_traverse(root, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_tree_node, NULL);
    g_clear_pointer(&root, g_node_destroy);
}

static FsearchQueryToken
top_token(GQueue *stack) {
    if (g_queue_is_empty(stack)) {
        return FSEARCH_QUERY_TOKEN_NONE;
    }
    return (FsearchQueryToken)GPOINTER_TO_UINT(g_queue_peek_tail(stack));
}

static FsearchQueryToken
pop_token(GQueue *stack) {
    if (g_queue_is_empty(stack)) {
        return FSEARCH_QUERY_TOKEN_NONE;
    }
    return (FsearchQueryToken)(GPOINTER_TO_UINT(g_queue_pop_tail(stack)));
}

static void
push_token(GQueue *stack, FsearchQueryToken token) {
    g_queue_push_tail(stack, GUINT_TO_POINTER((guint)token));
}

static uint32_t
get_operator_precedence(FsearchQueryToken operator) {
    switch (operator) {
    case FSEARCH_QUERY_TOKEN_NOT:
        return 3;
    case FSEARCH_QUERY_TOKEN_AND:
        return 2;
    case FSEARCH_QUERY_TOKEN_OR:
        return 1;
    default:
        return 0;
    }
}

static FsearchQueryNode *
get_operator_node(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token) {
    FsearchQueryNodeOperator op = 0;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_AND:
        op = FSEARCH_QUERY_NODE_OPERATOR_AND;
        break;
    case FSEARCH_QUERY_TOKEN_OR:
        op = FSEARCH_QUERY_NODE_OPERATOR_OR;
        break;
    case FSEARCH_QUERY_TOKEN_NOT:
        op = FSEARCH_QUERY_NODE_OPERATOR_NOT;
        break;
    default:
        return NULL;
    }
    return fsearch_query_node_new_operator(op);
}

static GList *
add_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token) {
    switch (parse_ctx->last_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
    case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
        break;
    default:
        return NULL;
    }

    switch (next_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
    case FSEARCH_QUERY_TOKEN_NOT:
    case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
        return handle_operator_token(parse_ctx, FSEARCH_QUERY_TOKEN_AND);
    default:
        return NULL;
    }
}

static GList *
handle_operator_token(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token) {
    parse_ctx->last_token = token;
    GList *res = NULL;
    while (!g_queue_is_empty(parse_ctx->operator_stack)
           && get_operator_precedence(token) <= get_operator_precedence(top_token(parse_ctx->operator_stack))) {
        FsearchQueryNode *op_node = get_operator_node(parse_ctx, pop_token(parse_ctx->operator_stack));
        if (op_node) {
            res = g_list_append(res, op_node);
        }
    }
    push_token(parse_ctx->operator_stack, token);
    return res;
}

static bool
uneven_number_of_consecutive_not_tokens(FsearchQueryLexer *lexer, FsearchQueryToken current_token) {
    if (current_token != FSEARCH_QUERY_TOKEN_NOT) {
        g_assert_not_reached();
    }
    bool uneven_number_of_not_tokens = true;
    while (fsearch_query_lexer_peek_next_token(lexer, NULL) == FSEARCH_QUERY_TOKEN_NOT) {
        fsearch_query_lexer_get_next_token(lexer, NULL);
        uneven_number_of_not_tokens = !uneven_number_of_not_tokens;
    }
    return uneven_number_of_not_tokens;
}

static GList *
handle_open_bracket_token(FsearchQueryParseContext *parse_ctx) {
    GList *res = add_implicit_and_if_necessary(parse_ctx, FSEARCH_QUERY_TOKEN_BRACKET_OPEN);
    parse_ctx->last_token = FSEARCH_QUERY_TOKEN_BRACKET_OPEN;
    push_token(parse_ctx->operator_stack, FSEARCH_QUERY_TOKEN_BRACKET_OPEN);
    return res;
}

GList *
fsearch_query_parser_parse_expression(FsearchQueryLexer *lexer,
                                      FsearchQueryParseContext *parse_ctx,
                                      bool in_open_bracket,
                                      FsearchQueryFlags flags) {
    GList *res = NULL;

    uint32_t num_open_brackets = in_open_bracket ? 1 : 0;
    uint32_t num_close_brackets = 0;

    while (true) {
        GString *token_value = NULL;
        FsearchQueryToken token = fsearch_query_lexer_get_next_token(lexer, &token_value);

        GList *to_append = NULL;
        switch (token) {
        case FSEARCH_QUERY_TOKEN_EOS:
            goto out;
        case FSEARCH_QUERY_TOKEN_NOT:
            if (uneven_number_of_consecutive_not_tokens(lexer, token)) {
                // We want to support consecutive NOT operators (i.e. `NOT NOT a`)
                // so even numbers of NOT operators get ignored and for uneven numbers
                // we simply add a single one
                // to_append = add_implicit_and_if_necessary(parse_ctx, token);
                to_append = handle_operator_token(parse_ctx, token);
            }
            break;
        case FSEARCH_QUERY_TOKEN_AND:
        case FSEARCH_QUERY_TOKEN_OR:
            to_append = handle_operator_token(parse_ctx, token);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
            num_open_brackets++;
            to_append = handle_open_bracket_token(parse_ctx);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
            if (num_open_brackets > num_close_brackets) {
                // only add closing bracket if there's at least one matching open bracket
                while (true) {
                    FsearchQueryToken t = top_token(parse_ctx->operator_stack);
                    if (t == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
                        break;
                    }
                    if (t == FSEARCH_QUERY_TOKEN_NONE) {
                        g_warning("[infix-postfix] Matching open bracket not found!\n");
                        g_assert_not_reached();
                    }
                    FsearchQueryNode *op_node = get_operator_node(parse_ctx, pop_token(parse_ctx->operator_stack));
                    if (op_node) {
                        to_append = g_list_append(to_append, op_node);
                    }
                }
                if (top_token(parse_ctx->operator_stack) == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
                    pop_token(parse_ctx->operator_stack);
                }
                parse_ctx->last_token = FSEARCH_QUERY_TOKEN_BRACKET_CLOSE;
                num_close_brackets++;
                if (in_open_bracket && num_close_brackets == num_open_brackets) {
                    if (to_append) {
                        return g_list_concat(res, to_append);
                    }
                    else {
                        return res;
                    }
                }
            }
            else {
                g_list_free_full(g_steal_pointer(&res), fsearch_query_node_free);
                g_debug("[infix-postfix] closing bracket found without a corresponding open bracket, abort parsing!\n");
                return g_list_append(res, fsearch_query_node_new_match_nothing());
            }
            break;
        case FSEARCH_QUERY_TOKEN_WORD:
            to_append = g_list_append(to_append, fsearch_query_node_new(token_value->str, flags));
            g_print("Token word: %s\n", to_append ? token_value->str : "empty");
            break;
        case FSEARCH_QUERY_TOKEN_FIELD:
            to_append = parse_field(lexer, parse_ctx, token_value, false, flags);
            break;
        case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
            to_append = parse_field(lexer, parse_ctx, token_value, true, flags);
            break;
        default:
            g_debug("[infix-postfix] ignoring unexpected token: %d", token);
            break;
        }

        if (to_append) {
            res = g_list_concat(res, add_implicit_and_if_necessary(parse_ctx, token));
            parse_ctx->last_token = token;
            res = g_list_concat(res, to_append);
        }

        if (token_value) {
            g_string_free(g_steal_pointer(&token_value), TRUE);
        }
    }

out:
    while (!g_queue_is_empty(parse_ctx->operator_stack)) {
        FsearchQueryNode *op_node = get_operator_node(parse_ctx, pop_token(parse_ctx->operator_stack));
        if (op_node) {
            res = g_list_append(res, op_node);
        }
    }
    return res;
}

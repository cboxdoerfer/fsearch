#include "fsearch_query_parser.h"
#include "fsearch_query_lexer.h"
#include "fsearch_query_node.h"
#include "fsearch_size_utils.h"
#include "fsearch_string_utils.h"
#include "fsearch_time_utils.h"

#include <stdbool.h>

static GList *
parse_open_bracket(FsearchQueryParseContext *parse_ctx);

static GList *
parse_close_bracket(FsearchQueryParseContext *parse_ctx);

static GList *
parse_operator(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token);

static GList *
parse_word(GString *field_name, FsearchQueryFlags flags);

static GList *
parse_field(FsearchQueryParseContext *parse_ctx, GString *field_name, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_modifier(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_exact(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_date_modified(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_size(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_extension(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_regex(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_parent(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_path(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_case(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_nocase(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_noregex(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_nopath(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_folder(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
parse_field_file(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags);

static GList *
get_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token);

typedef GList *(FsearchTokenFieldParser)(FsearchQueryParseContext *parse_ctx, bool, FsearchQueryFlags);

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

static FsearchQueryNode *
parse_size_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    int64_t size_start = 0;
    int64_t size_end = 0;
    if (fsearch_size_parse(string->str, &size_start, &end_ptr)) {
        if (fs_str_starts_with_range(end_ptr, &end_ptr)) {
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
parse_field_size(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    g_autoptr(GString) token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);
    FsearchQueryNodeComparison comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
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
        return g_list_append(NULL, parse_size_with_optional_range(token_value, flags, comp_type));
    default:
        g_debug("[size:] invalid or missing argument");
        return NULL;
    }

    g_autoptr(GString) next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &next_token_value);
    if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
        return g_list_append(NULL, parse_size(next_token_value, flags, comp_type));
    }

    return NULL;
}

static FsearchQueryNode *
parse_date_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
    char *end_ptr = NULL;
    time_t time_start = 0;
    time_t time_end = 0;
    if (fsearch_time_parse_range(string->str, &time_start, &time_end, &end_ptr)) {
        if (fs_str_starts_with_range(end_ptr, &end_ptr)) {
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
parse_date_modified(GString *string, FsearchQueryFlags flags, FsearchQueryNodeComparison comp_type) {
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
parse_field_date_modified(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    g_autoptr(GString) token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);
    FsearchQueryNodeComparison comp_type = FSEARCH_QUERY_NODE_COMPARISON_EQUAL;
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
        return g_list_append(NULL, parse_date_with_optional_range(token_value, flags, comp_type));
    default:
        g_debug("[size:] invalid or missing argument");
        return NULL;
    }

    g_autoptr(GString) next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &next_token_value);
    if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
        return g_list_append(NULL, parse_date_modified(next_token_value, flags, comp_type));
    }

    return NULL;
}

static GList *
parse_field_extension(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (!is_empty_field && fsearch_query_lexer_peek_next_token(parse_ctx->lexer, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return NULL;
    }
    g_autoptr(GString) token_value = NULL;
    if (!is_empty_field) {
        fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);
    }
    return g_list_append(NULL, fsearch_query_node_new_extension(token_value ? token_value->str : NULL, flags));
}

static GList *
parse_field_parent(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field || fsearch_query_lexer_peek_next_token(parse_ctx->lexer, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return NULL;
    }
    g_autoptr(GString) token_value = NULL;
    fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);

    return g_list_append(NULL, fsearch_query_node_new_parent(token_value->str, flags));
}

static GList *
parse_modifier(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return g_list_append(NULL, fsearch_query_node_new_match_everything(flags));
    }
    g_autoptr(GString) token_value = NULL;
    FsearchQueryToken token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);
    if (token == FSEARCH_QUERY_TOKEN_WORD) {
        return parse_word(token_value, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
        GList *res = parse_open_bracket(parse_ctx);
        return g_list_concat(res, fsearch_query_parser_parse_expression(parse_ctx, true, flags));
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD) {
        return parse_field(parse_ctx, token_value, false, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD_EMPTY) {
        return parse_field(parse_ctx, token_value, true, flags);
    }
    return NULL;
}

static GList *
parse_field_exact(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_EXACT_MATCH);
}

static GList *
parse_field_path(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_SEARCH_IN_PATH);
}

static GList *
parse_field_nopath(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags & ~QUERY_FLAG_SEARCH_IN_PATH);
}

static GList *
parse_field_case(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_MATCH_CASE);
}

static GList *
parse_field_nocase(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags & ~QUERY_FLAG_MATCH_CASE);
}

static GList *
parse_field_regex(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_REGEX);
}

static GList *
parse_field_noregex(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    if (is_empty_field) {
        return NULL;
    }
    return parse_modifier(parse_ctx, is_empty_field, flags & ~QUERY_FLAG_REGEX);
}

static GList *
parse_field_folder(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_FOLDERS_ONLY);
}

static GList *
parse_field_file(FsearchQueryParseContext *parse_ctx, bool is_empty_field, FsearchQueryFlags flags) {
    return parse_modifier(parse_ctx, is_empty_field, flags | QUERY_FLAG_FILES_ONLY);
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

        g_queue_push_tail(parse_ctx->macro_stack, filter);
        GQueue *main_operator_stack = parse_ctx->operator_stack;
        FsearchQueryLexer *main_lexer = parse_ctx->lexer;
        parse_ctx->lexer = fsearch_query_lexer_new(filter->query);
        parse_ctx->operator_stack = g_queue_new();
        res = fsearch_query_parser_parse_expression(parse_ctx, false, flags);
        if (!g_queue_is_empty(parse_ctx->operator_stack)) {
            g_warning("[parse_macro] operator stack not empty after parsing!\n");
        }
        g_clear_pointer(&parse_ctx->lexer, fsearch_query_lexer_free);
        g_clear_pointer(&parse_ctx->operator_stack, g_queue_free);
        parse_ctx->operator_stack = main_operator_stack;
        parse_ctx->lexer = main_lexer;
        g_queue_pop_tail(parse_ctx->macro_stack);

        break;
    }
    return res;
}

static GList *
parse_field(FsearchQueryParseContext *parse_ctx, GString *field_name, bool is_empty_field, FsearchQueryFlags flags) {
    // g_debug("[field] detected: [%s:]", field_name->str);
    GList *res = parse_filter_macros(parse_ctx, field_name, flags);
    if (!res) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(supported_fields); ++i) {
            if (!strcmp(supported_fields[i].name, field_name->str)) {
                return supported_fields[i].parser(parse_ctx, is_empty_field, flags);
            }
        }
    }
    return res;
}

static GList *
parse_word(GString *field_name, FsearchQueryFlags flags) {
    if (!field_name) {
        return NULL;
    }
    return g_list_append(NULL, fsearch_query_node_new(field_name->str, flags));
}

static FsearchQueryToken
top_query_token(GQueue *stack) {
    if (g_queue_is_empty(stack)) {
        return FSEARCH_QUERY_TOKEN_NONE;
    }
    return (FsearchQueryToken)GPOINTER_TO_UINT(g_queue_peek_tail(stack));
}

static FsearchQueryToken
pop_query_token(GQueue *stack) {
    if (g_queue_is_empty(stack)) {
        return FSEARCH_QUERY_TOKEN_NONE;
    }
    return (FsearchQueryToken)(GPOINTER_TO_UINT(g_queue_pop_tail(stack)));
}

static void
push_query_token(GQueue *stack, FsearchQueryToken token) {
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
get_operator_node_for_query_token(FsearchQueryToken token) {
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
get_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token) {
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
        return parse_operator(parse_ctx, FSEARCH_QUERY_TOKEN_AND);
    default:
        return NULL;
    }
}

static bool
is_operator_token(FsearchQueryToken token) {
    if (token == FSEARCH_QUERY_TOKEN_AND || token == FSEARCH_QUERY_TOKEN_OR) {
        return true;
    }
    return false;
}

static bool
is_operator_token_followed_by_operand(FsearchQueryLexer *lexer, FsearchQueryToken token) {
    FsearchQueryToken next_token = fsearch_query_lexer_peek_next_token(lexer, NULL);
    if (is_operator_token(token) && next_token == FSEARCH_QUERY_TOKEN_NOT) {
        return true;
    }
    switch (next_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
    case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
        return true;
    default:
        return false;
    }
}

static GList *
parse_operator(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token) {
    parse_ctx->last_token = token;
    GList *res = NULL;
    while (!g_queue_is_empty(parse_ctx->operator_stack)
           && get_operator_precedence(token) <= get_operator_precedence(top_query_token(parse_ctx->operator_stack))) {
        FsearchQueryNode *op_node = get_operator_node_for_query_token(pop_query_token(parse_ctx->operator_stack));
        if (op_node) {
            res = g_list_append(res, op_node);
        }
    }
    push_query_token(parse_ctx->operator_stack, token);
    return res;
}

static bool
consume_consecutive_not_token(FsearchQueryLexer *lexer) {
    bool uneven_number_of_not_tokens = true;
    while (fsearch_query_lexer_peek_next_token(lexer, NULL) == FSEARCH_QUERY_TOKEN_NOT) {
        fsearch_query_lexer_get_next_token(lexer, NULL);
        uneven_number_of_not_tokens = !uneven_number_of_not_tokens;
    }
    return uneven_number_of_not_tokens;
}

static void
discard_operator_tokens(FsearchQueryLexer *lexer) {
    while (is_operator_token(fsearch_query_lexer_peek_next_token(lexer, NULL))) {
        fsearch_query_lexer_get_next_token(lexer, NULL);
    }
}

static GList *
parse_close_bracket(FsearchQueryParseContext *parse_ctx) {
    GList *res = NULL;
    while (true) {
        FsearchQueryToken t = top_query_token(parse_ctx->operator_stack);
        if (t == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
            break;
        }
        if (t == FSEARCH_QUERY_TOKEN_NONE) {
            g_warning("[infix-postfix] Matching open bracket not found!\n");
            g_assert_not_reached();
        }
        FsearchQueryNode *op_node = get_operator_node_for_query_token(pop_query_token(parse_ctx->operator_stack));
        if (op_node) {
            res = g_list_append(res, op_node);
        }
    }
    if (top_query_token(parse_ctx->operator_stack) == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
        pop_query_token(parse_ctx->operator_stack);
    }
    parse_ctx->last_token = FSEARCH_QUERY_TOKEN_BRACKET_CLOSE;
    return res;
}

static GList *
parse_open_bracket(FsearchQueryParseContext *parse_ctx) {
    GList *res = get_implicit_and_if_necessary(parse_ctx, FSEARCH_QUERY_TOKEN_BRACKET_OPEN);
    parse_ctx->last_token = FSEARCH_QUERY_TOKEN_BRACKET_OPEN;
    push_query_token(parse_ctx->operator_stack, FSEARCH_QUERY_TOKEN_BRACKET_OPEN);
    return res;
}

GList *
fsearch_query_parser_parse_expression(FsearchQueryParseContext *parse_ctx, bool in_open_bracket, FsearchQueryFlags flags) {
    GList *res = NULL;

    uint32_t num_open_brackets = in_open_bracket ? 1 : 0;
    uint32_t num_close_brackets = 0;

    while (true) {
        g_autoptr(GString) token_value = NULL;
        FsearchQueryToken token = fsearch_query_lexer_get_next_token(parse_ctx->lexer, &token_value);

        GList *to_append = NULL;
        switch (token) {
        case FSEARCH_QUERY_TOKEN_EOS:
            goto out;
        case FSEARCH_QUERY_TOKEN_NOT:
            if (consume_consecutive_not_token(parse_ctx->lexer)) {
                // We want to support consecutive NOT operators (i.e. `NOT NOT a`)
                // so even numbers of NOT operators get ignored and for uneven numbers
                // we simply add a single one
                // to_append = add_implicit_and_if_necessary(parse_ctx, token);
                if (is_operator_token_followed_by_operand(parse_ctx->lexer, token)) {
                    to_append = parse_operator(parse_ctx, token);
                }
            }
            // discard_operator_tokens(parse_ctx->lexer);
            break;
        case FSEARCH_QUERY_TOKEN_AND:
        case FSEARCH_QUERY_TOKEN_OR:
            if (is_operator_token_followed_by_operand(parse_ctx->lexer, token)) {
                to_append = parse_operator(parse_ctx, token);
            }
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
            num_open_brackets++;
            to_append = parse_open_bracket(parse_ctx);
            discard_operator_tokens(parse_ctx->lexer);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
            // only add closing bracket if there's a matching open bracket
            if (num_open_brackets > num_close_brackets) {
                num_close_brackets++;
                to_append = parse_close_bracket(parse_ctx);

                if (in_open_bracket && num_close_brackets == num_open_brackets) {
                    // We found the matching closing bracket which marks the end of this expression, return.
                    if (to_append) {
                        return g_list_concat(res, to_append);
                    }
                    else {
                        return res;
                    }
                }
            }
            else {
                g_debug("[infix-postfix] closing bracket found without a corresponding open bracket, abort parsing!\n");
                g_list_free_full(g_steal_pointer(&res), (GDestroyNotify)fsearch_query_node_free);
                return g_list_append(NULL, fsearch_query_node_new_match_nothing());
            }
            break;
        case FSEARCH_QUERY_TOKEN_WORD:
            to_append = parse_word(token_value, flags);
            break;
        case FSEARCH_QUERY_TOKEN_FIELD:
            to_append = parse_field(parse_ctx, token_value, false, flags);
            break;
        case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
            to_append = parse_field(parse_ctx, token_value, true, flags);
            break;
        default:
            g_debug("[infix-postfix] ignoring unexpected token: %d", token);
            break;
        }

        if (to_append) {
            res = g_list_concat(res, get_implicit_and_if_necessary(parse_ctx, token));
            parse_ctx->last_token = token;
            res = g_list_concat(res, to_append);
        }
    }

out:
    while (!g_queue_is_empty(parse_ctx->operator_stack)) {
        FsearchQueryNode *op_node = get_operator_node_for_query_token(pop_query_token(parse_ctx->operator_stack));
        if (op_node) {
            res = g_list_append(res, op_node);
        }
    }
    return res;
}

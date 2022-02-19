#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-query-node"

#include "fsearch_query_node.h"
#include "fsearch_query_match_context.h"
#include "fsearch_query_parser.h"
#include "fsearch_string_utils.h"
#include "fsearch_utf.h"
#include <assert.h>
#include <fnmatch.h>
#include <glib.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>

static FsearchQueryNode *
parse_field(FsearchQueryParser *parser, GString *field_name, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_modifier(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_exact(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_size(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_regex(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_path(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_case(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_nocase(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_noregex(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_nopath(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_folder(FsearchQueryParser *parser, FsearchQueryFlags flags);

static FsearchQueryNode *
parse_field_file(FsearchQueryParser *parser, FsearchQueryFlags flags);

static gboolean
free_tree_node(GNode *node, gpointer data);

static void
free_tree(GNode *root);

typedef FsearchQueryNode *(FsearchTokenFieldParser)(FsearchQueryParser *, FsearchQueryFlags);

typedef struct FsearchTokenField {
    const char *name;
    FsearchTokenFieldParser *parser;
} FsearchTokenField;

FsearchTokenField supported_fields[] = {
    {"case", parse_field_case},
    {"exact", parse_field_exact},
    {"file", parse_field_file},
    {"files", parse_field_file},
    {"folder", parse_field_folder},
    {"folders", parse_field_folder},
    {"nocase", parse_field_nocase},
    {"nopath", parse_field_nopath},
    {"noregex", parse_field_noregex},
    {"path", parse_field_path},
    {"regex", parse_field_regex},
    {"size", parse_field_size},
};

static u_int32_t
fsearch_search_func_true(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    return 1;
}

static uint32_t
fsearch_search_func_size(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    FsearchDatabaseEntry *entry = fsearch_query_match_context_get_entry(matcher);
    if (entry) {
        off_t size = db_entry_get_size(entry);
        switch (node->size_comparison_type) {
        case FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL:
            return size == node->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_GREATER:
            return size > node->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER:
            return size < node->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_GREATER_EQ:
            return size >= node->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER_EQ:
            return size <= node->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_RANGE:
            return node->size <= size && size <= node->size_upper_limit;
        }
    }
    return 0;
}

static uint32_t
fsearch_search_func_regex(const char *haystack, FsearchQueryNode *node) {
    size_t haystack_len = strlen(haystack);
    return pcre_exec(node->regex, node->regex_study, haystack, haystack_len, 0, 0, node->ovector, OVECCOUNT) >= 0 ? 1 : 0;
}

static uint32_t
fsearch_search_func_regex_path(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_path_str(matcher);
    return fsearch_search_func_regex(haystack, node);
}

static uint32_t
fsearch_search_func_regex_name(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_name_str(matcher);
    return fsearch_search_func_regex(haystack, node);
}

static uint32_t
fsearch_search_func_wildcard(const char *haystack, FsearchQueryNode *node) {
    return !fnmatch(node->search_term, haystack, node->wildcard_flags) ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard_path(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_path_str(matcher);
    return fsearch_search_func_wildcard(haystack, node);
}

static uint32_t
fsearch_search_func_wildcard_name(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_name_str(matcher);
    return fsearch_search_func_wildcard(haystack, node);
}

// static uint32_t
// fsearch_search_func_normal_icase_u8_fast(FsearchToken *token, FsearchQueryMatcher *matcher) {
//     FsearchUtfConversionBuffer *buffer = token->get_haystack(matcher);
//     if (G_LIKELY(buffer->string_utf8_is_folded)) {
//         return strstr(buffer->string_utf8_folded, token->needle_buffer->string_utf8_folded) ? 1 : 0;
//     }
//     else {
//         // failed to fold case, fall back to fast but not accurate ascii search
//         // g_warning("[utf8_search] failed to lower case: %s", haystack);
//         // return strcasestr(haystack, needle) ? 1 : 0;
//     }
// }

static uint32_t
fsearch_search_func_normal_icase_u8(FsearchUtfConversionBuffer *haystack_buffer,
                                    FsearchUtfConversionBuffer *needle_buffer,
                                    bool exact_match) {
    if (G_LIKELY(haystack_buffer->string_is_folded_and_normalized)) {
        if (exact_match) {
            return !u_strCompare(haystack_buffer->string_normalized_folded,
                                 haystack_buffer->string_normalized_folded_len,
                                 needle_buffer->string_normalized_folded,
                                 needle_buffer->string_normalized_folded_len,
                                 false)
                     ? 1
                     : 0;
        }
        return u_strFindFirst(haystack_buffer->string_normalized_folded,
                              haystack_buffer->string_normalized_folded_len,
                              needle_buffer->string_normalized_folded,
                              needle_buffer->string_normalized_folded_len)
                 ? 1
                 : 0;
    }
    else {
        // failed to fold case, fall back to fast but not accurate ascii search
        g_warning("[utf8_search] failed to lower case: %s", haystack_buffer->string);
        if (exact_match) {
            return !strcasecmp(haystack_buffer->string, needle_buffer->string) ? 1 : 0;
        }
        return strcasestr(haystack_buffer->string, needle_buffer->string) ? 1 : 0;
    }
}

static uint32_t
fsearch_search_func_normal_icase_u8_path(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    FsearchUtfConversionBuffer *haystack_buffer = fsearch_query_match_context_get_utf_path_buffer(matcher);
    FsearchUtfConversionBuffer *needle_buffer = node->needle_buffer;
    return fsearch_search_func_normal_icase_u8(haystack_buffer, needle_buffer, node->flags & QUERY_FLAG_EXACT_MATCH);
}

static uint32_t
fsearch_search_func_normal_icase_u8_name(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    FsearchUtfConversionBuffer *haystack_buffer = fsearch_query_match_context_get_utf_name_buffer(matcher);
    FsearchUtfConversionBuffer *needle_buffer = node->needle_buffer;
    return fsearch_search_func_normal_icase_u8(haystack_buffer, needle_buffer, node->flags & QUERY_FLAG_EXACT_MATCH);
}

static uint32_t
fsearch_search_func_normal_icase_path(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    if (node->flags & QUERY_FLAG_EXACT_MATCH) {
        return !strcasecmp(fsearch_query_match_context_get_path_str(matcher), node->search_term) ? 1 : 0;
    }
    return strcasestr(fsearch_query_match_context_get_path_str(matcher), node->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_icase_name(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    if (node->flags & QUERY_FLAG_EXACT_MATCH) {
        return !strcasecmp(fsearch_query_match_context_get_name_str(matcher), node->search_term) ? 1 : 0;
    }
    return strcasestr(fsearch_query_match_context_get_name_str(matcher), node->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_path(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    if (node->flags & QUERY_FLAG_EXACT_MATCH) {
        return !strcmp(fsearch_query_match_context_get_path_str(matcher), node->search_term) ? 1 : 0;
    }
    return strstr(fsearch_query_match_context_get_path_str(matcher), node->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_name(FsearchQueryNode *node, FsearchQueryMatchContext *matcher) {
    if (node->flags & QUERY_FLAG_EXACT_MATCH) {
        return !strcmp(fsearch_query_match_context_get_name_str(matcher), node->search_term) ? 1 : 0;
    }
    return strstr(fsearch_query_match_context_get_name_str(matcher), node->search_term) ? 1 : 0;
}

static void
fsearch_query_node_free(void *data) {
    FsearchQueryNode *node = data;
    assert(node != NULL);

    fsearch_utf_conversion_buffer_clear(node->needle_buffer);
    g_clear_pointer(&node->needle_buffer, free);
    g_clear_pointer(&node->case_map, ucasemap_close);
    g_clear_pointer(&node->search_term, g_free);
    g_clear_pointer(&node->regex_study, pcre_free_study);
    g_clear_pointer(&node->regex, pcre_free);
    g_clear_pointer(&node, g_free);
}

void
fsearch_query_node_tree_free(GNode *node) {
    if (!node) {
        return;
    }
    g_clear_pointer(&node, free_tree);
}

static FsearchQueryNode *
fsearch_query_node_new_size(FsearchQueryFlags flags,
                            off_t size_start,
                            off_t size_end,
                            FsearchTokenSizeComparisonType comp_type) {
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->size = size_start;
    new->size_upper_limit = size_end;
    new->size_comparison_type = comp_type;
    new->search_func = fsearch_search_func_size;
    new->flags = flags;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_operator(FsearchQueryNodeOperator operator) {
    assert(operator== FSEARCH_TOKEN_OPERATOR_AND || operator== FSEARCH_TOKEN_OPERATOR_OR);
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);
    new->type = FSEARCH_QUERY_NODE_TYPE_OPERATOR;
    new->operator= operator;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_match_everything(FsearchQueryFlags flags) {
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->search_func = fsearch_search_func_true;
    new->flags = flags;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new(const char *search_term, FsearchQueryFlags flags) {
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->search_term = g_strdup(search_term);
    new->search_term_len = strlen(search_term);
    new->has_separator = strchr(search_term, G_DIR_SEPARATOR) ? 1 : 0;

    if ((flags & QUERY_FLAG_AUTO_MATCH_CASE) && fs_str_utf8_has_upper(search_term)) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }
    new->flags = flags;

    new->fold_options = U_FOLD_CASE_DEFAULT;
    const char *current_locale = setlocale(LC_CTYPE, NULL);
    if (current_locale && (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2))) {
        // Use special case mapping for Turkic languages
        new->fold_options = U_FOLD_CASE_EXCLUDE_SPECIAL_I;
    }

    UErrorCode status = U_ZERO_ERROR;
    new->case_map = ucasemap_open(current_locale, new->fold_options, &status);
    assert(U_SUCCESS(status));

    new->normalizer = unorm2_getNFDInstance(&status);
    assert(U_SUCCESS(status));

    // set up case folded needle in UTF16 format
    new->needle_buffer = calloc(1, sizeof(FsearchUtfConversionBuffer));
    fsearch_utf_conversion_buffer_init(new->needle_buffer, 8 * new->search_term_len);
    const bool utf_ready = fsearch_utf_converion_buffer_normalize_and_fold_case(new->needle_buffer,
                                                                                new->case_map,
                                                                                new->normalizer,
                                                                                search_term);
    assert(utf_ready == true);

    const bool search_in_path = flags & QUERY_FLAG_SEARCH_IN_PATH
                             || (flags & QUERY_FLAG_AUTO_SEARCH_IN_PATH && new->has_separator);

    if (flags & QUERY_FLAG_REGEX) {
        const char *error;
        int erroffset;
        new->regex =
            pcre_compile(search_term, flags & QUERY_FLAG_MATCH_CASE ? 0 : PCRE_CASELESS, &error, &erroffset, NULL);
        new->regex_study = pcre_study(new->regex, PCRE_STUDY_JIT_COMPILE, &error);
        new->search_func = search_in_path ? fsearch_search_func_regex_path : fsearch_search_func_regex_name;
    }
    else if (strchr(search_term, '*') || strchr(search_term, '?')) {
        new->search_func = search_in_path ? fsearch_search_func_wildcard_path : fsearch_search_func_wildcard_name;
        new->wildcard_flags = flags &QUERY_FLAG_MATCH_CASE ? FNM_CASEFOLD : 0;
    }
    else {
        if (flags & QUERY_FLAG_MATCH_CASE) {
            new->search_func = search_in_path ? fsearch_search_func_normal_path : fsearch_search_func_normal_name;
        }
        else if (fs_str_case_is_ascii(search_term)) {
            new->search_func = search_in_path ? fsearch_search_func_normal_icase_path
                                              : fsearch_search_func_normal_icase_name;
        }
        else {
            new->search_func = search_in_path ? fsearch_search_func_normal_icase_u8_path
                                              : fsearch_search_func_normal_icase_u8_name;
        }
    }
    return new;
}

FsearchQueryNode *
get_empty_query_node(FsearchQueryFlags flags) {
    return fsearch_query_node_new_match_everything(flags);
}

static bool
string_prefix_to_size(const char *str, off_t *size_out, char **end_ptr) {
    assert(size_out != NULL);
    char *size_suffix = NULL;
    off_t size = strtoll(str, &size_suffix, 10);
    if (size_suffix == str) {
        return false;
    }
    if (size_suffix && *size_suffix != '\0') {
        switch (*size_suffix) {
        case 'k':
        case 'K':
            size *= 1000;
            break;
        case 'm':
        case 'M':
            size *= 1000 * 1000;
            break;
        case 'g':
        case 'G':
            size *= 1000 * 1000 * 1000;
            break;
        case 't':
        case 'T':
            size *= (off_t)1000 * 1000 * 1000 * 1000;
            break;
        default:
            goto out;
        }
        size_suffix++;

        switch (*size_suffix) {
        case 'b':
        case 'B':
            size_suffix++;
            break;
        default:
            goto out;
        }
    }
out:
    if (end_ptr) {
        *end_ptr = size_suffix;
    }
    *size_out = size;
    return true;
}

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
parse_size_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchTokenSizeComparisonType comp_type) {
    char *end_ptr = NULL;
    off_t size_start = 0;
    off_t size_end = 0;
    if (string_prefix_to_size(string->str, &size_start, &end_ptr)) {
        if (string_starts_with_range(end_ptr, &end_ptr) && string_prefix_to_size(end_ptr, &size_end, &end_ptr)) {
            comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_RANGE;
        }
        return fsearch_query_node_new_size(flags, size_start, size_end, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return get_empty_query_node(flags);
}

static FsearchQueryNode *
parse_size(GString *string, FsearchQueryFlags flags, FsearchTokenSizeComparisonType comp_type) {
    char *end_ptr = NULL;
    off_t size = 0;
    if (string_prefix_to_size(string->str, &size, &end_ptr)) {
        return fsearch_query_node_new_size(flags, size, size, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return get_empty_query_node(flags);
}

static FsearchQueryNode *
parse_field_size(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    FsearchTokenSizeComparisonType comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL;
    FsearchQueryNode *result = NULL;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_SMALLER:
        comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER_EQ:
        comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER:
        comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_GREATER;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER_EQ:
        comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_GREATER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_WORD:
        result = parse_size_with_optional_range(token_value, flags, comp_type);
        break;
    default:
        g_debug("[size:] invalid or missing argument");
        goto out;
    }

    if (comp_type != FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL) {
        GString *next_token_value = NULL;
        FsearchQueryToken next_token = fsearch_query_parser_get_next_token(parser, &next_token_value);
        if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
            result = parse_size(next_token_value, flags, comp_type);
        }
        if (next_token_value) {
            g_string_free(g_steal_pointer(&next_token_value), TRUE);
        }
    }
out:
    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
    if (!result) {
        result = get_empty_query_node(flags);
    }
    return result;
}

static FsearchQueryNode *
parse_modifier(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    FsearchQueryNode *result = NULL;
    if (token == FSEARCH_QUERY_TOKEN_WORD) {
        result = fsearch_query_node_new(token_value->str, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD) {
        result = parse_field(parser, token_value, flags);
    }
    else {
        result = get_empty_query_node(flags);
    }

    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }

    return result;
}

static FsearchQueryNode *
parse_field_exact(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_EXACT_MATCH);
}

static FsearchQueryNode *
parse_field_path(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_SEARCH_IN_PATH);
}

static FsearchQueryNode *
parse_field_nopath(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags & ~QUERY_FLAG_SEARCH_IN_PATH);
}

static FsearchQueryNode *
parse_field_case(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_MATCH_CASE);
}

static FsearchQueryNode *
parse_field_nocase(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags & ~QUERY_FLAG_MATCH_CASE);
}

static FsearchQueryNode *
parse_field_regex(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_REGEX);
}

static FsearchQueryNode *
parse_field_noregex(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags & ~QUERY_FLAG_REGEX);
}

static FsearchQueryNode *
parse_field_folder(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_FOLDERS_ONLY);
}

static FsearchQueryNode *
parse_field_file(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    return parse_modifier(parser, flags | QUERY_FLAG_FILES_ONLY);
}

static FsearchQueryNode *
parse_field(FsearchQueryParser *parser, GString *field_name, FsearchQueryFlags flags) {
    // g_debug("[field] detected: [%s:]", field_name->str);
    for (uint32_t i = 0; i < G_N_ELEMENTS(supported_fields); ++i) {
        if (!strcmp(supported_fields[i].name, field_name->str)) {
            return supported_fields[i].parser(parser, flags);
        }
    }
    return get_empty_query_node(flags);
}

static GNode *
get_empty_node(FsearchQueryFlags flags) {
    return g_node_new(get_empty_query_node(flags));
}

static GNode *
get_operator_node(FsearchQueryToken token) {
    assert(token == FSEARCH_QUERY_TOKEN_AND || token == FSEARCH_QUERY_TOKEN_OR);
    FsearchQueryNodeOperator operator= token == FSEARCH_QUERY_TOKEN_AND ? FSEARCH_TOKEN_OPERATOR_AND
                                                                        : FSEARCH_TOKEN_OPERATOR_OR;
    return g_node_new(fsearch_query_node_new_operator(operator));
}

static gboolean
free_tree_node(GNode *node, gpointer data) {
    FsearchQueryNode *n = node->data;
    g_clear_pointer(&n, fsearch_query_node_free);
    return TRUE;
}

static void
free_tree(GNode *root) {
    g_node_traverse(root, G_IN_ORDER, G_TRAVERSE_ALL, -1, free_tree_node, NULL);
    g_clear_pointer(&root, g_node_destroy);
}

static FsearchQueryToken
top_token(GQueue *stack) {
    return (FsearchQueryToken)GPOINTER_TO_UINT(g_queue_peek_tail(stack));
}

static FsearchQueryToken
pop_token(GQueue *stack) {
    return (FsearchQueryToken)(GPOINTER_TO_UINT(g_queue_pop_tail(stack)));
}

static void
push_token(GQueue *stack, FsearchQueryToken token) {
    g_queue_push_tail(stack, GUINT_TO_POINTER((guint)token));
}

static uint32_t
get_operator_precedence(FsearchQueryToken operator) {
    switch (operator) {
    case FSEARCH_QUERY_TOKEN_AND:
        return 2;
    case FSEARCH_QUERY_TOKEN_OR:
        return 1;
    default:
        return 0;
    }
}

static GList *
append_operator(GList *list, FsearchQueryToken token) {
    if (token != FSEARCH_QUERY_TOKEN_AND && token != FSEARCH_QUERY_TOKEN_OR) {
        return list;
    }
    return g_list_append(list,
                         fsearch_query_node_new_operator(token == FSEARCH_QUERY_TOKEN_AND ? FSEARCH_TOKEN_OPERATOR_AND
                                                                                          : FSEARCH_TOKEN_OPERATOR_OR));
}

static bool
next_token_is_implicit_and_operator(FsearchQueryToken current_token, FsearchQueryToken next_token) {
    switch (current_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
        break;
    default:
        return false;
    }

    switch (next_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
        return true;
    default:
        return false;
    }
}

static GList *
handle_operator_token(GList *postfix_query, GQueue *operator_stack, FsearchQueryToken token) {
    while (!g_queue_is_empty(operator_stack)
           && get_operator_precedence(token) <= get_operator_precedence(top_token(operator_stack))) {
        postfix_query = append_operator(postfix_query, pop_token(operator_stack));
    }
    push_token(operator_stack, token);
    return postfix_query;
}

static GList *
convert_query_from_infix_to_postfix(FsearchQueryParser *parser, FsearchQueryFlags flags) {
    GQueue *operator_stack = g_queue_new();
    GList *postfix_query = NULL;

    int32_t num_open_brackets = 0;
    int32_t num_close_brackets = 0;

    FsearchQueryToken last_token = FSEARCH_QUERY_TOKEN_NONE;

    while (true) {
        GString *token_value = NULL;
        FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);

        switch (token) {
        case FSEARCH_QUERY_TOKEN_EOS:
            goto out;
        case FSEARCH_QUERY_TOKEN_AND:
        case FSEARCH_QUERY_TOKEN_OR:
            postfix_query = handle_operator_token(postfix_query, operator_stack, token);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
            num_open_brackets++;
            push_token(operator_stack, token);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
            if (num_open_brackets >= num_close_brackets + 1) {
                num_close_brackets++;
                while (top_token(operator_stack) != FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
                    postfix_query = append_operator(postfix_query, pop_token(operator_stack));
                }
                pop_token(operator_stack);
            }
            break;
        case FSEARCH_QUERY_TOKEN_WORD:
            postfix_query = g_list_append(postfix_query, fsearch_query_node_new(token_value->str, flags));
            break;
        case FSEARCH_QUERY_TOKEN_FIELD:
            postfix_query = g_list_append(postfix_query, parse_field(parser, token_value, flags));
            break;
        default:
            g_debug("[infix-postfix] ignoring unexpected token: %d", token);
            break;
        }

        GString *next_token_value = NULL;
        FsearchQueryToken next_token = fsearch_query_parser_peek_next_token(parser, &next_token_value);
        if (next_token_is_implicit_and_operator(token, next_token)) {
            postfix_query = handle_operator_token(postfix_query, operator_stack, FSEARCH_QUERY_TOKEN_AND);
        }

        if (token_value) {
            g_string_free(g_steal_pointer(&token_value), TRUE);
        }
        if (next_token_value) {
            g_string_free(g_steal_pointer(&next_token_value), TRUE);
        }
    }

out:
    while (!g_queue_is_empty(operator_stack)) {
        postfix_query = append_operator(postfix_query, pop_token(operator_stack));
    }

    g_queue_free_full(g_steal_pointer(&operator_stack), (GDestroyNotify)fsearch_query_node_free);
    return postfix_query;
}

static GNode *
build_query_tree(GList *postfix_query, FsearchQueryFlags flags) {
    if (!postfix_query) {
        return get_empty_node(flags);
    }

    GQueue *query_stack = g_queue_new();

    for (GList *q = postfix_query; q != NULL; q = q->next) {
        FsearchQueryNode *node = q->data;
        if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
            GNode *op_node = get_operator_node(node->operator== FSEARCH_TOKEN_OPERATOR_AND ? FSEARCH_QUERY_TOKEN_AND
                                                                                           : FSEARCH_QUERY_TOKEN_OR);
            GNode *right = g_queue_pop_tail(query_stack);
            GNode *left = g_queue_pop_tail(query_stack);
            g_node_append(op_node, left ? left : get_empty_node(flags));
            g_node_append(op_node, right ? right : get_empty_node(flags));
            g_queue_push_tail(query_stack, op_node);
        }
        else {
            g_queue_push_tail(query_stack, g_node_new(node));
        }
    }
    GNode *root = g_queue_pop_tail(query_stack);
    if (!g_queue_is_empty(query_stack)) {
        g_debug("[builder_tree] query stack still has nodes left!!");
    }

    g_queue_free_full(g_steal_pointer(&query_stack), (GDestroyNotify)free_tree);
    return root;
}

static GNode *
get_nodes(const char *src, FsearchQueryFlags flags) {
    assert(src != NULL);

    FsearchQueryParser *parser = fsearch_query_parser_new(src);
    GList *query_postfix = convert_query_from_infix_to_postfix(parser, flags);
    GNode *root = build_query_tree(query_postfix, flags);

    // g_print("Postfix representation of query:\n");
    // g_print("================================\n");
    // for (GList *q = query_postfix; q != NULL; q = q->next) {
    //     FsearchQueryNode *node = q->data;
    //     if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
    //         g_print("%s ", node->operator== FSEARCH_TOKEN_OPERATOR_AND ? "AND" : "OR");
    //     }
    //     else {
    //         g_print("%s ", node->search_term ? node->search_term : "[empty query]");
    //     }
    // }
    // g_print("\n");
    g_list_free(g_steal_pointer(&query_postfix));

    g_clear_pointer(&parser, fsearch_query_parser_free);
    return root;
}

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchQueryFlags flags) {
    if (flags & QUERY_FLAG_REGEX) {
        // If we're in regex mode we're passing the whole search term to the regex engine
        // i.e. there's only one query node
        return g_node_new(fsearch_query_node_new(search_term, flags));
    }

    return get_nodes(search_term, flags);
}

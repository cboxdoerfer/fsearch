#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-query-node"

#include "fsearch_query_node.h"
#include "fsearch_limits.h"
#include "fsearch_query_match_data.h"
#include "fsearch_query_parser.h"
#include "fsearch_size_utils.h"
#include "fsearch_string_utils.h"
#include "fsearch_time_utils.h"
#include "fsearch_utf.h"
#include <assert.h>
#include <glib.h>
#include <stdbool.h>
#include <string.h>

typedef struct FsearchQueryParseContext {
    GList *suffix_list;
    GQueue *operator_stack;
    FsearchQueryToken last_token;
    GPtrArray *macro_filters;
    GQueue *macro_stack;
} FsearchQueryParseContext;

static FsearchQueryNode *fsearch_query_node_new_operator(FsearchQueryNodeOperator operator);

static void
handle_operator_token(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token);

static void
parse_expression(FsearchQueryParser *parser, FsearchQueryParseContext *parse_ctx, FsearchQueryFlags flags);

static void
parse_field(FsearchQueryParser *parser,
            FsearchQueryParseContext *parse_ctx,
            GString *field_name,
            bool is_empty_field,
            FsearchQueryFlags flags);

static void
parse_modifier(FsearchQueryParser *parser,
               FsearchQueryParseContext *parse_ctx,
               bool is_empty_field,
               FsearchQueryFlags flags);

static void
parse_field_exact(FsearchQueryParser *parser,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags);

static void
parse_field_date_modified(FsearchQueryParser *parser,
                          FsearchQueryParseContext *parse_ctx,
                          bool is_empty_field,
                          FsearchQueryFlags flags);

static void
parse_field_size(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static void
parse_field_extension(FsearchQueryParser *parser,
                      FsearchQueryParseContext *parse_ctx,
                      bool is_empty_field,
                      FsearchQueryFlags flags);

static void
parse_field_regex(FsearchQueryParser *parser,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags);

static void
parse_field_parent(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static void
parse_field_path(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static void
parse_field_case(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static void
parse_field_nocase(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static void
parse_field_noregex(FsearchQueryParser *parser,
                    FsearchQueryParseContext *parse_ctx,
                    bool is_empty_field,
                    FsearchQueryFlags flags);

static void
parse_field_nopath(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static void
parse_field_folder(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags);

static void
parse_field_file(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags);

static gboolean
free_tree_node(GNode *node, gpointer data);

static void
free_tree(GNode *root);

static void
add_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token);

static void
add_query_to_parse_context(FsearchQueryParseContext *parse_ctx, FsearchQueryNode *node, FsearchQueryToken token) {
    if (!node) {
        return;
    }
    add_implicit_and_if_necessary(parse_ctx, token);

    parse_ctx->suffix_list = g_list_append(parse_ctx->suffix_list, node);
    parse_ctx->last_token = token;
}

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

typedef void(FsearchTokenFieldParser)(FsearchQueryParser *, FsearchQueryParseContext *parse_ctx, bool, FsearchQueryFlags);

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
fsearch_highlight_func_none(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    return true;
}

static u_int32_t
fsearch_search_func_true(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    return 1;
}

static uint32_t
fsearch_search_func_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    if (!node->search_term_list) {
        return 0;
    }
    FsearchDatabaseEntry *entry = fsearch_query_match_data_get_entry(match_data);
    const char *ext = db_entry_get_extension(entry);
    if (!ext) {
        return 0;
    }
    for (uint32_t i = 0; i < node->num_search_term_list_entries; i++) {
        if (node->flags & QUERY_FLAG_MATCH_CASE) {
            if (!strcmp(ext, node->search_term_list[i])) {
                return 1;
            }
        }
        else if (!strcasecmp(ext, node->search_term_list[i])) {
            return 1;
        }
    }
    return 0;
}

static bool
fsearch_highlight_func_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    if (!node->search_term_list) {
        return false;
    }
    if (!fsearch_search_func_extension(node, match_data)) {
        return false;
    }
    FsearchDatabaseEntry *entry = fsearch_query_match_data_get_entry(match_data);
    const char *ext = db_entry_get_extension(entry);
    const char *name = fsearch_query_match_data_get_name_str(match_data);
    if (!name) {
        return false;
    }
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);

    PangoAttribute *pa_name = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pa_name->start_index = name_len - ext_len;
    pa_name->end_index = G_MAXUINT;
    fsearch_query_match_data_add_highlight(match_data, pa_name, DATABASE_INDEX_TYPE_NAME);

    PangoAttribute *pa_ext = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    fsearch_query_match_data_add_highlight(match_data, pa_ext, DATABASE_INDEX_TYPE_EXTENSION);
    return true;
}

static uint32_t
fsearch_search_func_date_modified(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    FsearchDatabaseEntry *entry = fsearch_query_match_data_get_entry(match_data);
    if (entry) {
        time_t time = db_entry_get_mtime(entry);
        switch (node->comparison_type) {
        case FSEARCH_TOKEN_COMPARISON_EQUAL:
            return time == node->time;
        case FSEARCH_TOKEN_COMPARISON_GREATER:
            return time > node->time;
        case FSEARCH_TOKEN_COMPARISON_SMALLER:
            return time < node->time;
        case FSEARCH_TOKEN_COMPARISON_GREATER_EQ:
            return time >= node->time;
        case FSEARCH_TOKEN_COMPARISON_SMALLER_EQ:
            return time <= node->time;
        case FSEARCH_TOKEN_COMPARISON_RANGE:
            return node->time <= time && time < node->time_upper_limit;
        }
    }
    return 0;
}

static uint32_t
fsearch_search_func_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    FsearchDatabaseEntry *entry = fsearch_query_match_data_get_entry(match_data);
    if (entry) {
        int64_t size = db_entry_get_size(entry);
        switch (node->comparison_type) {
        case FSEARCH_TOKEN_COMPARISON_EQUAL:
            return size == node->size;
        case FSEARCH_TOKEN_COMPARISON_GREATER:
            return size > node->size;
        case FSEARCH_TOKEN_COMPARISON_SMALLER:
            return size < node->size;
        case FSEARCH_TOKEN_COMPARISON_GREATER_EQ:
            return size >= node->size;
        case FSEARCH_TOKEN_COMPARISON_SMALLER_EQ:
            return size <= node->size;
        case FSEARCH_TOKEN_COMPARISON_RANGE:
            return node->size <= size && size < node->size_upper_limit;
        }
    }
    return 0;
}

static void
add_path_highlight(FsearchQueryMatchData *match_data, uint32_t start_idx, uint32_t needle_len) {
    // It's possible that the path highlighting spans across both the path and name string
    const char *name = fsearch_query_match_data_get_name_str(match_data);
    const char *path = fsearch_query_match_data_get_path_str(match_data);
    const size_t name_len = strlen(name);
    const size_t path_len = strlen(path);
    const size_t parent_len = path_len - name_len;

    PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pa->start_index = start_idx;
    if (pa->start_index > parent_len) {
        // the matching part is only in the file name
        pa->start_index -= parent_len;
        pa->end_index = pa->start_index + needle_len;
        fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_NAME);
        return;
    }
    else if (pa->start_index + needle_len > parent_len) {
        // the matching part spans across the path and name
        pa->end_index = G_MAXUINT;
        fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_PATH);
        PangoAttribute *pa_name = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pa_name->start_index = 0;
        pa_name->end_index = pa->start_index + needle_len - parent_len;
        fsearch_query_match_data_add_highlight(match_data, pa_name, DATABASE_INDEX_TYPE_NAME);
        return;
    }
    else {
        // the matching part is only in the path name
        pa->end_index = pa->start_index + needle_len;
        fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_PATH);
        return;
    }
}

static bool
fsearch_highlight_func_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    if (fsearch_search_func_size(node, match_data)) {
        PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_SIZE);
        return true;
    }
    return false;
}

static bool
fsearch_highlight_func_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    const bool search_in_path = node->flags & QUERY_FLAG_SEARCH_IN_PATH;
    const char *haystack = search_in_path ? fsearch_query_match_data_get_path_str(match_data)
                                          : fsearch_query_match_data_get_name_str(match_data);
    int32_t thread_id = fsearch_query_match_data_get_thread_id(match_data);
    const size_t haystack_len = strlen(haystack);
    pcre2_match_data *regex_match_data = g_ptr_array_index(node->regex_match_data_for_threads, thread_id);
    if (!regex_match_data) {
        return false;
    }
    int num_matches =
        pcre2_match(node->regex, (PCRE2_SPTR)haystack, (PCRE2_SIZE)haystack_len, 0, 0, regex_match_data, NULL);
    if (num_matches <= 0) {
        return false;
    }

    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(regex_match_data);
    for (int i = 0; i < num_matches; i++) {
        const uint32_t start_idx = ovector[2 * i];
        const uint32_t end_idx = ovector[2 * i + 1];
        if (!search_in_path) {
            PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            pa->start_index = start_idx;
            pa->end_index = end_idx;
            fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_NAME);
        }
        else {
            add_path_highlight(match_data, start_idx, end_idx - start_idx);
        }
    }
    return true;
}

static uint32_t
fsearch_search_func_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    const char *haystack = node->flags & QUERY_FLAG_SEARCH_IN_PATH ? fsearch_query_match_data_get_path_str(match_data)
                                                                   : fsearch_query_match_data_get_name_str(match_data);
    const size_t haystack_len = strlen(haystack);
    if (!node->regex) {
        return 0;
    }
    const int32_t thread_id = fsearch_query_match_data_get_thread_id(match_data);
    pcre2_match_data *regex_match_data = g_ptr_array_index(node->regex_match_data_for_threads, thread_id);
    if (!regex_match_data) {
        return 0;
    }
    int num_matches = 0;
    if (node->regex_jit_available) {
        num_matches =
            pcre2_jit_match(node->regex, (PCRE2_SPTR)haystack, (PCRE2_SIZE)haystack_len, 0, 0, regex_match_data, NULL);
    }
    else {
        num_matches =
            pcre2_match(node->regex, (PCRE2_SPTR)haystack, (PCRE2_SIZE)haystack_len, 0, 0, regex_match_data, NULL);
    }
    return num_matches > 0 ? 1 : 0;
}

// static uint32_t
// fsearch_search_func_normal_icase_u8_fast(FsearchToken *token, FsearchQueryMatcher *match_data) {
//     FsearchUtfConversionBuffer *builder = token->get_haystack(match_data);
//     if (G_LIKELY(builder->string_utf8_is_folded)) {
//         return strstr(builder->string_utf8_folded, token->needle_buffer->string_utf8_folded) ? 1 : 0;
//     }
//     else {
//         // failed to fold case, fall back to fast but not accurate ascii search
//         // g_warning("[utf8_search] failed to lower case: %s", haystack);
//         // return strcasestr(haystack, needle) ? 1 : 0;
//     }
// }

static uint32_t
comp_func_utf(FsearchUtfBuilder *haystack_builder, FsearchUtfBuilder *needle_builder, FsearchQueryFlags flags) {
    if (G_LIKELY(haystack_builder->string_is_folded_and_normalized)) {
        if (flags & QUERY_FLAG_EXACT_MATCH) {
            return !u_strCompare(haystack_builder->string_normalized_folded,
                                 haystack_builder->string_normalized_folded_len,
                                 needle_builder->string_normalized_folded,
                                 needle_builder->string_normalized_folded_len,
                                 false)
                     ? 1
                     : 0;
        }
        return u_strFindFirst(haystack_builder->string_normalized_folded,
                              haystack_builder->string_normalized_folded_len,
                              needle_builder->string_normalized_folded,
                              needle_builder->string_normalized_folded_len)
                 ? 1
                 : 0;
    }
    else {
        // failed to fold case, fall back to fast but not accurate ascii search
        g_warning("[utf8_search] failed to lower case: %s", haystack_builder->string);
        if (flags & QUERY_FLAG_EXACT_MATCH) {
            return !strcasecmp(haystack_builder->string, needle_builder->string) ? 1 : 0;
        }
        return strcasestr(haystack_builder->string, needle_builder->string) ? 1 : 0;
    }
}

static uint32_t
fsearch_search_func_utf(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    return comp_func_utf(node->flags & QUERY_FLAG_SEARCH_IN_PATH
                             ? fsearch_query_match_data_get_utf_path_builder(match_data)
                             : fsearch_query_match_data_get_utf_name_builder(match_data),
                         node->needle_builder,
                         node->flags);
}

static uint32_t
fsearch_search_func_parent_utf(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    return comp_func_utf(node->flags & QUERY_FLAG_SEARCH_IN_PATH
                             ? fsearch_query_match_data_get_utf_parent_path_builder(match_data)
                             : fsearch_query_match_data_get_utf_name_builder(match_data),
                         node->needle_builder,
                         node->flags);
}

static bool
fsearch_highlight_func_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    const bool search_in_path = node->flags & QUERY_FLAG_SEARCH_IN_PATH;
    const char *haystack = search_in_path ? fsearch_query_match_data_get_path_str(match_data)
                                          : fsearch_query_match_data_get_name_str(match_data);
    if (node->flags & QUERY_FLAG_EXACT_MATCH) {
        if (node->flags & QUERY_FLAG_MATCH_CASE ? !strcmp(haystack, node->needle) : !strcasecmp(haystack, node->needle)) {
            PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_NAME);
            if (search_in_path) {
                PangoAttribute *pa_path = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                fsearch_query_match_data_add_highlight(match_data, pa_path, DATABASE_INDEX_TYPE_PATH);
            }
            return true;
        }
        return false;
    }
    char *dest = node->flags & QUERY_FLAG_MATCH_CASE ? strstr(haystack, node->needle)
                                                     : strcasestr(haystack, node->needle);
    if (!dest) {
        return false;
    }
    if (search_in_path) {
        const size_t needle_len = strlen(node->needle);
        add_path_highlight(match_data, dest - haystack, needle_len);
    }
    else {
        PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pa->start_index = dest - haystack;
        pa->end_index = pa->start_index + strlen(node->needle);
        fsearch_query_match_data_add_highlight(match_data, pa, DATABASE_INDEX_TYPE_NAME);
    }
    return true;
}

static uint32_t
comp_func_ascii(const char *haystack, const char *needle, FsearchQueryFlags flags) {
    if (G_UNLIKELY(flags & QUERY_FLAG_EXACT_MATCH)) {
        return !(flags & QUERY_FLAG_MATCH_CASE ? strcmp(haystack, needle) : strcasecmp(haystack, needle)) ? 1 : 0;
    }
    return (flags & QUERY_FLAG_MATCH_CASE ? strstr(haystack, needle) : strcasestr(haystack, needle)) ? 1 : 0;
}

static uint32_t
fsearch_search_func_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    const char *haystack = node->flags & QUERY_FLAG_SEARCH_IN_PATH ? fsearch_query_match_data_get_path_str(match_data)
                                                                   : fsearch_query_match_data_get_name_str(match_data);
    return comp_func_ascii(haystack, node->needle, node->flags);
}

static uint32_t
fsearch_search_func_parent_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data) {
    const char *haystack = fsearch_query_match_data_get_parent_path_str(match_data);
    return comp_func_ascii(haystack, node->needle, node->flags);
}

static void
fsearch_query_node_free(void *data) {
    FsearchQueryNode *node = data;
    assert(node != NULL);

    fsearch_utf_builder_clear(node->needle_builder);
    if (node->query_description) {
        g_string_free(g_steal_pointer(&node->query_description), TRUE);
    }
    g_clear_pointer(&node->search_term_list, g_strfreev);
    g_clear_pointer(&node->needle_builder, free);
    g_clear_pointer(&node->needle, g_free);

    if (node->regex_match_data_for_threads) {
        g_ptr_array_free(g_steal_pointer(&node->regex_match_data_for_threads), TRUE);
    }
    g_clear_pointer(&node->regex, pcre2_code_free);

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
fsearch_query_node_new_date_modified(FsearchQueryFlags flags,
                                     time_t dm_start,
                                     time_t dm_end,
                                     FsearchTokenComparisonType comp_type) {
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    if (comp_type == FSEARCH_TOKEN_COMPARISON_EQUAL) {
        new->needle = g_strdup_printf("=%ld", dm_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_GREATER_EQ) {
        new->needle = g_strdup_printf(">=%ld", dm_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_GREATER) {
        new->needle = g_strdup_printf(">%ld", dm_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_SMALLER_EQ) {
        new->needle = g_strdup_printf("<=%ld", dm_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_SMALLER) {
        new->needle = g_strdup_printf("<%ld", dm_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_RANGE) {
        new->needle = g_strdup_printf("%ld..%ld", dm_start, dm_end);
    }
    new->query_description = g_string_new("date-modified");
    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->time = dm_start;
    new->time_upper_limit = dm_end;
    new->comparison_type = comp_type;
    new->search_func = fsearch_search_func_date_modified;
    new->highlight_func = NULL;
    new->flags = flags;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_size(FsearchQueryFlags flags,
                            int64_t size_start,
                            int64_t size_end,
                            FsearchTokenComparisonType comp_type) {
    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    if (comp_type == FSEARCH_TOKEN_COMPARISON_EQUAL) {
        new->needle = g_strdup_printf("=%ld", size_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_GREATER_EQ) {
        new->needle = g_strdup_printf(">=%ld", size_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_GREATER) {
        new->needle = g_strdup_printf(">%ld", size_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_SMALLER_EQ) {
        new->needle = g_strdup_printf("<=%ld", size_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_SMALLER) {
        new->needle = g_strdup_printf("<%ld", size_start);
    }
    else if (comp_type == FSEARCH_TOKEN_COMPARISON_RANGE) {
        new->needle = g_strdup_printf("%ld..%ld", size_start, size_end);
    }
    new->query_description = g_string_new("size");
    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->size = size_start;
    new->size_upper_limit = size_end;
    new->comparison_type = comp_type;
    new->search_func = fsearch_search_func_size;
    new->highlight_func = fsearch_highlight_func_size;
    new->flags = flags;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_operator(FsearchQueryNodeOperator operator) {
    assert(operator== FSEARCH_TOKEN_OPERATOR_AND || operator== FSEARCH_TOKEN_OPERATOR_OR ||
           operator== FSEARCH_TOKEN_OPERATOR_NOT);
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
    new->highlight_func = fsearch_highlight_func_none;
    new->flags = flags;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_regex(const char *search_term, FsearchQueryFlags flags) {
    int error_code;

    PCRE2_SIZE erroroffset;
    uint32_t regex_options = PCRE2_UTF | (flags & QUERY_FLAG_MATCH_CASE ? 0 : PCRE2_CASELESS);
    pcre2_code *regex = pcre2_compile((PCRE2_SPTR)search_term,
                                      (PCRE2_SIZE)strlen(search_term),
                                      regex_options,
                                      &error_code,
                                      &erroroffset,
                                      NULL);
    if (!regex) {
        PCRE2_UCHAR buffer[256] = "";
        pcre2_get_error_message(error_code, buffer, sizeof(buffer));
        g_debug("[regex] PCRE2 compilation failed at offset %d. Error message: %s", (int)erroroffset, buffer);
        return NULL;
    }

    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    new->query_description = g_string_new("regex");
    new->needle = g_strdup(search_term);
    new->regex = regex;
    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->flags = flags;

    if (pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE) != 0) {
        g_debug("[regex] JIT compilation failed.\n");
        new->regex_jit_available = false;
    }
    else {
        new->regex_jit_available = true;
    }
    new->regex_match_data_for_threads = g_ptr_array_sized_new(FSEARCH_THREAD_LIMIT);
    g_ptr_array_set_free_func(new->regex_match_data_for_threads, (GDestroyNotify)pcre2_match_data_free);
    for (int32_t i = 0; i < FSEARCH_THREAD_LIMIT; i++) {
        g_ptr_array_add(new->regex_match_data_for_threads, pcre2_match_data_create_from_pattern(new->regex, NULL));
    }

    new->search_func = fsearch_search_func_regex;
    new->highlight_func = fsearch_highlight_func_regex;
    return new;
}

static FsearchQueryNode *
fsearch_query_node_new_wildcard(const char *search_term, FsearchQueryFlags flags) {
    // We convert the wildcard pattern to a regex pattern
    // The regex engine is not only faster than fnmatch, but it also handles utf8 strings better
    // and it provides matching information, which are useful for the highlighting engine
    char *regex_search_term = fs_str_convert_wildcard_to_regex_expression(search_term);
    if (!regex_search_term) {
        return NULL;
    }
    FsearchQueryNode *res = fsearch_query_node_new_regex(regex_search_term, flags);
    g_clear_pointer(&regex_search_term, free);
    return res;
}

static FsearchQueryNode *
fsearch_query_node_new(const char *search_term, FsearchQueryFlags flags) {
    bool has_separator = strchr(search_term, G_DIR_SEPARATOR) ? 1 : 0;
    const bool search_in_path = flags & QUERY_FLAG_SEARCH_IN_PATH
                             || (flags & QUERY_FLAG_AUTO_SEARCH_IN_PATH && has_separator);
    if (search_in_path) {
        flags |= QUERY_FLAG_SEARCH_IN_PATH;
    }
    if ((flags & QUERY_FLAG_AUTO_MATCH_CASE) && fs_str_utf8_has_upper(search_term)) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }

    if (flags & QUERY_FLAG_REGEX) {
        return fsearch_query_node_new_regex(search_term, flags);
    }
    else if (strchr(search_term, '*') || strchr(search_term, '?')) {
        return fsearch_query_node_new_wildcard(search_term, flags);
    }

    FsearchQueryNode *new = calloc(1, sizeof(FsearchQueryNode));
    assert(new != NULL);

    new->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    new->flags = flags;
    node_init_needle(new, search_term);

    if (fs_str_case_is_ascii(search_term) || flags & QUERY_FLAG_MATCH_CASE) {
        new->search_func = fsearch_search_func_ascii;
        new->highlight_func = fsearch_highlight_func_ascii;
        new->query_description = g_string_new("ascii_icase");
    }
    else {
        new->search_func = fsearch_search_func_utf;
        new->highlight_func = NULL;
        new->query_description = g_string_new("utf_icase");
    }
    return new;
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
parse_size_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchTokenComparisonType comp_type) {
    char *end_ptr = NULL;
    int64_t size_start = 0;
    int64_t size_end = 0;
    if (fsearch_size_parse(string->str, &size_start, &end_ptr)) {
        if (string_starts_with_range(end_ptr, &end_ptr)) {
            if (end_ptr && *end_ptr == '\0') {
                // interpret size:SIZE.. or size:SIZE- with a missing upper bound as size:>=SIZE
                comp_type = FSEARCH_TOKEN_COMPARISON_GREATER_EQ;
            }
            else if (fsearch_size_parse(end_ptr, &size_end, &end_ptr)) {
                comp_type = FSEARCH_TOKEN_COMPARISON_RANGE;
            }
        }
        return fsearch_query_node_new_size(flags, size_start, size_end, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return NULL;
}

static FsearchQueryNode *
parse_size(GString *string, FsearchQueryFlags flags, FsearchTokenComparisonType comp_type) {
    char *end_ptr = NULL;
    int64_t size = 0;
    if (fsearch_size_parse(string->str, &size, &end_ptr)) {
        return fsearch_query_node_new_size(flags, size, size, comp_type);
    }
    g_debug("[size:] invalid argument: %s", string->str);
    return NULL;
}

static void
parse_field_size(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    FsearchTokenComparisonType comp_type = FSEARCH_TOKEN_COMPARISON_EQUAL;
    FsearchQueryNode *result = NULL;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_EQUAL:
        comp_type = FSEARCH_TOKEN_COMPARISON_EQUAL;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER:
        comp_type = FSEARCH_TOKEN_COMPARISON_SMALLER;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER_EQ:
        comp_type = FSEARCH_TOKEN_COMPARISON_SMALLER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER:
        comp_type = FSEARCH_TOKEN_COMPARISON_GREATER;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER_EQ:
        comp_type = FSEARCH_TOKEN_COMPARISON_GREATER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_WORD:
        result = parse_size_with_optional_range(token_value, flags, comp_type);
        goto out;
    default:
        g_debug("[size:] invalid or missing argument");
        goto out;
    }

    GString *next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_parser_get_next_token(parser, &next_token_value);
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
    if (result) {
        add_query_to_parse_context(parse_ctx, result, FSEARCH_QUERY_TOKEN_FIELD);
    }
}

static FsearchQueryNode *
parse_date_with_optional_range(GString *string, FsearchQueryFlags flags, FsearchTokenComparisonType comp_type) {
    char *end_ptr = NULL;
    time_t time_start = 0;
    time_t time_end = 0;
    if (fsearch_time_parse_range(string->str, &time_start, &time_end, &end_ptr)) {
        if (string_starts_with_range(end_ptr, &end_ptr)) {
            if (end_ptr && *end_ptr == '\0') {
                // interpret size:SIZE.. or size:SIZE- with a missing upper bound as size:>=SIZE
                comp_type = FSEARCH_TOKEN_COMPARISON_GREATER_EQ;
            }
            else if (fsearch_time_parse_range(end_ptr, NULL, &time_end, &end_ptr)) {
                comp_type = FSEARCH_TOKEN_COMPARISON_RANGE;
            }
        }
        else {
            comp_type = FSEARCH_TOKEN_COMPARISON_RANGE;
        }
        return fsearch_query_node_new_date_modified(flags, time_start, time_end, comp_type);
    }
    g_debug("[date-modified:] invalid argument: %s", string->str);
    return NULL;
}

static FsearchQueryNode *
parse_date(GString *string, FsearchQueryFlags flags, FsearchTokenComparisonType comp_type) {
    char *end_ptr = NULL;
    time_t date = 0;
    time_t date_end = 0;
    if (fsearch_time_parse_range(string->str, &date, &date_end, &end_ptr)) {
        time_t dm_start = date;
        time_t dm_end = date_end;
        switch (comp_type) {
        case FSEARCH_TOKEN_COMPARISON_EQUAL:
            // Equal actually refers to a time range. E.g. dm:today is the time range from 0:00:00 to 23:59:59
            comp_type = FSEARCH_TOKEN_COMPARISON_RANGE;
            dm_start = date;
            dm_end = date_end;
            break;
        case FSEARCH_TOKEN_COMPARISON_GREATER_EQ:
        case FSEARCH_TOKEN_COMPARISON_SMALLER:
            dm_start = date;
            break;
        case FSEARCH_TOKEN_COMPARISON_GREATER:
        case FSEARCH_TOKEN_COMPARISON_SMALLER_EQ:
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

static void
parse_field_date_modified(FsearchQueryParser *parser,
                          FsearchQueryParseContext *parse_ctx,
                          bool is_empty_field,
                          FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    FsearchTokenComparisonType comp_type = FSEARCH_TOKEN_COMPARISON_EQUAL;
    FsearchQueryNode *result = NULL;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_EQUAL:
        comp_type = FSEARCH_TOKEN_COMPARISON_EQUAL;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER:
        comp_type = FSEARCH_TOKEN_COMPARISON_SMALLER;
        break;
    case FSEARCH_QUERY_TOKEN_SMALLER_EQ:
        comp_type = FSEARCH_TOKEN_COMPARISON_SMALLER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER:
        comp_type = FSEARCH_TOKEN_COMPARISON_GREATER;
        break;
    case FSEARCH_QUERY_TOKEN_GREATER_EQ:
        comp_type = FSEARCH_TOKEN_COMPARISON_GREATER_EQ;
        break;
    case FSEARCH_QUERY_TOKEN_WORD:
        result = parse_date_with_optional_range(token_value, flags, comp_type);
        goto out;
    default:
        g_debug("[size:] invalid or missing argument");
        goto out;
    }

    GString *next_token_value = NULL;
    FsearchQueryToken next_token = fsearch_query_parser_get_next_token(parser, &next_token_value);
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
    if (result) {
        add_query_to_parse_context(parse_ctx, result, FSEARCH_QUERY_TOKEN_FIELD);
    }
}

static void
parse_field_extension(FsearchQueryParser *parser,
                      FsearchQueryParseContext *parse_ctx,
                      bool is_empty_field,
                      FsearchQueryFlags flags) {
    if (!is_empty_field && fsearch_query_parser_peek_next_token(parser, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return;
    }
    FsearchQueryNode *result = calloc(1, sizeof(FsearchQueryNode));
    result->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    result->query_description = g_string_new("ext");
    result->search_func = fsearch_search_func_extension;
    result->highlight_func = fsearch_highlight_func_extension;
    result->flags = flags;
    if (is_empty_field) {
        // Show all files with no extension
        result->needle = g_strdup("");
        result->search_term_list = calloc(2, sizeof(char *));
        result->search_term_list[0] = g_strdup("");
        result->search_term_list[1] = NULL;
    }
    else {
        GString *token_value = NULL;
        fsearch_query_parser_get_next_token(parser, &token_value);
        result->needle = g_strdup(token_value->str);
        result->search_term_list = g_strsplit(token_value->str, ";", -1);
        if (token_value) {
            g_string_free(g_steal_pointer(&token_value), TRUE);
        }
    }
    result->num_search_term_list_entries = result->search_term_list ? g_strv_length(result->search_term_list) : 0;

    return add_query_to_parse_context(parse_ctx,
                                      result,
                                      is_empty_field ? FSEARCH_QUERY_TOKEN_FIELD_EMPTY : FSEARCH_QUERY_TOKEN_FIELD);
}

static void
parse_field_parent(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field || fsearch_query_parser_peek_next_token(parser, NULL) != FSEARCH_QUERY_TOKEN_WORD) {
        return;
    }
    GString *token_value = NULL;
    fsearch_query_parser_get_next_token(parser, &token_value);

    FsearchQueryNode *result = calloc(1, sizeof(FsearchQueryNode));
    result->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    result->query_description = g_string_new("parent");
    node_init_needle(result, token_value->str);

    result->highlight_func = NULL;
    result->flags = flags;
    if (fs_str_case_is_ascii(result->needle) || flags & QUERY_FLAG_MATCH_CASE) {
        result->search_func = fsearch_search_func_parent_ascii;
        result->query_description = g_string_new("parent_ascii");
    }
    else {
        result->search_func = fsearch_search_func_parent_utf;
        result->query_description = g_string_new("parent_utf");
    }

    g_string_free(g_steal_pointer(&token_value), TRUE);

    return add_query_to_parse_context(parse_ctx, result, FSEARCH_QUERY_TOKEN_FIELD);
}

static void
parse_modifier(FsearchQueryParser *parser,
               FsearchQueryParseContext *parse_ctx,
               bool is_empty_field,
               FsearchQueryFlags flags) {
    if (is_empty_field) {
        return add_query_to_parse_context(parse_ctx,
                                          fsearch_query_node_new_match_everything(flags),
                                          FSEARCH_QUERY_TOKEN_FIELD_EMPTY);
    }
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    if (token == FSEARCH_QUERY_TOKEN_WORD) {
        add_query_to_parse_context(parse_ctx, fsearch_query_node_new(token_value->str, flags), FSEARCH_QUERY_TOKEN_FIELD);
    }
    else if (token == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
        parse_expression(parser, parse_ctx, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD) {
        parse_field(parser, parse_ctx, token_value, false, flags);
    }
    else if (token == FSEARCH_QUERY_TOKEN_FIELD_EMPTY) {
        parse_field(parser, parse_ctx, token_value, true, flags);
    }
    // else {
    //      //add_query_to_parse_context(parse_ctx, fsearch_query_node_new_match_everything(flags),
    //      FSEARCH_QUERY_TOKEN_FIELD_EMPTY);
    // }

    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }

    return;
}

static void
parse_field_exact(FsearchQueryParser *parser,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_EXACT_MATCH);
}

static void
parse_field_path(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_SEARCH_IN_PATH);
}

static void
parse_field_nopath(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_SEARCH_IN_PATH);
}

static void
parse_field_case(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_MATCH_CASE);
}

static void
parse_field_nocase(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_MATCH_CASE);
}

static void
parse_field_regex(FsearchQueryParser *parser,
                  FsearchQueryParseContext *parse_ctx,
                  bool is_empty_field,
                  FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_REGEX);
}

static void
parse_field_noregex(FsearchQueryParser *parser,
                    FsearchQueryParseContext *parse_ctx,
                    bool is_empty_field,
                    FsearchQueryFlags flags) {
    if (is_empty_field) {
        return;
    }
    return parse_modifier(parser, parse_ctx, is_empty_field, flags & ~QUERY_FLAG_REGEX);
}

static void
parse_field_folder(FsearchQueryParser *parser,
                   FsearchQueryParseContext *parse_ctx,
                   bool is_empty_field,
                   FsearchQueryFlags flags) {
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_FOLDERS_ONLY);
}

static void
parse_field_file(FsearchQueryParser *parser,
                 FsearchQueryParseContext *parse_ctx,
                 bool is_empty_field,
                 FsearchQueryFlags flags) {
    return parse_modifier(parser, parse_ctx, is_empty_field, flags | QUERY_FLAG_FILES_ONLY);
}

static bool
parse_filter_macros(FsearchQueryParseContext *parse_ctx, GString *name, FsearchQueryFlags flags) {
    for (uint32_t i = 0; i < parse_ctx->macro_filters->len; ++i) {
        FsearchFilter *filter = g_ptr_array_index(parse_ctx->macro_filters, i);
        if (strcmp(name->str, filter->macro) != 0) {
            continue;
        }
        if (g_queue_find(parse_ctx->macro_stack, filter)) {
            g_debug("[expand_filter_macros] nested macro detected. Stop parsing of macro.");
            return true;
        }
        if (fs_str_is_empty(filter->query)) {
            // We don't need to process an empty macro query
            return true;
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
        FsearchQueryParser *macro_parser = fsearch_query_parser_new(filter->query);

        g_queue_push_tail(parse_ctx->macro_stack, filter);
        parse_expression(macro_parser, parse_ctx, flags);
        g_queue_pop_tail(parse_ctx->macro_stack);

        g_clear_pointer(&macro_parser, fsearch_query_parser_free);

        return true;
    }
    return false;
}

static void
parse_field(FsearchQueryParser *parser,
            FsearchQueryParseContext *parse_ctx,
            GString *field_name,
            bool is_empty_field,
            FsearchQueryFlags flags) {
    // g_debug("[field] detected: [%s:]", field_name->str);
    if (!parse_filter_macros(parse_ctx, field_name, flags)) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(supported_fields); ++i) {
            if (!strcmp(supported_fields[i].name, field_name->str)) {
                supported_fields[i].parser(parser, parse_ctx, is_empty_field, flags);
                return;
            }
        }
    }
}

static GNode *
get_empty_node(FsearchQueryFlags flags) {
    return g_node_new(fsearch_query_node_new_match_everything(flags));
}

static GNode *
get_operator_node(FsearchQueryNodeOperator operator) {
    return g_node_new(fsearch_query_node_new_operator(operator));
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

static void
append_operator(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token) {
    FsearchQueryNodeOperator op = 0;
    switch (token) {
    case FSEARCH_QUERY_TOKEN_AND:
        op = FSEARCH_TOKEN_OPERATOR_AND;
        break;
    case FSEARCH_QUERY_TOKEN_OR:
        op = FSEARCH_TOKEN_OPERATOR_OR;
        break;
    case FSEARCH_QUERY_TOKEN_NOT:
        op = FSEARCH_TOKEN_OPERATOR_NOT;
        break;
    default:
        return;
    }
    parse_ctx->suffix_list = g_list_append(parse_ctx->suffix_list, fsearch_query_node_new_operator(op));
    return;
}

static void
add_implicit_and_if_necessary(FsearchQueryParseContext *parse_ctx, FsearchQueryToken next_token) {
    switch (parse_ctx->last_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
    case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
        break;
    default:
        return;
    }

    switch (next_token) {
    case FSEARCH_QUERY_TOKEN_WORD:
    case FSEARCH_QUERY_TOKEN_FIELD:
    case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
    case FSEARCH_QUERY_TOKEN_NOT:
    case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
        handle_operator_token(parse_ctx, FSEARCH_QUERY_TOKEN_AND);
        return;
    default:
        return;
    }
}

static void
handle_operator_token(FsearchQueryParseContext *parse_ctx, FsearchQueryToken token) {
    parse_ctx->last_token = token;
    while (!g_queue_is_empty(parse_ctx->operator_stack)
           && get_operator_precedence(token) <= get_operator_precedence(top_token(parse_ctx->operator_stack))) {
        append_operator(parse_ctx, pop_token(parse_ctx->operator_stack));
    }
    push_token(parse_ctx->operator_stack, token);
    return;
}

static bool
uneven_number_of_consecutive_not_tokens(FsearchQueryParser *parser, FsearchQueryToken current_token) {
    if (current_token != FSEARCH_QUERY_TOKEN_NOT) {
        return false;
    }
    bool uneven_number_of_not_tokens = true;
    while (fsearch_query_parser_peek_next_token(parser, NULL) == FSEARCH_QUERY_TOKEN_NOT) {
        fsearch_query_parser_get_next_token(parser, NULL);
        uneven_number_of_not_tokens = !uneven_number_of_not_tokens;
    }
    return uneven_number_of_not_tokens;
}

static void
parse_expression(FsearchQueryParser *parser, FsearchQueryParseContext *parse_ctx, FsearchQueryFlags flags) {
    uint32_t num_open_brackets = 0;
    uint32_t num_close_brackets = 0;

    while (true) {
        GString *token_value = NULL;
        FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);

        switch (token) {
        case FSEARCH_QUERY_TOKEN_EOS:
            goto out;
        case FSEARCH_QUERY_TOKEN_NOT:
            if (uneven_number_of_consecutive_not_tokens(parser, token)) {
                // We want to support consecutive NOT operators (i.e. `NOT NOT a`)
                // so even numbers of NOT operators get ignored and for uneven numbers
                // we simply add a single one
                add_implicit_and_if_necessary(parse_ctx, token);
                handle_operator_token(parse_ctx, token);
            }
            break;
        case FSEARCH_QUERY_TOKEN_AND:
        case FSEARCH_QUERY_TOKEN_OR:
            handle_operator_token(parse_ctx, token);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_OPEN:
            num_open_brackets++;
            add_implicit_and_if_necessary(parse_ctx, token);
            parse_ctx->last_token = token;
            push_token(parse_ctx->operator_stack, token);
            break;
        case FSEARCH_QUERY_TOKEN_BRACKET_CLOSE:
            if (num_open_brackets > num_close_brackets) {
                // only add closing bracket if there's at least one matching open bracket
                while (top_token(parse_ctx->operator_stack) != FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
                    append_operator(parse_ctx, pop_token(parse_ctx->operator_stack));
                }
                if (top_token(parse_ctx->operator_stack) == FSEARCH_QUERY_TOKEN_BRACKET_OPEN) {
                    pop_token(parse_ctx->operator_stack);
                }
                parse_ctx->last_token = FSEARCH_QUERY_TOKEN_BRACKET_CLOSE;
                num_close_brackets++;
            }
            else {
                goto out;
            }
            break;
        case FSEARCH_QUERY_TOKEN_WORD:
            add_query_to_parse_context(parse_ctx, fsearch_query_node_new(token_value->str, flags), token);
            break;
        case FSEARCH_QUERY_TOKEN_FIELD:
            parse_field(parser, parse_ctx, token_value, false, flags);
            break;
        case FSEARCH_QUERY_TOKEN_FIELD_EMPTY:
            parse_field(parser, parse_ctx, token_value, true, flags);
            break;
        default:
            g_debug("[infix-postfix] ignoring unexpected token: %d", token);
            break;
        }

        if (token_value) {
            g_string_free(g_steal_pointer(&token_value), TRUE);
        }
    }

out:
    while (!g_queue_is_empty(parse_ctx->operator_stack)) {
        append_operator(parse_ctx, pop_token(parse_ctx->operator_stack));
    }

    return;
}

static GNode *
build_query_tree(GList *postfix_query, FsearchQueryFlags flags) {
    if (!postfix_query) {
        return get_empty_node(flags);
    }

    GQueue *query_stack = g_queue_new();

    for (GList *q = postfix_query; q != NULL; q = q->next) {
        FsearchQueryNode *node = q->data;
        if (!node) {
            continue;
        }
        if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
            GNode *op_node = get_operator_node(node->operator);
            GNode *right = g_queue_pop_tail(query_stack);
            if (node->operator!= FSEARCH_TOKEN_OPERATOR_NOT) {
                GNode *left = g_queue_pop_tail(query_stack);
                g_node_append(op_node, left ? left : get_empty_node(flags));
            }
            g_node_append(op_node, right ? right : get_empty_node(flags));
            g_queue_push_tail(query_stack, op_node);
        }
        else {
            g_queue_push_tail(query_stack, g_node_new(node));
        }
    }
    GNode *root = g_queue_pop_tail(query_stack);
    if (!g_queue_is_empty(query_stack)) {
        g_critical("[builder_tree] query stack still has nodes left!!");
    }

    g_queue_free_full(g_steal_pointer(&query_stack), (GDestroyNotify)free_tree);
    return root;
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

    for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(manager); ++i) {
        FsearchFilter *filter = fsearch_filter_manager_get_filter(manager, i);
        if (filter && filter->macro && !fs_str_is_empty(filter->macro)) {
            g_ptr_array_add(macros, fsearch_filter_ref(filter));
        }
    }
    return macros;
}

static GNode *
get_nodes(const char *src, FsearchFilterManager *filters, FsearchQueryFlags flags) {
    assert(src != NULL);

    FsearchQueryParser *parser = fsearch_query_parser_new(src);

    FsearchQueryParseContext *parse_context = calloc(1, sizeof(FsearchQueryParseContext));
    assert(parse_context != NULL);
    parse_context->macro_filters = get_filters_with_macros(filters);
    parse_context->macro_stack = g_queue_new();

    parse_context->last_token = FSEARCH_QUERY_TOKEN_NONE;
    parse_context->operator_stack = g_queue_new();
    parse_expression(parser, parse_context, flags);

    g_print("Postfix representation of query:\n");
    g_print("================================\n");
    for (GList *q = parse_context->suffix_list; q != NULL; q = q->next) {
        FsearchQueryNode *node = q->data;
        if (node->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
            g_print("%s ",
                    node->operator== FSEARCH_TOKEN_OPERATOR_AND  ? "AND"
                    : node->operator== FSEARCH_TOKEN_OPERATOR_OR ? "OR"
                                                                 : "NOT");
        }
        else {
            char *flag_string = query_flags_to_string(node->flags);
            g_print("[%s:'%s':%s] ",
                    node->query_description ? node->query_description->str : "unknown query",
                    node->needle ? node->needle : "",
                    flag_string);
            g_free(flag_string);
        }
    }
    g_print("\n");

    GNode *root = build_query_tree(parse_context->suffix_list, flags);

    g_clear_pointer(&parse_context->macro_stack, g_queue_free);
    g_queue_free_full(g_steal_pointer(&parse_context->operator_stack), (GDestroyNotify)fsearch_query_node_free);
    g_ptr_array_free(g_steal_pointer(&parse_context->macro_filters), TRUE);
    g_list_free(g_steal_pointer(&parse_context->suffix_list));
    g_clear_pointer(&parse_context, free);
    g_clear_pointer(&parser, fsearch_query_parser_free);
    return root;
}

GNode *
fsearch_query_node_tree_new(const char *search_term, FsearchFilterManager *filters, FsearchQueryFlags flags) {
    char *query = g_strdup(search_term);
    char *query_stripped = g_strstrip(query);
    GNode *res = NULL;
    if (flags & QUERY_FLAG_REGEX) {
        // If we're in regex mode we're passing the whole query to the regex engine
        // i.e. there's only one query node
        res = g_node_new(fsearch_query_node_new(query_stripped, flags));
    }
    else {
        res = get_nodes(query_stripped, filters, flags);
    }
    g_clear_pointer(&query, free);
    return res;
}

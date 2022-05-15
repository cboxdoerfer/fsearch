#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-query-node"

#include "fsearch_query_node.h"
#include "fsearch_limits.h"
#include "fsearch_query_matchers.h"
#include "fsearch_size_utils.h"
#include "fsearch_string_utils.h"
#include "fsearch_utf.h"
#include <glib.h>
#include <stdbool.h>
#include <string.h>

static void
node_init_needle(FsearchQueryNode *node, const char *needle) {
    g_assert(node);
    g_assert(needle);
    // node->needle must not be set already
    g_assert_null(node->needle);
    g_assert_null(node->needle_builder);

    node->needle = g_strdup(needle);
    node->needle_len = strlen(needle);

    // set up case folded needle in UTF16 format
    node->needle_builder = calloc(1, sizeof(FsearchUtfBuilder));
    fsearch_utf_builder_init(node->needle_builder, 8 * node->needle_len);
    const bool utf_ready = fsearch_utf_builder_normalize_and_fold_case(node->needle_builder, needle);
    g_assert(utf_ready == true);
}

void
fsearch_query_node_free(FsearchQueryNode *node) {
    g_assert(node);

    fsearch_utf_builder_clear(node->needle_builder);
    if (node->description) {
        g_string_free(g_steal_pointer(&node->description), TRUE);
    }
    g_clear_pointer(&node->search_term_list, g_ptr_array_unref);
    g_clear_pointer(&node->needle_builder, free);
    g_clear_pointer(&node->needle, g_free);

    if (node->regex_match_data_for_threads) {
        g_ptr_array_free(g_steal_pointer(&node->regex_match_data_for_threads), TRUE);
    }
    g_clear_pointer(&node->regex, pcre2_code_free);

    g_clear_pointer(&node, g_free);
}

static char *
get_needle_description_for_comparison_type(int64_t start, int64_t end, FsearchQueryNodeComparison comp_type) {
    switch (comp_type) {
    case FSEARCH_QUERY_NODE_COMPARISON_EQUAL:
        return g_strdup_printf("=%ld", start);
    case FSEARCH_QUERY_NODE_COMPARISON_GREATER_EQ:
        return g_strdup_printf(">=%ld", start);
    case FSEARCH_QUERY_NODE_COMPARISON_GREATER:
        return g_strdup_printf(">%ld", start);
    case FSEARCH_QUERY_NODE_COMPARISON_SMALLER_EQ:
        return g_strdup_printf("<=%ld", start);
    case FSEARCH_QUERY_NODE_COMPARISON_SMALLER:
        return g_strdup_printf("<%ld", start);
    case FSEARCH_QUERY_NODE_COMPARISON_INTERVAL:
        return g_strdup_printf("%ld..%ld", start, end);
    default:
        return g_strdup("invalid");
    }
}

static FsearchQueryNode *
new_numeric_node(int64_t num_start,
                 int64_t num_end,
                 FsearchQueryNodeComparison comp_type,
                 const char *description,
                 FsearchQueryNodeMatchFunc search_func,
                 FsearchQueryNodeMatchFunc highlight_func,
                 FsearchQueryFlags flags) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);

    qnode->needle = get_needle_description_for_comparison_type(num_start, num_end, comp_type);
    qnode->description = g_string_new(description);
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->num_start = num_start;
    qnode->num_end = num_end;
    qnode->comparison_type = comp_type;
    qnode->search_func = search_func;
    qnode->highlight_func = highlight_func;
    qnode->flags = flags;
    return qnode;
}

FsearchQueryNode *
fsearch_query_node_new_date_modified(FsearchQueryFlags flags,
                                     int64_t dm_start,
                                     int64_t dm_end,
                                     FsearchQueryNodeComparison comp_type) {
    return new_numeric_node(dm_start, dm_end, comp_type, "date-modified", fsearch_query_matcher_date_modified, NULL, flags);
}

FsearchQueryNode *
fsearch_query_node_new_size(FsearchQueryFlags flags,
                            int64_t size_start,
                            int64_t size_end,
                            FsearchQueryNodeComparison comp_type) {
    return new_numeric_node(size_start,
                            size_end,
                            comp_type,
                            "size",
                            fsearch_query_matcher_size,
                            fsearch_query_matcher_highlight_size,
                            flags);
}

FsearchQueryNode *
fsearch_query_node_new_childcount(FsearchQueryFlags flags,
                                  int64_t child_count_start,
                                  int64_t child_count_end,
                                  FsearchQueryNodeComparison comp_type) {
    return new_numeric_node(child_count_start,
                            child_count_end,
                            comp_type,
                            "childcount",
                            fsearch_query_matcher_childcount,
                            NULL,
                            flags);
}

FsearchQueryNode *
fsearch_query_node_new_childfilecount(FsearchQueryFlags flags,
                                      int64_t child_file_count_start,
                                      int64_t child_file_count_end,
                                      FsearchQueryNodeComparison comp_type) {
    return new_numeric_node(child_file_count_start,
                            child_file_count_end,
                            comp_type,
                            "childfilecount",
                            fsearch_query_matcher_childfilecount,
                            NULL,
                            flags);
}
FsearchQueryNode *
fsearch_query_node_new_childfoldercount(FsearchQueryFlags flags,
                                        int64_t child_folder_count_start,
                                        int64_t child_folder_count_end,
                                        FsearchQueryNodeComparison comp_type) {
    return new_numeric_node(child_folder_count_start,
                            child_folder_count_end,
                            comp_type,
                            "childfoldercount",
                            fsearch_query_matcher_childfoldercount,
                            NULL,
                            flags);
}

FsearchQueryNode *
fsearch_query_node_new_operator(FsearchQueryNodeOperator operator) {
    g_assert(operator== FSEARCH_QUERY_NODE_OPERATOR_AND || operator== FSEARCH_QUERY_NODE_OPERATOR_OR ||
             operator== FSEARCH_QUERY_NODE_OPERATOR_NOT);
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);
    qnode->description = g_string_new(operator== FSEARCH_QUERY_NODE_OPERATOR_AND
                                          ? "AND"
                                          : (operator== FSEARCH_QUERY_NODE_OPERATOR_OR ? "OR" : "NOT"));
    qnode->type = FSEARCH_QUERY_NODE_TYPE_OPERATOR;
    qnode->operator= operator;
    return qnode;
}

FsearchQueryNode *
fsearch_query_node_new_match_nothing(void) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);

    qnode->description = g_string_new("match_nothing");
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->search_func = fsearch_query_matcher_false;
    qnode->highlight_func = fsearch_query_matcher_highlight_none;
    qnode->flags = 0;
    return qnode;
}

FsearchQueryNode *
fsearch_query_node_new_match_everything(FsearchQueryFlags flags) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);

    qnode->description = g_string_new("match_everything");
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->search_func = fsearch_query_matcher_true;
    qnode->highlight_func = fsearch_query_matcher_highlight_none;
    qnode->flags = flags;
    return qnode;
}

FsearchQueryNode *
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

    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);

    qnode->description = g_string_new("regex");
    qnode->needle = g_strdup(search_term);
    qnode->regex = regex;
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->flags = flags;

    if (pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE) != 0) {
        g_debug("[regex] JIT compilation failed.\n");
        qnode->regex_jit_available = false;
    }
    else {
        qnode->regex_jit_available = true;
    }
    qnode->regex_match_data_for_threads = g_ptr_array_sized_new(FSEARCH_THREAD_LIMIT);
    g_ptr_array_set_free_func(qnode->regex_match_data_for_threads, (GDestroyNotify)pcre2_match_data_free);
    for (int32_t i = 0; i < FSEARCH_THREAD_LIMIT; i++) {
        g_ptr_array_add(qnode->regex_match_data_for_threads, pcre2_match_data_create_from_pattern(qnode->regex, NULL));
    }

    qnode->search_func = fsearch_query_matcher_regex;
    qnode->haystack_func = (FsearchQueryNodeHaystackFunc *)(flags & QUERY_FLAG_SEARCH_IN_PATH
                                                                ? fsearch_query_match_data_get_path_str
                                                                : fsearch_query_match_data_get_name_str);
    qnode->highlight_func = fsearch_query_matcher_highlight_regex;
    return qnode;
}

FsearchQueryNode *
fsearch_query_node_new_parent(const char *search_term, FsearchQueryFlags flags) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->description = g_string_new("parent");
    node_init_needle(qnode, search_term);

    qnode->highlight_func = NULL;
    qnode->flags = flags;
    if (fsearch_string_is_ascii_icase(qnode->needle) || flags & QUERY_FLAG_MATCH_CASE) {
        qnode->search_func = flags & QUERY_FLAG_MATCH_CASE ? fsearch_query_matcher_strcmp
                                                           : fsearch_query_matcher_strcasecmp;
        qnode->haystack_func = (FsearchQueryNodeHaystackFunc *)fsearch_query_match_data_get_parent_path_str;
        qnode->description = g_string_new("parent_ascii");
    }
    else {
        qnode->search_func = fsearch_query_matcher_utf_strcasecmp;
        qnode->haystack_func = (FsearchQueryNodeHaystackFunc *)fsearch_query_match_data_get_utf_parent_path_builder;
        qnode->description = g_string_new("parent_utf");
    }
    return qnode;
}

static int32_t
cmp_strcasecmp(gconstpointer a, gconstpointer b) {
    const char *aa = *(char **)a;
    const char *bb = *(char **)b;

    return strcasecmp(aa, bb);
}

static int32_t
cmp_strcmp(gconstpointer a, gconstpointer b) {
    const char *aa = *(char **)a;
    const char *bb = *(char **)b;

    return strcmp(aa, bb);
}

FsearchQueryNode *
fsearch_query_node_new_extension(const char *search_term, FsearchQueryFlags flags) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);
    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->description = g_string_new("ext");
    qnode->search_func = fsearch_query_matcher_extension;
    qnode->highlight_func = fsearch_query_matcher_highlight_extension;
    qnode->flags = flags | QUERY_FLAG_FILES_ONLY;
    qnode->search_term_list = g_ptr_array_new_full(16, g_free);
    if (!search_term) {
        // Show all files with no extension
        qnode->needle = g_strdup("");
        g_ptr_array_add(qnode->search_term_list, g_strdup(""));
    }
    else {
        qnode->needle = g_strdup(search_term);
        gchar **search_terms = g_strsplit(search_term, ";", -1);
        const uint32_t num_search_terms = g_strv_length(search_terms);
        for (uint32_t i = 0; i < num_search_terms; ++i) {
            g_ptr_array_add(qnode->search_term_list, g_strdup(search_terms[i]));
        }
        g_ptr_array_sort(qnode->search_term_list, (qnode->flags & QUERY_FLAG_MATCH_CASE) ? cmp_strcmp : cmp_strcasecmp);
        g_clear_pointer(&search_terms, g_strfreev);
    }
    return qnode;
}

FsearchQueryNode *
fsearch_query_node_new_wildcard(const char *search_term, FsearchQueryFlags flags) {
    // We convert the wildcard pattern to a regex pattern
    // The regex engine is not only faster than fnmatch, but it also handles utf8 strings better
    // and it provides matching information, which are useful for the highlighting engine
    g_autofree char *regex_search_term = fsearch_string_convert_wildcard_to_regex_expression(search_term);
    if (!regex_search_term) {
        return NULL;
    }
    return fsearch_query_node_new_regex(regex_search_term, flags);
}

static FsearchQueryNode *
query_node_new_simple(const char *search_term, FsearchQueryFlags flags) {
    FsearchQueryNode *qnode = calloc(1, sizeof(FsearchQueryNode));
    g_assert(qnode);

    qnode->type = FSEARCH_QUERY_NODE_TYPE_QUERY;
    qnode->flags = flags;
    node_init_needle(qnode, search_term);

    if (fsearch_string_is_ascii_icase(search_term) || flags & QUERY_FLAG_MATCH_CASE) {
        if (flags & QUERY_FLAG_EXACT_MATCH) {
            qnode->search_func = flags & QUERY_FLAG_MATCH_CASE ? fsearch_query_matcher_strcmp
                                                               : fsearch_query_matcher_strcasecmp;
        }
        else {
            qnode->search_func = flags & QUERY_FLAG_MATCH_CASE ? fsearch_query_matcher_strstr
                                                               : fsearch_query_matcher_strcasestr;
        }
        qnode->haystack_func = (FsearchQueryNodeHaystackFunc *)(flags & QUERY_FLAG_SEARCH_IN_PATH
                                                                    ? fsearch_query_match_data_get_path_str
                                                                    : fsearch_query_match_data_get_name_str);
        qnode->highlight_func = fsearch_query_matcher_highlight_ascii;
        qnode->description = g_string_new("ascii_icase");
    }
    else {
        qnode->search_func = flags & QUERY_FLAG_EXACT_MATCH ? fsearch_query_matcher_utf_strcasecmp
                                                            : fsearch_query_matcher_utf_strcasestr;
        qnode->haystack_func = (FsearchQueryNodeHaystackFunc *)(flags & QUERY_FLAG_SEARCH_IN_PATH
                                                                    ? fsearch_query_match_data_get_utf_path_builder
                                                                    : fsearch_query_match_data_get_utf_name_builder);
        qnode->highlight_func = NULL;
        qnode->description = g_string_new("utf_icase");
    }
    return qnode;
}

static bool
is_wildcard_expression(const char *expression) {
    if (strchr(expression, '*') || strchr(expression, '?')) {
        return true;
    }
    return false;
}

FsearchQueryNode *
fsearch_query_node_new_contenttype(const char *search_term, FsearchQueryFlags flags) {
    FsearchQueryNode *res = NULL;
    if (flags & QUERY_FLAG_REGEX) {
        res = fsearch_query_node_new_regex(search_term, flags);
    }
    else if (is_wildcard_expression(search_term)) {
        res = fsearch_query_node_new_wildcard(search_term, flags);
    }
    else {
        res = query_node_new_simple(search_term, flags);
    }

    if (res) {
        g_string_prepend(res->description, "contenttype_");
        res->haystack_func = (FsearchQueryNodeHaystackFunc *)fsearch_query_match_data_get_content_type_str;
        res->highlight_func = fsearch_query_matcher_highlight_none;
        res->wants_single_threaded_search = true;
    }

    return res;
}

FsearchQueryNode *
fsearch_query_node_new(const char *search_term, FsearchQueryFlags flags) {
    bool has_separator = strchr(search_term, G_DIR_SEPARATOR) ? 1 : 0;

    bool triggers_auto_match_case = !(flags & QUERY_FLAG_MATCH_CASE) && flags & QUERY_FLAG_AUTO_MATCH_CASE
                                 && fsearch_string_utf8_has_upper(search_term);
    bool triggers_auto_match_path = !(flags & QUERY_FLAG_SEARCH_IN_PATH) && flags & QUERY_FLAG_AUTO_SEARCH_IN_PATH
                                 && has_separator;

    if (triggers_auto_match_path) {
        flags |= QUERY_FLAG_SEARCH_IN_PATH;
    }
    if (triggers_auto_match_case) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }

    FsearchQueryNode *res = NULL;
    if (flags & QUERY_FLAG_REGEX) {
        res = fsearch_query_node_new_regex(search_term, flags);
    }
    else if (is_wildcard_expression(search_term)) {
        res = fsearch_query_node_new_wildcard(search_term, flags);
    }
    else {
        res = query_node_new_simple(search_term, flags);
    }
    if (res) {
        res->triggers_auto_match_case = triggers_auto_match_case;
        res->triggers_auto_match_path = triggers_auto_match_path;
    }
    return res;
}

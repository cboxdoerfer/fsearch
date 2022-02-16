#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-search-token"

#include "fsearch_token.h"
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

static uint32_t
fsearch_search_func_size(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    FsearchDatabaseEntry *entry = fsearch_query_match_context_get_entry(matcher);
    if (entry) {
        off_t size = db_entry_get_size(entry);
        switch (token->size_comparison_type) {
        case FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL:
            return size == token->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_GREATER:
            return size > token->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER:
            return size < token->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_GREATER_EQ:
            return size >= token->size;
        case FSEARCH_TOKEN_SIZE_COMPARISON_SMALLER_EQ:
            return size <= token->size;
        }
    }
    return 0;
}

static uint32_t
fsearch_search_func_regex(const char *haystack, FsearchToken *token) {
    size_t haystack_len = strlen(haystack);
    return pcre_exec(token->regex, token->regex_study, haystack, haystack_len, 0, 0, token->ovector, OVECCOUNT) >= 0 ? 1
                                                                                                                     : 0;
}

static uint32_t
fsearch_search_func_regex_path(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_path_str(matcher);
    return fsearch_search_func_regex(haystack, token);
}

static uint32_t
fsearch_search_func_regex_name(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_name_str(matcher);
    return fsearch_search_func_regex(haystack, token);
}

static uint32_t
fsearch_search_func_wildcard(const char *haystack, FsearchToken *token) {
    return !fnmatch(token->search_term, haystack, token->wildcard_flags) ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard_path(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_path_str(matcher);
    return fsearch_search_func_wildcard(haystack, token);
}

static uint32_t
fsearch_search_func_wildcard_name(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    const char *haystack = fsearch_query_match_context_get_name_str(matcher);
    return fsearch_search_func_wildcard(haystack, token);
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
                                    FsearchUtfConversionBuffer *needle_buffer) {
    if (G_LIKELY(haystack_buffer->string_is_folded_and_normalized)) {
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
        return strcasestr(haystack_buffer->string, needle_buffer->string) ? 1 : 0;
    }
}

static uint32_t
fsearch_search_func_normal_icase_u8_path(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    FsearchUtfConversionBuffer *haystack_buffer = fsearch_query_match_context_get_utf_path_buffer(matcher);
    FsearchUtfConversionBuffer *needle_buffer = token->needle_buffer;
    return fsearch_search_func_normal_icase_u8(haystack_buffer, needle_buffer);
}

static uint32_t
fsearch_search_func_normal_icase_u8_name(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    FsearchUtfConversionBuffer *haystack_buffer = fsearch_query_match_context_get_utf_name_buffer(matcher);
    FsearchUtfConversionBuffer *needle_buffer = token->needle_buffer;
    return fsearch_search_func_normal_icase_u8(haystack_buffer, needle_buffer);
}

static uint32_t
fsearch_search_func_normal_icase_path(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    return strcasestr(fsearch_query_match_context_get_path_str(matcher), token->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_icase_name(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    return strcasestr(fsearch_query_match_context_get_name_str(matcher), token->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_path(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    return strstr(fsearch_query_match_context_get_path_str(matcher), token->search_term) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_name(FsearchToken *token, FsearchQueryMatchContext *matcher) {
    return strstr(fsearch_query_match_context_get_name_str(matcher), token->search_term) ? 1 : 0;
}

static void
fsearch_token_free(void *data) {
    FsearchToken *token = data;
    assert(token != NULL);

    fsearch_utf_conversion_buffer_clear(token->needle_buffer);
    g_clear_pointer(&token->needle_buffer, free);
    g_clear_pointer(&token->case_map, ucasemap_close);
    g_clear_pointer(&token->search_term, g_free);
    g_clear_pointer(&token->regex_study, pcre_free_study);
    g_clear_pointer(&token->regex, pcre_free);
    g_clear_pointer(&token, g_free);
}

void
fsearch_tokens_free(FsearchToken **tokens) {
    if (!tokens) {
        return;
    }
    for (uint32_t i = 0; tokens[i] != NULL; ++i) {
        g_clear_pointer(&tokens[i], fsearch_token_free);
    }
    g_clear_pointer(&tokens, free);
}

static FsearchToken *
fsearch_token_new_size(off_t size, FsearchTokenSizeComparisonType comp_type) {
    FsearchToken *new = calloc(1, sizeof(FsearchToken));
    assert(new != NULL);

    new->type = FSEARCH_TOKEN_TYPE_FUNC_SIZE;
    new->size = size;
    new->size_comparison_type = comp_type;
    new->search_func = fsearch_search_func_size;
    return new;
}

static FsearchToken *
fsearch_token_new(const char *search_term, FsearchQueryFlags flags) {
    FsearchToken *new = calloc(1, sizeof(FsearchToken));
    assert(new != NULL);

    new->type = FSEARCH_TOKEN_TYPE_NORMAL;
    new->search_term = g_strdup(search_term);
    new->search_term_len = strlen(search_term);
    new->has_separator = strchr(search_term, G_DIR_SEPARATOR) ? 1 : 0;

    if ((flags & QUERY_FLAG_AUTO_MATCH_CASE) && fs_str_utf8_has_upper(search_term)) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }

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
            new->is_utf = 1;
            new->search_func = search_in_path ? fsearch_search_func_normal_icase_u8_path
                                              : fsearch_search_func_normal_icase_u8_name;
        }
    }
    return new;
}

static void
parse_size(GString *string, GPtrArray *token_list, FsearchTokenSizeComparisonType comp_type) {
    char *size_suffix = NULL;
    off_t size = strtoll(string->str, &size_suffix, 10);
    if (size_suffix == string->str) {
        g_print("Invalid size format: %s\n", string->str);
        return;
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
            g_print("Invalid size suffix: %s\n", size_suffix);
            return;
        }
        size_suffix++;

        switch (*size_suffix) {
        case '\0':
        case 'b':
        case 'B':
            break;
        default:
            g_print("Invalid size suffix: %s\n", size_suffix);
            return;
        }
    }
    g_ptr_array_add(token_list, fsearch_token_new_size(size, comp_type));
}

static void
parse_field_size(FsearchQueryParser *parser, FsearchQueryFlags flags, GPtrArray *token_list) {
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    FsearchTokenSizeComparisonType comp_type = FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL;
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
        parse_size(token_value, token_list, comp_type);
        break;
    default:
        g_print("size field: invalid format\n");
        goto out;
    }

    if (comp_type != FSEARCH_TOKEN_SIZE_COMPARISON_EQUAL) {
        GString *next_token_value = NULL;
        FsearchQueryToken next_token = fsearch_query_parser_get_next_token(parser, &next_token_value);
        if (next_token == FSEARCH_QUERY_TOKEN_WORD) {
            parse_size(next_token_value, token_list, comp_type);
        }
        if (next_token_value) {
            g_string_free(g_steal_pointer(&next_token_value), TRUE);
        }
    }
out:
    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
}

static void
parse_field_regex(FsearchQueryParser *parser, FsearchQueryFlags flags, GPtrArray *token_list) {
    GString *token_value = NULL;
    FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
    if (token == FSEARCH_QUERY_TOKEN_WORD) {
        flags |= QUERY_FLAG_REGEX;
        g_ptr_array_add(token_list, fsearch_token_new(token_value->str, flags));
    }
    else {
        g_print("regex field: invalid format\n");
    }

    if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
}

typedef void(FsearchTokenFieldParser)(FsearchQueryParser *, FsearchQueryFlags, GPtrArray *);

typedef struct FsearchTokenField {
    const char *name;
    FsearchTokenFieldParser *parser;
} FsearchTokenField;

FsearchTokenField supported_fields[] = {
    {"size", parse_field_size},
    {"regex", parse_field_regex},
};

static GPtrArray *
get_tokens(const char *src, FsearchQueryFlags flags) {
    assert(src != NULL);

    GPtrArray *token_list = g_ptr_array_new();
    FsearchQueryParser *parser = fsearch_query_parser_new(src);

    FsearchQueryToken prev_token = FSEARCH_QUERY_TOKEN_NONE;
    while (true) {
        GString *token_value = NULL;
        GString *next_token_value = NULL;
        FsearchQueryToken token = fsearch_query_parser_get_next_token(parser, &token_value);
        FsearchQueryToken next_token = fsearch_query_parser_peek_next_token(parser, &next_token_value);
        switch (token) {
        case FSEARCH_QUERY_TOKEN_EOS:
            goto out;
        case FSEARCH_QUERY_TOKEN_WORD:
            g_ptr_array_add(token_list, fsearch_token_new(token_value->str, flags));
            break;
        case FSEARCH_QUERY_TOKEN_FIELD:
            g_print("field detected: [%s]\n", token_value->str);
            for (uint32_t i = 0; i < G_N_ELEMENTS(supported_fields); ++i) {
                if (!strcmp(supported_fields[i].name, token_value->str)) {
                    supported_fields[i].parser(parser, flags, token_list);
                }
            }
            break;
        default:
            g_print("Unhandled token: %d\n", token);
        }
        if (token_value) {
            g_string_free(g_steal_pointer(&token_value), TRUE);
        }
        if (next_token_value) {
            g_string_free(g_steal_pointer(&next_token_value), TRUE);
        }

        prev_token = token;
    }

out:
    g_clear_pointer(&parser, fsearch_query_parser_free);
    g_ptr_array_add(token_list, NULL);
    return token_list;
}

FsearchToken **
fsearch_tokens_new(const char *query, FsearchQueryFlags flags, uint32_t *num_token) {
    // check if regex characters are present
    const bool is_reg = fs_str_is_regex(query);
    if (is_reg && (flags & QUERY_FLAG_REGEX)) {
        FsearchToken **token = calloc(2, sizeof(FsearchToken *));
        assert(token != NULL);
        token[0] = fsearch_token_new(query, flags);
        token[1] = NULL;
        if (num_token) {
            *num_token = 1;
        }
        return token;
    }

    GPtrArray *tokens = get_tokens(query, flags);
    if (tokens) {
        if (num_token) {
            *num_token = tokens->len - 1;
            g_print("num_token: %d\n", *num_token);
        }
        FsearchToken **token = (FsearchToken **)g_ptr_array_free(tokens, FALSE);
        return token;
    }
    else {
        *num_token = 0;
    }
    return NULL;
}

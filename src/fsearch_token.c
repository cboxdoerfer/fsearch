#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-search-token"

#include "fsearch_token.h"
#include "fsearch_string_utils.h"
#include <assert.h>
#include <fnmatch.h>
#include <glib.h>
#include <locale.h>
#include <string.h>
#include <unicode/uloc.h>

static uint32_t
fsearch_search_func_regex(const char *haystack,
                          const char *needle,
                          void *token,
                          char *haystack_buffer,
                          size_t haystack_buffer_len) {
    FsearchToken *t = token;
    size_t haystack_len = strlen(haystack);
    return pcre_exec(t->regex, t->regex_study, haystack, haystack_len, 0, 0, t->ovector, OVECCOUNT) >= 0 ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard_icase(const char *haystack,
                                   const char *needle,
                                   void *token,
                                   char *haystack_buffer,
                                   size_t haystack_buffer_len) {
    return !fnmatch(needle, haystack, FNM_CASEFOLD) ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard(const char *haystack,
                             const char *needle,
                             void *token,
                             char *haystack_buffer,
                             size_t haystack_buffer_len) {
    return !fnmatch(needle, haystack, 0) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_icase_u8(const char *haystack,
                                    const char *needle,
                                    void *token,
                                    char *haystack_buffer,
                                    size_t haystack_buffer_len) {
    FsearchToken *t = token;

    UErrorCode status = U_ZERO_ERROR;
    ucasemap_utf8FoldCase(t->case_map, haystack_buffer, (int32_t)haystack_buffer_len, haystack, -1, &status);
    if (G_LIKELY(U_SUCCESS(status))) {
        return strstr(haystack_buffer, t->needle_down) ? 1 : 0;
    }
    else {
        // failed to fold case, fall back to fast but not accurate ascii search
        g_warning("[utf8_search] failed to lower case: %s", haystack);
        return strcasestr(haystack, needle) ? 1 : 0;
    }
}

static uint32_t
fsearch_search_func_normal_icase(const char *haystack,
                                 const char *needle,
                                 void *token,
                                 char *haystack_buffer,
                                 size_t haystack_buffer_len) {
    return strcasestr(haystack, needle) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal(const char *haystack,
                           const char *needle,
                           void *token,
                           char *haystack_buffer,
                           size_t haystack_buffer_len) {
    return strstr(haystack, needle) ? 1 : 0;
}

static void
fsearch_token_free(void *data) {
    FsearchToken *token = data;
    assert(token != NULL);

    g_clear_pointer(&token->case_map, ucasemap_close);
    g_clear_pointer(&token->needle_down, g_free);
    g_clear_pointer(&token->text, g_free);
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
fsearch_token_new(const char *text, FsearchQueryFlags flags) {
    FsearchToken *new = calloc(1, sizeof(FsearchToken));
    assert(new != NULL);

    const char *current_locale = setlocale(LC_CTYPE, NULL);

    uint32_t fold_case_options = U_FOLD_CASE_DEFAULT;
    if (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2)) {
        // Use special case mapping for Turkic languages
        fold_case_options = U_FOLD_CASE_EXCLUDE_SPECIAL_I;
    }
    UErrorCode status = U_ZERO_ERROR;
    new->case_map = ucasemap_open(current_locale, fold_case_options, &status);
    assert(U_SUCCESS(status));

    new->text_len = strlen(text);

    // make sure the buffer is large enough, because case folding can result in a larger string
    new->needle_down = calloc(8 * new->text_len, sizeof(char));
    new->needle_down_len = ucasemap_utf8FoldCase(new->case_map, new->needle_down, 8 * new->text_len, text, -1, &status);
    assert(U_SUCCESS(status));

    new->has_separator = strchr(text, G_DIR_SEPARATOR) ? 1 : 0;

    if ((flags & QUERY_FLAG_AUTO_MATCH_CASE) && fs_str_utf8_has_upper(text)) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }

    char *normalized = g_utf8_normalize(text, -1, G_NORMALIZE_DEFAULT);
    new->text = (flags & QUERY_FLAG_MATCH_CASE) ? g_strdup(text) : g_utf8_strdown(normalized, -1);

    g_clear_pointer(&normalized, g_free);

    if (flags & QUERY_FLAG_REGEX) {
        const char *error;
        int erroffset;
        new->regex = pcre_compile(text, flags & QUERY_FLAG_MATCH_CASE ? 0 : PCRE_CASELESS, &error, &erroffset, NULL);
        new->regex_study = pcre_study(new->regex, PCRE_STUDY_JIT_COMPILE, &error);
        new->search_func = fsearch_search_func_regex;
    }
    else if (strchr(text, '*') || strchr(text, '?')) {
        new->search_func =
            flags &QUERY_FLAG_MATCH_CASE ? fsearch_search_func_wildcard : fsearch_search_func_wildcard_icase;
    }
    else {
        if (flags & QUERY_FLAG_MATCH_CASE) {
            new->search_func = fsearch_search_func_normal;
        }
        else {
            new->search_func =
                !fs_str_case_is_ascii(text) ? fsearch_search_func_normal_icase_u8 : fsearch_search_func_normal_icase;
        }
    }
    return new;
}

FsearchToken **
fsearch_tokens_new(const char *query, FsearchQueryFlags flags) {
    // check if regex characters are present
    const bool is_reg = fs_str_is_regex(query);
    if (is_reg && (flags & QUERY_FLAG_REGEX)) {
        FsearchToken **token = calloc(2, sizeof(FsearchToken *));
        assert(token != NULL);
        token[0] = fsearch_token_new(query, flags);
        token[1] = NULL;
        return token;
    }

    // whitespace is regarded as AND so split query there in multiple token
    char **query_split = fs_str_split(query);
    assert(query_split != NULL);

    uint32_t tmp_token_len = g_strv_length(query_split);
    FsearchToken **token = calloc(tmp_token_len + 1, sizeof(FsearchToken *));
    assert(token != NULL);
    for (uint32_t i = 0; i < tmp_token_len; i++) {
        // g_debug("[search] token %d: %s", i, query_split[i]);
        token[i] = fsearch_token_new(query_split[i], flags);
    }

    g_clear_pointer(&query_split, g_strfreev);

    return token;
}

#define _GNU_SOURCE

#include "token.h"
#include "debug.h"
#include "string_utils.h"
#include <assert.h>
#include <fnmatch.h>
#include <glib.h>
#include <string.h>

static uint32_t
fsearch_search_func_regex(const char *haystack, const char *needle, void *data) {
    FsearchToken *t = data;
    size_t haystack_len = strlen(haystack);
    return pcre_exec(t->regex, t->regex_study, haystack, haystack_len, 0, 0, t->ovector, OVECCOUNT) >= 0 ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard_icase(const char *haystack, const char *needle, void *data) {
    return !fnmatch(needle, haystack, FNM_CASEFOLD) ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard(const char *haystack, const char *needle, void *data) {
    return !fnmatch(needle, haystack, 0) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_icase_u8(const char *haystack, const char *needle, void *data) {
    // TODO: make this faster
    char *haystack_normalized = g_utf8_normalize(haystack, -1, G_NORMALIZE_DEFAULT);
    if (haystack_normalized == NULL) {
        trace("[search] file has invalid encoding: %s\n", haystack);
        return strcasestr(haystack, needle) ? 1 : 0;
    }
    char *haystack_down = g_utf8_strdown(haystack_normalized, -1);
    uint32_t res = strstr(haystack_down, needle) ? 1 : 0;
    g_free(haystack_down);
    g_free(haystack_normalized);
    return res;
}

static uint32_t
fsearch_search_func_normal_icase(const char *haystack, const char *needle, void *data) {
    return strcasestr(haystack, needle) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal(const char *haystack, const char *needle, void *data) {
    return strstr(haystack, needle) ? 1 : 0;
}

static void
fsearch_token_free(void *data) {
    FsearchToken *token = data;
    assert(token != NULL);

    if (token->text != NULL) {
        g_free(token->text);
        token->text = NULL;
    }
    if (token->regex_study != NULL) {
        pcre_free_study(token->regex_study);
        token->regex_study = NULL;
    }
    if (token->regex != NULL) {
        pcre_free(token->regex);
        token->regex = NULL;
    }
    g_free(token);
    token = NULL;
}

void
fsearch_tokens_free(FsearchToken **tokens) {
    if (!tokens) {
        return;
    }
    for (uint32_t i = 0; tokens[i] != NULL; ++i) {
        fsearch_token_free(tokens[i]);
        tokens[i] = NULL;
    }
    free(tokens);
    tokens = NULL;
}

static FsearchToken *
fsearch_token_new(const char *text, bool match_case, bool auto_match_case, bool is_regex) {
    FsearchToken *new = calloc(1, sizeof(FsearchToken));
    assert(new != NULL);

    new->text_len = strlen(text);
    new->has_separator = strchr(text, '/') ? 1 : 0;

    if (auto_match_case && fs_str_utf8_has_upper(text)) {
        match_case = true;
    }

    char *normalized = g_utf8_normalize(text, -1, G_NORMALIZE_DEFAULT);
    new->text = match_case ? g_strdup(text) : g_utf8_strdown(normalized, -1);
    g_free(normalized);
    normalized = NULL;

    if (is_regex) {
        const char *error;
        int erroffset;
        new->regex = pcre_compile(text, match_case ? 0 : PCRE_CASELESS, &error, &erroffset, NULL);
        new->regex_study = pcre_study(new->regex, PCRE_STUDY_JIT_COMPILE, &error);
        new->search_func = fsearch_search_func_regex;
    }
    else if (strchr(text, '*') || strchr(text, '?')) {
        new->search_func = match_case ? fsearch_search_func_wildcard : fsearch_search_func_wildcard_icase;
    }
    else {
        if (match_case) {
            new->search_func = fsearch_search_func_normal;
        }
        else {
            new->search_func =
                fs_str_is_utf8(text) ? fsearch_search_func_normal_icase_u8 : fsearch_search_func_normal_icase;
        }
    }
    return new;
}

FsearchToken **
fsearch_tokens_new(const char *query, bool match_case, bool enable_regex, bool auto_match_case) {
    // check if regex characters are present
    const bool is_reg = fs_str_is_regex(query);
    if (is_reg && enable_regex) {
        FsearchToken **token = calloc(2, sizeof(FsearchToken *));
        token[0] = fsearch_token_new(query, match_case, auto_match_case, true);
        token[1] = NULL;
        return token;
    }

    // whitespace is regarded as AND so split query there in multiple token
    char **query_split = fs_str_split(query);
    assert(query_split != NULL);

    uint32_t tmp_token_len = g_strv_length(query_split);
    FsearchToken **token = calloc(tmp_token_len + 1, sizeof(FsearchToken *));
    for (uint32_t i = 0; i < tmp_token_len; i++) {
        // trace("[search] token %d: %s\n", i, query_split[i]);
        token[i] = fsearch_token_new(query_split[i], match_case, auto_match_case, false);
    }

    g_strfreev(query_split);
    query_split = NULL;

    return token;
}


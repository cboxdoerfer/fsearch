#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-search-token"

#include "fsearch_token.h"
#include "fsearch_string_utils.h"
#include "fsearch_utf.h"
#include <assert.h>
#include <fnmatch.h>
#include <glib.h>
#include <locale.h>
#include <string.h>

static uint32_t
fsearch_search_func_regex(const char *haystack,
                          const char *needle,
                          void *token,
                          char *haystack_buffer,
                          size_t haystack_buffer_len,
                          FsearchUtfConversionBuffer *buffer) {
    FsearchToken *t = token;
    size_t haystack_len = strlen(haystack);
    return pcre_exec(t->regex, t->regex_study, haystack, haystack_len, 0, 0, t->ovector, OVECCOUNT) >= 0 ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard_icase(const char *haystack,
                                   const char *needle,
                                   void *token,
                                   char *haystack_buffer,
                                   size_t haystack_buffer_len,
                                   FsearchUtfConversionBuffer *buffer) {
    return !fnmatch(needle, haystack, FNM_CASEFOLD) ? 1 : 0;
}

static uint32_t
fsearch_search_func_wildcard(const char *haystack,
                             const char *needle,
                             void *token,
                             char *haystack_buffer,
                             size_t haystack_buffer_len,
                             FsearchUtfConversionBuffer *buffer) {
    return !fnmatch(needle, haystack, 0) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal_icase_u8_fast(const char *haystack,
                                         const char *needle,
                                         void *token,
                                         char *haystack_buffer,
                                         size_t haystack_buffer_len,
                                         FsearchUtfConversionBuffer *buffer) {
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
fsearch_search_func_normal_icase_u8(const char *haystack,
                                    const char *needle,
                                    void *token,
                                    char *haystack_buffer,
                                    size_t haystack_buffer_len,
                                    FsearchUtfConversionBuffer *buffer) {
    FsearchToken *t = token;
    if (G_LIKELY(fsearch_utf_normalize_and_fold_case(t->normalizer, t->case_map, buffer, haystack))) {
        return u_strFindFirst(buffer->string_normalized_folded,
                              buffer->string_normalized_folded_len,
                              t->needle_buffer->string_normalized_folded,
                              t->needle_buffer->string_normalized_folded_len)
                 ? 1
                 : 0;
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
                                 size_t haystack_buffer_len,
                                 FsearchUtfConversionBuffer *buffer) {
    return strcasestr(haystack, needle) ? 1 : 0;
}

static uint32_t
fsearch_search_func_normal(const char *haystack,
                           const char *needle,
                           void *token,
                           char *haystack_buffer,
                           size_t haystack_buffer_len,
                           FsearchUtfConversionBuffer *buffer) {
    return strstr(haystack, needle) ? 1 : 0;
}

static void
fsearch_token_free(void *data) {
    FsearchToken *token = data;
    assert(token != NULL);

    fsearch_utf_conversion_buffer_clear(token->needle_buffer);
    g_clear_pointer(&token->needle_buffer, free);
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

    new->text = g_strdup(text);
    new->text_len = strlen(text);
    new->has_separator = strchr(text, G_DIR_SEPARATOR) ? 1 : 0;

    if ((flags & QUERY_FLAG_AUTO_MATCH_CASE) && fs_str_utf8_has_upper(text)) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }

    new->fold_options = U_FOLD_CASE_DEFAULT;
    if (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2)) {
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
    fsearch_utf_conversion_buffer_init(new->needle_buffer, 8 * new->text_len);
    const bool utf_ready =
        fsearch_utf_normalize_and_fold_case(new->normalizer, new->case_map, new->needle_buffer, text);
    assert(utf_ready == true);

    // set up case folded needle in UTF8 format
    new->needle_down = g_strdup(new->needle_buffer->string_utf8_folded);
    new->needle_down_len = new->needle_buffer->string_utf8_folded_len;

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
        else if (fs_str_case_is_ascii(text)) {
            new->search_func = fsearch_search_func_normal_icase;
        }
        else {
            new->search_func = fsearch_search_func_normal_icase_u8;
            // new->search_func = fsearch_search_func_normal_icase_u8_fast;
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

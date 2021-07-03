
#include "fsearch_highlight_token.h"
#include "fsearch_query_flags.h"
#include "fsearch_string_utils.h"

#define _GNU_SOURCE

#include <assert.h>
#include <fnmatch.h>
#include <glib.h>
#include <pango/pango.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct FsearchHighlightToken {
    GRegex *regex;

    bool is_supported_glob;
    bool start_with_asterisk;
    bool end_with_asterisk;

    uint32_t hl_start;
    uint32_t hl_end;

    char *text;
    size_t query_len;
} FsearchHighlightToken;

static bool
fsearch_query_highlight_match_glob(FsearchHighlightToken *token, const char *text, bool match_case) {
    if (!token->end_with_asterisk && !token->start_with_asterisk) {
        return false;
    }

    if (fnmatch(token->text, text, match_case ? 0 : FNM_CASEFOLD)) {
        return false;
    }

    if (token->end_with_asterisk) {
        token->hl_start = 0;
        token->hl_end = token->query_len - 1;
    }
    else if (token->start_with_asterisk) {
        size_t text_len = strlen(text);
        // +1 because query starts with *
        token->hl_start = text_len - token->query_len + 1;
        token->hl_end = text_len;
    }
    return true;
}

PangoAttrList *
fsearch_highlight_tokens_match(GList *tokens, FsearchQueryFlags flags, const char *input) {
    PangoAttrList *attr = pango_attr_list_new();
    GRegexMatchFlags match_flags = G_REGEX_MATCH_PARTIAL;

    GList *l = tokens;
    while (l) {
        FsearchHighlightToken *token = l->data;
        if (!token || !token->regex) {
            break;
        }
        l = l->next;

        if (token->is_supported_glob
            && fsearch_query_highlight_match_glob(token, input, flags & QUERY_FLAG_MATCH_CASE)) {
            PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            pa->start_index = token->hl_start;
            pa->end_index = token->hl_end;
            pango_attr_list_insert(attr, pa);
            continue;
        }

        GMatchInfo *match_info = NULL;
        g_regex_match(token->regex, input, match_flags, &match_info);
        while (g_match_info_matches(match_info)) {
            int count = g_match_info_get_match_count(match_info);
            for (int index = (count > 1) ? 1 : 0; index < count; index++) {
                int start, end;
                g_match_info_fetch_pos(match_info, index, &start, &end);
                PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                pa->start_index = start;
                pa->end_index = end;
                pango_attr_list_insert(attr, pa);
            }
            g_match_info_next(match_info, NULL);
        }
        g_clear_pointer(&match_info, g_match_info_free);
    }
    return attr;
}

static void
fsearch_highlight_token_init(FsearchHighlightToken *token, char *text) {
    token->text = g_strdup(text);
    token->query_len = strlen(text);

    if (token->query_len < 1) {
        return;
    }

    uint32_t n_asterisk = 0;
    const char *p = text;
    do {
        if (*p == '*') {
            n_asterisk++;
        }
    } while (*(p++));

    if (n_asterisk != 1) {
        return;
    }

    token->end_with_asterisk = text[token->query_len - 1] == '*' ? true : false;
    token->start_with_asterisk = text[0] == '*' ? true : false;
    token->is_supported_glob = true;
}

static FsearchHighlightToken *
fsearch_highlight_token_new() {
    return calloc(1, sizeof(FsearchHighlightToken));
}

GList *
fsearch_highlight_tokens_new(const char *text, FsearchQueryFlags flags) {
    if (!text) {
        return NULL;
    }

    GList *tokens = NULL;
    if (fs_str_is_regex(text) && (flags & QUERY_FLAG_REGEX)) {
        FsearchHighlightToken *token = fsearch_highlight_token_new();

        bool has_uppercase = fs_str_utf8_has_upper(text);
        if (has_uppercase && (flags & QUERY_FLAG_AUTO_MATCH_CASE)) {
            flags |= QUERY_FLAG_MATCH_CASE;
        }
        token->regex = g_regex_new(text, !(flags & QUERY_FLAG_MATCH_CASE) ? G_REGEX_CASELESS : 0, 0, NULL);
        token->text = g_strdup(text);
        token->query_len = strlen(text);
        tokens = g_list_append(tokens, token);
    }
    else {
        gchar *tmp = g_strdup(text);
        // remove leading/trailing whitespace
        g_strstrip(tmp);

        // whitespace is regarded as AND so split query there in multiple
        // queries
        gchar **queries = fs_str_split(tmp);
        g_clear_pointer(&tmp, g_free);

        const guint n_queries = g_strv_length(queries);

        for (int i = 0; i < n_queries; i++) {
            FsearchHighlightToken *token = fsearch_highlight_token_new();
            assert(token != NULL);

            gchar *query_escaped = g_regex_escape_string(queries[i], -1);

            bool has_uppercase = fs_str_utf8_has_upper(query_escaped);
            FsearchQueryFlags flags_token = flags;
            if (has_uppercase && (flags & QUERY_FLAG_AUTO_MATCH_CASE)) {
                flags_token |= QUERY_FLAG_MATCH_CASE;
            }
            token->regex =
                g_regex_new(query_escaped, !(flags_token & QUERY_FLAG_MATCH_CASE) ? G_REGEX_CASELESS : 0, 0, NULL);
            g_clear_pointer(&query_escaped, g_free);

            fsearch_highlight_token_init(token, queries[i]);

            tokens = g_list_append(tokens, token);
        }

        g_clear_pointer(&queries, g_strfreev);
    }

    return tokens;
}

static void
fsearch_highlight_token_free(FsearchHighlightToken *token) {
    if (!token) {
        return;
    }
    g_clear_pointer(&token->regex, g_regex_unref);
    g_clear_pointer(&token->text, free);
    g_clear_pointer(&token, free);
}

void
fsearch_highlight_tokens_free(GList *tokens) {
    if (tokens) {
        g_list_free_full(g_steal_pointer(&tokens), (GDestroyNotify)fsearch_highlight_token_free);
    }
}

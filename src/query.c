/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fnmatch.h>
#include "query.h"
#include "utf8.h"
#include "string_utils.h"

FsearchQuery *
fsearch_query_new (const char *text,
                   FsearchDatabase *db,
                   FsearchFilter filter,
                   void (*callback)(void *),
                   void *callback_data,
                   void (*callback_cancelled)(void *),
                   void *callback_cancelled_data,
                   uint32_t max_results,
                   bool match_case,
                   bool auto_match_case,
                   bool enable_regex,
                   bool auto_search_in_path,
                   bool search_in_path,
                   bool pass_on_empty_query)
{
    FsearchQuery *q = calloc (1, sizeof (FsearchQuery));
    assert (q != NULL);
    if (text) {
        q->text = strdup (text);
    }
    q->db = db;
    q->filter = filter;
    q->callback = callback;
    q->callback_data = callback_data;
    q->callback_cancelled = callback_cancelled;
    q->callback_cancelled_data = callback_cancelled_data;
    q->max_results = max_results;
    q->match_case = match_case;
    q->enable_regex = enable_regex;
    q->auto_search_in_path = auto_search_in_path;
    q->auto_match_case = auto_match_case;
    q->search_in_path = search_in_path;
    q->pass_on_empty_query = pass_on_empty_query;
    return q;
}

void
fsearch_query_free (FsearchQuery *query)
{
    assert (query != NULL);
    if (query->text) {
        free (query->text);
        query->text = NULL;
    }
    free (query);
    query = NULL;
}

static bool
fsearch_query_highlight_match_glob (FsearchQueryHighlightToken *token, const char *text, bool match_case)
{
    if (!token->end_with_asterisk && !token->start_with_asterisk) {
        return false;
    }

    if (fnmatch (token->text, text, match_case ? 0 : FNM_CASEFOLD)) {
        return false;
    }

    if (token->end_with_asterisk) {
        token->hl_start = 0;
        token->hl_end = token->query_len - 1;
    }
    else if (token->start_with_asterisk) {
        size_t text_len = strlen (text);
        // +1 because query starts with *
        token->hl_start = text_len - token->query_len + 1;
        token->hl_end = text_len;
    }
    return true;
}

PangoAttrList *
fsearch_query_highlight_match (FsearchQueryHighlight *q, const char *input)
{
    PangoAttrList *attr = pango_attr_list_new ();
    GRegexMatchFlags match_flags = G_REGEX_MATCH_PARTIAL;

    GList *l = q->token;
    while (l) {
        FsearchQueryHighlightToken *token = l->data;
        if (!token || !token->regex) {
            break;
        }
        l = l->next;

        if (token->is_supported_glob && fsearch_query_highlight_match_glob (token, input, q->match_case)) {
            PangoAttribute *pa = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
            pa->start_index = token->hl_start;
            pa->end_index   = token->hl_end;
            pango_attr_list_insert (attr, pa);
            continue;
        }

        GMatchInfo *match_info = NULL;
        g_regex_match (token->regex, input, match_flags, &match_info);
        while (g_match_info_matches (match_info)) {
            int count = g_match_info_get_match_count (match_info);
            for (int index = (count > 1) ? 1 : 0; index < count; index++) {
                int start, end;
                g_match_info_fetch_pos (match_info, index, &start, &end);
                PangoAttribute *pa = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
                pa->start_index = start;
                pa->end_index   = end;
                pango_attr_list_insert (attr, pa);
            }
            g_match_info_next (match_info, NULL);
        }
        g_match_info_free (match_info);
    }
    return attr;
}

static void
fsearch_query_highlight_token_glob_init (FsearchQueryHighlightToken *token, char *text)
{
    if (!token) {
        return;
    }

    token->text = g_strdup (text);
    token->query_len = strlen (text);

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

static FsearchQueryHighlightToken *
fsearch_query_highlight_token_new ()
{
    return calloc (1, sizeof (FsearchQueryHighlightToken));
}

FsearchQueryHighlight *
fsearch_query_highlight_new (const char *text, bool enable_regex, bool match_case, bool auto_match_case, bool auto_search_in_path, bool search_in_path)
{
    if (!text) {
        return NULL;
    }

    FsearchQueryHighlight *q = calloc (1, sizeof (FsearchQueryHighlight));
    assert (q != NULL);

    q->auto_search_in_path = auto_search_in_path;
    q->auto_match_case = auto_match_case;
    q->search_in_path = search_in_path;

    q->has_separator = strchr (text, '/') ? 1 : 0;

    if (fs_str_is_regex (text) && enable_regex) {
        FsearchQueryHighlightToken *token = fsearch_query_highlight_token_new ();

        bool has_uppercase = fs_str_utf8_has_upper (text);
        if (!match_case && auto_match_case) {
            q->match_case = has_uppercase ? true : false;
        }
        token->regex = g_regex_new (text, !q->match_case ? G_REGEX_CASELESS : 0, 0, NULL);
        token->text = g_strdup (text);
        token->query_len = strlen (text);
        q->token = g_list_append (q->token, token);
    }
    else {
        gchar *tmp = g_strdup (text);
//        if (!match_case) {
//            utf8lwr (tmp);
//        }
        // remove leading/trailing whitespace
        g_strstrip (tmp);

        // whitespace is regarded as AND so split query there in multiple queries
        gchar **queries = g_strsplit_set (tmp, " ", -1);
        guint n_queries = g_strv_length (queries);

        g_free (tmp);
        tmp = NULL;

        for (int i = 0; i < n_queries; i++) {
            FsearchQueryHighlightToken *token = fsearch_query_highlight_token_new ();
            assert (token != NULL);

            gchar *query_escaped = g_regex_escape_string (queries[i], -1);

            bool has_uppercase = fs_str_utf8_has_upper (query_escaped);
            bool query_match_case = false;
            if (!match_case && auto_match_case) {
                query_match_case = has_uppercase ? true : false;
            }
            token->regex = g_regex_new (query_escaped, !query_match_case? G_REGEX_CASELESS : 0, 0, NULL);
            g_free (query_escaped);
            query_escaped = NULL;

            fsearch_query_highlight_token_glob_init (token, queries[i]);

            q->token = g_list_append (q->token, token);
        }

        g_strfreev (queries);
        queries = NULL;

    }

    return q;
}

static void
fsearch_query_highlight_token_free (FsearchQueryHighlightToken *token)
{
    if (!token) {
        return;
    }
    if (token->regex) {
        g_regex_unref (token->regex);
        token->regex = NULL;
    }
    if (token->text) {
        free (token->text);
        token->text = NULL;
    }
    free (token);
    token = NULL;
}

void
fsearch_query_highlight_free (FsearchQueryHighlight *q)
{
    if (q->token) {
        g_list_free_full (q->token, (GDestroyNotify)fsearch_query_highlight_token_free);
    }
    free (q);
    q = NULL;
}


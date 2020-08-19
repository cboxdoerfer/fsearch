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
#include "query.h"
#include "string_utils.h"

FsearchQuery *
fsearch_query_new (const char *query,
                   FsearchDatabase *db,
                   FsearchFilter filter,
                   void (*callback)(void *),
                   void *callback_data,
                   uint32_t max_results,
                   bool match_case,
                   bool enable_regex,
                   bool auto_search_in_path,
                   bool search_in_path,
                   bool pass_on_empty_query)
{
    FsearchQuery *q = calloc (1, sizeof (FsearchQuery));
    assert (q != NULL);
    if (query) {
        q->query = strdup (query);
    }
    q->db = db;
    q->filter = filter;
    q->callback = callback;
    q->callback_data = callback_data;
    q->max_results = max_results;
    q->match_case = match_case;
    q->enable_regex = enable_regex;
    q->auto_search_in_path = auto_search_in_path;
    q->search_in_path = search_in_path;
    q->pass_on_empty_query = pass_on_empty_query;
    return q;
}

void
fsearch_query_free (FsearchQuery *query)
{
    assert (query != NULL);
    if (query->query) {
        free (query->query);
        query->query = NULL;
    }
    free (query);
    query = NULL;
}

PangoAttrList *
fsearch_query_highlight_match (FsearchQueryHighlight *q, const char *input)
{
    PangoAttrList *attr = pango_attr_list_new ();
    GRegexMatchFlags match_flags = G_REGEX_MATCH_PARTIAL;

    GList *l = q->regex;
    while (l) {
        GRegex *r = l->data;
        if (!r) {
            break;
        }
        l = l->next;

        GMatchInfo *match_info = NULL;
        g_regex_match (r, input, match_flags, &match_info);
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

FsearchQueryHighlight *
fsearch_query_highlight_new (const char *text, bool enable_regex, bool match_case, bool auto_search_in_path, bool search_in_path)
{
    if (!text) {
        return NULL;
    }

    FsearchQueryHighlight *q = calloc (1, sizeof (FsearchQueryHighlight));
    assert (q != NULL);

    q->auto_search_in_path = auto_search_in_path;
    q->search_in_path = search_in_path;

    bool has_uppercase = fs_str_has_upper (text);
    q->has_separator = strchr (text, '/') ? 1 : 0;

    if (!match_case) {
        match_case = has_uppercase ? true : false;
    }
    if (fs_str_is_regex (text) && enable_regex) {
        GRegex *r = g_regex_new (text, !match_case ? G_REGEX_CASELESS : 0, 0, NULL);
        q->regex = g_list_append (q->regex, r);
    }
    else {
        gchar *tmp_query_copy = g_regex_escape_string (text, -1);
        // remove leading/trailing whitespace
        g_strstrip (tmp_query_copy);

        // whitespace is regarded as AND so split query there in multiple queries
        gchar **queries = g_strsplit_set (tmp_query_copy, " ", -1);
        guint n_queries = g_strv_length (queries);

        g_free (tmp_query_copy);
        tmp_query_copy = NULL;

        for (int i = 0; i < n_queries; i++) {
            GRegex *r = g_regex_new (queries[i], !match_case ? G_REGEX_CASELESS : 0, 0, NULL);
            q->regex = g_list_append (q->regex, r);
        }

        g_strfreev (queries);
        queries = NULL;

    }

    return q;
}

void
fsearch_query_highlight_free (FsearchQueryHighlight *q)
{
    if (q->regex) {
        g_list_free_full (q->regex, (GDestroyNotify)g_regex_unref);
    }
    free (q);
    q = NULL;
}


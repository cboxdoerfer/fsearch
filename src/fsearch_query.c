/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

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
#include "fsearch_query.h"
#include "fsearch_highlight_token.h"
#include "fsearch_string_utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

FsearchQuery *
fsearch_query_new(const char *search_term,
                  FsearchDatabase *db,
                  int32_t sort_order,
                  FsearchFilter *filter,
                  FsearchThreadPool *pool,
                  FsearchQueryFlags flags,
                  uint32_t id,
                  uint32_t window_id,
                  gpointer data) {
    FsearchQuery *q = calloc(1, sizeof(FsearchQuery));
    assert(q != NULL);

    q->search_term = search_term ? strdup(search_term) : "";
    q->has_separator = strchr(search_term, G_DIR_SEPARATOR) ? 1 : 0;

    q->db = db_ref(db);

    q->sort_order = sort_order;

    q->pool = pool;

    q->token = fsearch_tokens_new(search_term, flags);
    q->num_token = 0;
    for (uint32_t i = 0; q->token[i] != NULL; i++) {
        q->num_token++;
    }

    if (filter && filter->query) {
        q->filter_token = fsearch_tokens_new(filter->query, filter->flags);
        q->num_filter_token = 0;
        for (uint32_t i = 0; q->filter_token[i] != NULL; i++) {
            q->num_filter_token++;
        }
    }

    q->highlight_tokens = fsearch_highlight_tokens_new(q->search_term, flags);

    q->filter = fsearch_filter_ref(filter);
    q->flags = flags;
    q->id = id;
    q->window_id = window_id;
    q->data = data;
    q->ref_count = 1;
    return q;
}

static void
fsearch_query_free(FsearchQuery *query) {
    g_clear_pointer(&query->db, db_unref);
    g_clear_pointer(&query->filter, fsearch_filter_unref);
    g_clear_pointer(&query->highlight_tokens, fsearch_highlight_tokens_free);
    g_clear_pointer(&query->search_term, free);
    g_clear_pointer(&query->token, fsearch_tokens_free);
    g_clear_pointer(&query, free);
}

FsearchQuery *
fsearch_query_ref(FsearchQuery *query) {
    if (!query || query->ref_count <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&query->ref_count);
    return query;
}

void
fsearch_query_unref(FsearchQuery *query) {
    if (!query || query->ref_count <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&query->ref_count)) {
        g_clear_pointer(&query, fsearch_query_free);
    }
}

bool
fsearch_query_matches_everything(FsearchQuery *query) {
    const bool empty_query = fs_str_is_empty(query->search_term);
    if (empty_query && (!query->filter || query->filter->type == FSEARCH_FILTER_NONE)) {
        return true;
    }
    return false;
}

PangoAttrList *
fsearch_query_highlight_match(FsearchQuery *q, const char *input) {
    return fsearch_highlight_tokens_match(q->highlight_tokens, q->flags, input);
}

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
#include "fsearch_database_entry.h"
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
                  const char *query_id,
                  gpointer data) {
    FsearchQuery *q = calloc(1, sizeof(FsearchQuery));
    assert(q != NULL);

    q->search_term = search_term ? strdup(search_term) : "";
    q->has_separator = strchr(q->search_term, G_DIR_SEPARATOR) ? 1 : 0;

    q->db = db_ref(db);

    q->sort_order = sort_order;

    q->pool = pool;

    q->token = fsearch_query_node_tree_new(q->search_term, flags);

    if (filter && filter->query) {
        q->filter_token = fsearch_query_node_tree_new(filter->query, filter->flags);
    }

    q->highlight_tokens = fsearch_highlight_tokens_new(q->search_term, flags);

    q->filter = fsearch_filter_ref(filter);
    q->flags = flags;
    q->query_id = strdup(query_id);
    q->data = data;
    q->ref_count = 1;
    return q;
}

static void
fsearch_query_free(FsearchQuery *query) {
    g_clear_pointer(&query->query_id, free);
    g_clear_pointer(&query->db, db_unref);
    g_clear_pointer(&query->filter, fsearch_filter_unref);
    g_clear_pointer(&query->highlight_tokens, fsearch_highlight_tokens_free);
    g_clear_pointer(&query->search_term, free);
    g_clear_pointer(&query->token, fsearch_query_node_tree_free);
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

static bool
matches(GNode *node, FsearchDatabaseEntry *entry, FsearchQueryMatchContext *matcher, FsearchDatabaseEntryType type) {
    if (!node) {
        return true;
    }
    FsearchQueryNode *n = node->data;
    if (n->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
        GNode *left = node->children;
        assert(left != NULL);
        GNode *right = left->next;
        if (n->operator== FSEARCH_TOKEN_OPERATOR_AND) {
            return matches(left, entry, matcher, type) && matches(right, entry, matcher, type);
        }
        else {
            return matches(left, entry, matcher, type) || matches(right, entry, matcher, type);
        }
    }
    else {
        if (n->flags & QUERY_FLAG_FOLDERS_ONLY && type != DATABASE_ENTRY_TYPE_FOLDER) {
            return false;
        }
        if (n->flags & QUERY_FLAG_FILES_ONLY && type != DATABASE_ENTRY_TYPE_FILE) {
            return false;
        }

        return n->search_func(n, matcher);
    }
}

static bool
filter_entry(FsearchDatabaseEntry *entry, FsearchQueryMatchContext *matcher, FsearchQuery *query) {
    if (!query->filter) {
        return true;
    }
    if (query->filter->type == FSEARCH_FILTER_NONE && query->filter->query == NULL) {
        return true;
    }
    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    bool is_dir = type == DATABASE_ENTRY_TYPE_FOLDER ? true : false;
    bool is_file = type == DATABASE_ENTRY_TYPE_FILE ? true : false;
    if (query->filter->type != FSEARCH_FILTER_FILES && is_file) {
        return false;
    }
    if (query->filter->type != FSEARCH_FILTER_FOLDERS && is_dir) {
        return false;
    }
    if (query->filter_token) {
        return matches(query->filter_token, entry, matcher, type);
    }
    return true;
}

bool
fsearch_query_match(FsearchQuery *query, FsearchQueryMatchContext *matcher) {
    FsearchDatabaseEntry *entry = fsearch_query_match_context_get_entry(matcher);
    if (G_UNLIKELY(!matcher || !entry)) {
        return false;
    }

    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    GNode *token = query->token;

    if (!filter_entry(entry, matcher, query)) {
        return false;
    }

    return matches(token, entry, matcher, type);
}

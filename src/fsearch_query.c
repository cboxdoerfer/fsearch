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

#include "fsearch_query.h"
#include "fsearch_database_entry.h"
#include "fsearch_string_utils.h"
#include <stdlib.h>
#include <string.h>

FsearchQuery *
fsearch_query_new(const char *search_term,
                  FsearchFilter *filter,
                  FsearchFilterManager *filters,
                  FsearchQueryFlags flags,
                  const char *query_id) {
    FsearchQuery *q = calloc(1, sizeof(FsearchQuery));
    g_assert(q);

    q->search_term = search_term ? strdup(search_term) : "";

    q->query_tree = fsearch_query_node_tree_new(q->search_term, filters, flags);
    if (q->query_tree) {
        q->triggers_auto_match_case = fsearch_query_node_tree_triggers_auto_match_case(q->query_tree);
        q->triggers_auto_match_path = fsearch_query_node_tree_triggers_auto_match_path(q->query_tree);
        q->wants_single_threaded_search = fsearch_query_node_tree_wants_single_threaded_search(q->query_tree);
    }

    if (filter && filter->query) {
        q->filter_tree = fsearch_query_node_tree_new(filter->query, filters, filter->flags);
    }

    q->filter = fsearch_filter_ref(filter);
    q->flags = flags;
    q->query_id = strdup(query_id ? query_id : "[missing_id]");
    q->ref_count = 1;
    return q;
}

static void
fsearch_query_free(FsearchQuery *query) {
    g_clear_pointer(&query->query_id, free);
    g_clear_pointer(&query->filter, fsearch_filter_unref);
    g_clear_pointer(&query->search_term, free);
    g_clear_pointer(&query->query_tree, fsearch_query_node_tree_free);
    g_clear_pointer(&query, free);
}

FsearchQuery *
fsearch_query_ref(FsearchQuery *query) {
    if (!query || g_atomic_int_get(&query->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&query->ref_count);
    return query;
}

void
fsearch_query_unref(FsearchQuery *query) {
    if (!query || g_atomic_int_get(&query->ref_count) <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&query->ref_count)) {
        g_clear_pointer(&query, fsearch_query_free);
    }
}

bool
fsearch_query_matches_everything(FsearchQuery *query) {
    const bool empty_query = fsearch_string_is_empty(query->search_term);
    if (empty_query && (!query->filter || !query->filter->query || fsearch_string_is_empty(query->filter->query))) {
        return true;
    }
    return false;
}

static bool
highlight(GNode *node, FsearchDatabaseEntryBase *entry, FsearchQueryMatchData *match_data, FsearchDatabaseEntryType type) {
    if (!node) {
        return true;
    }
    FsearchQueryNode *n = node->data;
    if (!n) {
        return false;
    }
    if (n->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
        GNode *left = node->children;
        g_assert(left);
        GNode *right = left->next;
        if (n->operator== FSEARCH_QUERY_NODE_OPERATOR_AND) {
            return highlight(left, entry, match_data, type) && highlight(right, entry, match_data, type);
        }
        else if (n->operator== FSEARCH_QUERY_NODE_OPERATOR_OR) {
            return highlight(left, entry, match_data, type) || highlight(right, entry, match_data, type);
        }
        else {
            return !highlight(left, entry, match_data, type);
        }
    }
    else {
        if (n->flags & QUERY_FLAG_FOLDERS_ONLY && type != DATABASE_ENTRY_TYPE_FOLDER) {
            return false;
        }
        if (n->flags & QUERY_FLAG_FILES_ONLY && type != DATABASE_ENTRY_TYPE_FILE) {
            return false;
        }

        return n->highlight_func ? n->highlight_func(n, match_data) : false;
    }
}

static bool
matches(GNode *node, FsearchDatabaseEntryBase *entry, FsearchQueryMatchData *match_data, FsearchDatabaseEntryType type) {
    if (!node) {
        return true;
    }
    FsearchQueryNode *n = node->data;
    if (!n) {
        return false;
    }
    if (n->type == FSEARCH_QUERY_NODE_TYPE_OPERATOR) {
        GNode *left = node->children;
        g_assert(left);
        GNode *right = left->next;
        if (n->operator== FSEARCH_QUERY_NODE_OPERATOR_AND) {
            return matches(left, entry, match_data, type) && matches(right, entry, match_data, type);
        }
        else if (n->operator== FSEARCH_QUERY_NODE_OPERATOR_OR) {
            return matches(left, entry, match_data, type) || matches(right, entry, match_data, type);
        }
        else {
            return !matches(left, entry, match_data, type);
        }
    }
    else {
        if (n->flags & QUERY_FLAG_FOLDERS_ONLY && type != DATABASE_ENTRY_TYPE_FOLDER) {
            return false;
        }
        if (n->flags & QUERY_FLAG_FILES_ONLY && type != DATABASE_ENTRY_TYPE_FILE) {
            return false;
        }

        return n->search_func(n, match_data);
    }
}

static bool
filter_entry(FsearchDatabaseEntryBase *entry, FsearchQueryMatchData *match_data, FsearchQuery *query) {
    if (!query->filter) {
        return true;
    }
    if (query->filter->query == NULL || fsearch_string_is_empty(query->filter->query)) {
        return true;
    }
    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    if (query->filter_tree) {
        return matches(query->filter_tree, entry, match_data, type);
    }
    return true;
}

bool
fsearch_query_highlight(FsearchQuery *query, FsearchQueryMatchData *match_data) {
    FsearchDatabaseEntryBase *entry = fsearch_query_match_data_get_entry(match_data);
    if (G_UNLIKELY(!match_data || !entry)) {
        return false;
    }

    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    GNode *token = query->query_tree;

    if (!filter_entry(entry, match_data, query)) {
        return false;
    }

    return highlight(token, entry, match_data, type);
}

bool
fsearch_query_match(FsearchQuery *query, FsearchQueryMatchData *match_data) {
    FsearchDatabaseEntryBase *entry = fsearch_query_match_data_get_entry(match_data);
    if (G_UNLIKELY(!match_data || !entry)) {
        return false;
    }

    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    GNode *token = query->query_tree;

    if (!filter_entry(entry, match_data, query)) {
        return false;
    }

    return matches(token, entry, match_data, type);
}

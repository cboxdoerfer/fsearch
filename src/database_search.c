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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "database_search.h"
#include "string_utils.h"

struct _DatabaseSearch
{
    GPtrArray *results;
    FsearchThreadPool *pool;

    DynamicArray *entries;
    uint32_t num_entries;

    gchar *query;
    uint32_t num_folders;
    uint32_t num_files;
    bool enable_regex;
    bool search_in_path;
};

struct _DatabaseSearchEntry
{
    GNode *node;
    uint32_t pos;
};

typedef struct search_query_s {
    gchar *query;
    size_t query_len;
    bool has_uppercase;
    bool found;
} search_query_t;

typedef struct search_context_s {
    DatabaseSearch *search;
    GNode **results;
    GList *queries;
    uint32_t num_queries;
    uint32_t num_results;
    FsearchFilter filter;
    uint32_t max_results;
    uint32_t start_pos;
    uint32_t end_pos;
} search_context_t;

DatabaseSearchEntry *
db_search_entry_new (GNode *node, uint32_t pos);

static void
db_search_entry_free (DatabaseSearchEntry *entry);

static search_context_t *
new_thread_data (DatabaseSearch *search,
                 GList *queries,
                 uint32_t num_queries,
                 FsearchFilter filter,
                 uint32_t max_results,
                 uint32_t start_pos,
                 uint32_t end_pos)
{
    search_context_t *ctx = calloc (1, sizeof(search_context_t));
    g_assert (ctx != NULL);

    ctx->search = search;
    ctx->queries = queries;
    ctx->num_queries = num_queries;
    ctx->results = calloc (end_pos - start_pos + 1, sizeof (GNode *));
    g_assert (ctx->results != NULL);

    ctx->num_results = 0;
    ctx->filter = filter;
    ctx->max_results = max_results;
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static inline bool
filter_node (GNode *node, FsearchFilter filter)
{
    if (filter == FSEARCH_FILTER_NONE) {
        return true;
    }
    bool is_dir = db_node_is_dir (node);
    if (filter == FSEARCH_FILTER_FILES
        && !is_dir) {
        return true;
    }
    if (filter == FSEARCH_FILTER_FOLDERS
        && is_dir) {
        return true;
    }
    return false;
}

static gpointer
search_thread (gpointer user_data)
{
    search_context_t *ctx = (search_context_t *)user_data;
    g_assert (ctx != NULL);
    g_assert (ctx->results != NULL);

    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t max_results = ctx->max_results;
    const uint32_t num_queries = ctx->num_queries;
    const FsearchFilter filter = ctx->filter;
    GList *queries = ctx->queries;
    const bool search_in_path = ctx->search->search_in_path;
    DynamicArray *entries = ctx->search->entries;


    uint32_t num_results = 0;
    GNode **results = ctx->results;
    for (uint32_t i = start; i <= end; i++) {
        if (num_results == max_results) {
            break;
        }
        GNode *node = darray_get_item (entries, i);
        if (!node) {
            continue;
        }
        if (!filter_node (node, filter)) {
            continue;
        }

        const char *haystack = NULL;
        if (search_in_path) {
            gchar full_path[PATH_MAX] = "";
            db_node_get_path_full (node, full_path, sizeof (full_path));
            haystack = full_path;
        }
        else {
            DatabaseNodeData *data = node->data;
            haystack = data->name;
        }


        uint32_t num_found = 0;
        GList *temp = queries;
        while (temp) {
            search_query_t *query = temp->data;
            gchar *ptr = query->query;
            if (!strcasestr (haystack, ptr)) {
                break;
            }
            num_found++;
            temp = temp->next;
        }

        if (num_found == num_queries) {
            results[num_results] = node;
            num_results++;
        }
    }
    ctx->num_results = num_results;
    return NULL;
}

static struct timeval tm1;

static inline void start()
{
    gettimeofday(&tm1, NULL);
}

static inline void stop()
{
    struct timeval tm2;
    gettimeofday(&tm2, NULL);

    unsigned long long t = 1000 * (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) / 1000;
    printf("%llu ms\n", t);
}

static gpointer
search_regex_thread (gpointer user_data)
{
    search_context_t *ctx = (search_context_t *)user_data;
    g_assert (ctx != NULL);
    g_assert (ctx->results != NULL);

    GList *queries = ctx->queries;
    search_query_t *query = queries->data;
    GRegexCompileFlags regex_compile_flags = G_REGEX_CASELESS | G_REGEX_OPTIMIZE;
    GError *error = NULL;
    GRegex *regex = g_regex_new (query->query, regex_compile_flags, 0, &error);

    if (regex) {
        const uint32_t start = ctx->start_pos;
        const uint32_t end = ctx->end_pos;
        const uint32_t max_results = ctx->max_results;
        const bool search_in_path = ctx->search->search_in_path;
        DynamicArray *entries = ctx->search->entries;
        GNode **results = ctx->results;
        const FsearchFilter filter = ctx->filter;

        uint32_t num_results = 0;
        for (uint32_t i = start; i <= end; ++i) {
            if (num_results == max_results) {
                break;
            }
            GNode *node = darray_get_item (entries, i);
            if (!node) {
                continue;
            }

            if (!filter_node (node, filter)) {
                continue;
            }

            const char *haystack = NULL;
            if (search_in_path) {
                gchar full_path[PATH_MAX] = "";
                db_node_get_path_full (node, full_path, sizeof (full_path));
                haystack = full_path;
            }
            else {
                DatabaseNodeData *data = node->data;
                haystack = data->name;
            }

            GMatchInfo *match_info = NULL;
            if (g_regex_match (regex, haystack, 0, &match_info)) {
                results[num_results] = node;
                num_results++;
            }
            g_match_info_free (match_info);
        }
        ctx->num_results = num_results;
        g_regex_unref (regex);
    }
    return NULL;
}

int
is_regex (const char *query)
{
    char regex_chars[] = {
        '$',
        '(',
        ')',
        '*',
        '+',
        '.',
        '?',
        '[',
        '\\',
        '^',
        '{',
        '|',
        '\0'
    };

    return (strpbrk(query, regex_chars) != NULL);
}

bool
str_has_upper (const char *string)
{
    g_assert (string != NULL);
    const char *ptr = string;
    while (ptr != '\0') {
        if (g_ascii_isupper (ptr)) {
            return true;
        }
        ptr++;

    }
    return false;
}

static void
search_query_free (gpointer data)
{
    search_query_t *query = data;
    g_return_if_fail (query != NULL);
    if (query->query != NULL) {
        g_free (query->query);
        query->query = NULL;
    }
    g_free (query);
    query = NULL;
}

static search_query_t *
search_query_new (const char *query)
{
    search_query_t *new = g_new0 (search_query_t, 1);
    g_assert (new != NULL);

    new->query = g_strdup (query);
    new->query_len = strlen (query);
    new->has_uppercase = str_has_upper (query);
    new->found = false;
    return new;
}

static GList *
build_queries (DatabaseSearch *search)
{
    g_assert (search != NULL);
    g_assert (search->query != NULL);

    gchar *tmp_query_copy = strdup (search->query);
    g_assert (tmp_query_copy != NULL);
    // remove leading/trailing whitespace
    g_strstrip (tmp_query_copy);

    // check if regex characters are present
    const bool is_reg = is_regex (search->query);
    if (is_reg && search->enable_regex) {
        GList *queries = g_list_append (NULL, search_query_new (tmp_query_copy));
        g_free (tmp_query_copy);
        tmp_query_copy = NULL;

        return queries;
    }
    // whitespace is regarded as AND so split query there in multiple queries
    gchar **tmp_queries = g_strsplit_set (tmp_query_copy, " ", -1);
    g_assert (tmp_queries != NULL);

    uint32_t tmp_queries_len = g_strv_length (tmp_queries);
    GList *queries = NULL;
    for (uint32_t i = 0; i < tmp_queries_len; i++) {
        queries = g_list_append (queries, search_query_new (tmp_queries[i]));
    }

    g_free (tmp_query_copy);
    tmp_query_copy = NULL;
    g_strfreev (tmp_queries);
    tmp_queries = NULL;

    return queries;
}

static uint32_t
db_perform_normal_search (DatabaseSearch *search,
                          FsearchFilter filter,
                          uint32_t max_results)
{
    g_assert (search != NULL);
    g_assert (search->entries != NULL);

    printf("init search: ");
    start ();

    GList *queries = build_queries (search);

    const uint32_t num_threads = fsearch_thread_pool_get_num_threads (search->pool);
    const uint32_t num_items_per_thread = search->num_entries / num_threads;

    search_context_t *thread_data[num_threads];
    memset (thread_data, 0, num_threads * sizeof (search_context_t *));

    const bool limit_results = max_results ? true : false;
    const bool is_reg = is_regex (search->query);
    const uint32_t num_queries = g_list_length (queries);
    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;

    stop ();
    start ();
    GList *temp = fsearch_thread_pool_get_threads (search->pool);
    for (uint32_t i = 0; i < num_threads; i++) {
        thread_data[i] = new_thread_data (search,
                queries,
                num_queries,
                filter,
                limit_results ? max_results : -1,
                start_pos,
                i == num_threads - 1 ? search->num_entries - 1 : end_pos);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;

        if (is_reg && search->enable_regex) {
            fsearch_thread_pool_push_data (search->pool,
                                           temp,
                                           search_regex_thread,
                                           thread_data[i]);
        }
        else {
            fsearch_thread_pool_push_data (search->pool,
                                           temp,
                                           search_thread,
                                           thread_data[i]);
        }
        temp = temp->next;
    }

    temp = fsearch_thread_pool_get_threads (search->pool);
    while (temp) {
        fsearch_thread_pool_wait_for_thread (search->pool, temp);
        temp = temp->next;
    }

    printf("search done: ");
    stop ();
    start ();

    // get total number of entries found
    uint32_t num_results = 0;
    for (uint32_t i = 0; i < num_threads; ++i) {
        num_results += thread_data[i]->num_results;
    }

    search->results = g_ptr_array_sized_new (MIN (num_results, max_results));
    g_ptr_array_set_free_func (search->results, (GDestroyNotify)db_search_entry_free);

    uint32_t pos = 0;
    for (uint32_t i = 0; i < num_threads; i++) {
        search_context_t *ctx = thread_data[i];
        if (!ctx) {
            break;
        }
        for (uint32_t j = 0; j < ctx->num_results; ++j) {
            if (limit_results) {
                if (pos >= max_results) {
                    break;
                }
            }
            GNode *node = ctx->results[j];
            if (db_node_is_dir (node)) {
                search->num_folders++;
            }
            else {
                search->num_files++;
            }
            DatabaseSearchEntry *entry = db_search_entry_new (node, pos);
            g_ptr_array_add (search->results, entry);
            pos++;
        }
        if (ctx->results) {
            g_free (ctx->results);
            ctx->results = NULL;
        }
        if (ctx) {
            g_free (ctx);
            ctx = NULL;
        }
    }

    g_list_free_full (queries, search_query_free);
    stop ();
    return search->results->len;
}

static void
db_search_results_clear (DatabaseSearch *search)
{
    g_assert (search != NULL);

    // free entries
    if (search->results) {
        g_ptr_array_free (search->results, TRUE);
        search->results = NULL;
    }
    search->num_folders = 0;
    search->num_files = 0;
    return;
}

void
db_search_free (DatabaseSearch *search)
{
    g_assert (search != NULL);

    db_search_results_clear (search);
    if (search->query) {
        g_free (search->query);
        search->query = NULL;
    }
    g_free (search);
    search = NULL;
    return;
}

GNode *
db_search_entry_get_node (DatabaseSearchEntry *entry)
{
    return entry->node;
}

uint32_t
db_search_entry_get_pos (DatabaseSearchEntry *entry)
{
    return entry->pos;
}

void
db_search_entry_set_pos (DatabaseSearchEntry *entry, uint32_t pos)
{
    entry->pos = pos;
}

static void
db_search_entry_free (DatabaseSearchEntry *entry)
{
    if (entry) {
        g_free (entry);
        entry = NULL;
    }
}

DatabaseSearchEntry *
db_search_entry_new (GNode *node, uint32_t pos)
{
    DatabaseSearchEntry *entry = g_new0 (DatabaseSearchEntry, 1);
    g_assert (entry != NULL);

    entry->node = node;
    entry->pos = pos;
    return entry;
}

DatabaseSearch *
db_search_new (FsearchThreadPool *pool,
               DynamicArray *entries,
               uint32_t num_entries,
               const char *query,
               bool enable_regex,
               bool search_in_path)
{
    DatabaseSearch *db_search = g_new0 (DatabaseSearch, 1);
    g_assert (db_search != NULL);

    db_search->entries = entries;
    db_search->num_entries = num_entries;
    db_search->results = NULL;
    if (query) {
        db_search->query = g_strdup (query);
    }
    else {
        db_search->query = NULL;
    }
    db_search->pool = pool;
    db_search->num_folders = 0;
    db_search->num_files = 0;
    db_search->enable_regex = enable_regex;
    db_search->search_in_path = search_in_path;
    return db_search;
}

void
db_search_set_search_in_path (DatabaseSearch *search, bool search_in_path)
{
    g_assert (search != NULL);

    search->search_in_path = search_in_path;
}

void
db_search_set_query (DatabaseSearch *search, const char *query)
{
    g_assert (search != NULL);

    if (search->query) {
        g_free (search->query);
    }
    search->query = g_strdup (query);
}

void
db_search_update (DatabaseSearch *search,
                  DynamicArray *entries,
                  uint32_t num_entries,
                  const char *query,
                  bool enable_regex,
                  bool search_in_path)
{
    g_assert (search != NULL);

    search->entries = entries;
    search->num_entries = num_entries;
    db_search_set_query (search, query);
    search->enable_regex = enable_regex;
    search->search_in_path = search_in_path;
}

uint32_t
db_search_get_num_results (DatabaseSearch *search)
{
    g_assert (search != NULL);
    return search->results->len;
}

uint32_t
db_search_get_num_files (DatabaseSearch *search)
{
    g_assert (search != NULL);
    return search->num_files;
}

uint32_t
db_search_get_num_folders (DatabaseSearch *search)
{
    g_assert (search != NULL);
    return search->num_folders;
}

static void
update_index (DatabaseSearch *search)
{
    g_assert (search != NULL);

    for (uint32_t i = 0; i < search->results->len; ++i) {
        DatabaseSearchEntry *entry = g_ptr_array_index (search->results, i);
        entry->pos = i;
    }
}

void
db_search_remove_entry (DatabaseSearch *search, DatabaseSearchEntry *entry)
{
    g_return_if_fail (search != NULL);
    g_return_if_fail (entry != NULL);

    g_ptr_array_remove (search->results, (gpointer) entry);
    update_index (search);
}

GPtrArray *
db_search_get_results (DatabaseSearch *search)
{
    g_assert (search != NULL);
    return search->results;
}

static bool
is_empty (const gchar *s)
{
    while (*s != '\0') {
        if (!isspace (*s)) {
            return 0;
        }
        s++;
    }
    return 1;
}

uint32_t
db_perform_search (DatabaseSearch *search,
                   FsearchFilter filter,
                   uint32_t max_results)
{
    g_assert (search != NULL);
    g_return_val_if_fail (search->entries != NULL, 0);

    db_search_results_clear (search);

    // if query is empty string we are done here
    if (is_empty (search->query)) {
        return 0;
    }
    db_perform_normal_search (search, filter, max_results);

    if (search->results) {
        return search->results->len;
    }
    else {
        return 0;
    }
}


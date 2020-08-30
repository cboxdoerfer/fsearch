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
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <pcre.h>
#include <fnmatch.h>

#include "database_search.h"
#include "fsearch_window.h"
#include "string_utils.h"
#include "debug.h"
#include "utf8.h"

#define OVECCOUNT 3

struct _DatabaseSearchEntry
{
    BTreeNode *node;
    uint32_t pos;
};

typedef struct search_token_s {
    char *text;
    uint32_t (*search_func)(const char *, const char *);
    size_t text_len;
    uint32_t has_separator;
} search_token_t;

typedef struct search_context_s {
    FsearchQuery *query;
    BTreeNode **results;
    search_token_t **token;
    uint32_t num_token;
    uint32_t num_results;
    uint32_t start_pos;
    uint32_t end_pos;
} search_thread_context_t;

static DatabaseSearchResult *
db_search (DatabaseSearch *search, FsearchQuery *q);

static DatabaseSearchResult *
db_search_empty (FsearchQuery *query);

DatabaseSearchEntry *
db_search_entry_new (BTreeNode *node, uint32_t pos);

static void
db_search_entry_free (DatabaseSearchEntry *entry);

static gpointer
fsearch_search_thread (gpointer user_data)
{
    DatabaseSearch *search = user_data;

    g_mutex_lock (&search->query_mutex);
    while (true) {
        g_cond_wait (&search->search_thread_start_cond, &search->query_mutex);
        if (search->search_thread_terminate) {
            break;
        }
        while (search->query_ctx) {
            FsearchQuery *query = search->query_ctx;
            if (!query) {
                break;
            }
            search->query_ctx = NULL;
            g_mutex_unlock (&search->query_mutex);
            // if query is empty string we are done here
            DatabaseSearchResult *result = NULL;
            if (fs_str_is_empty (query->text)) {
                if (query->pass_on_empty_query) {
                    result = db_search_empty (query);
                }
                else {
                    result = calloc (1, sizeof (DatabaseSearchResult));
                }
            }
            else {
                result = db_search (search, query);
            }
            g_mutex_lock (&search->query_mutex);
            result->cb_data = query->callback_data;
            result->db = query->db;
            query->callback (result);
            fsearch_query_free (query);
        }
    }
    g_mutex_unlock (&search->query_mutex);
    return NULL;
}


static search_thread_context_t *
search_thread_context_new (FsearchQuery *query,
                           search_token_t **token,
                           uint32_t num_token,
                           uint32_t start_pos,
                           uint32_t end_pos)
{
    search_thread_context_t *ctx = calloc (1, sizeof(search_thread_context_t));
    assert (ctx != NULL);
    assert (end_pos >= start_pos);

    ctx->query = query;
    ctx->token = token;
    ctx->num_token = num_token;
    ctx->results = calloc (end_pos - start_pos + 1, sizeof (BTreeNode *));
    assert (ctx->results != NULL);

    ctx->num_results = 0;
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static inline bool
filter_node (BTreeNode *node, FsearchFilter filter)
{
    if (filter == FSEARCH_FILTER_NONE) {
        return true;
    }
    bool is_dir = node->is_dir;
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

static void *
search_thread (void * user_data)
{
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert (ctx != NULL);
    assert (ctx->results != NULL);

    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t max_results = ctx->query->max_results;
    const uint32_t num_token = ctx->num_token;
    const FsearchFilter filter = ctx->query->filter;
    search_token_t **token = ctx->token;
    const uint32_t search_in_path = ctx->query->search_in_path;
    const uint32_t auto_search_in_path = ctx->query->auto_search_in_path;
    DynamicArray *entries = db_get_entries (ctx->query->db);
    BTreeNode **results = ctx->results;

    if (!entries) {
        ctx->num_results = 0;
        trace ("[database_search] entries empty\n");
        return NULL;
    }

    uint32_t num_results = 0;
    char full_path[PATH_MAX] = "";
    for (uint32_t i = start; i <= end; i++) {
        if (max_results && num_results == max_results) {
            break;
        }
        BTreeNode *node = darray_get_item (entries, i);
        if (!node) {
            continue;
        }
        if (!filter_node (node, filter)) {
            continue;
        }

        const char *haystack_path = NULL;
        const char *haystack_name = node->name;
        if (search_in_path) {
            btree_node_get_path_full (node, full_path, sizeof (full_path));
            haystack_path = full_path;
        }

        uint32_t num_found = 0;
        while (true) {
            if (num_found == num_token) {
                results[num_results] = node;
                num_results++;
                break;
            }
            search_token_t *t = token[num_found++];
            if (!t) {
                break;
            }
            char *ptr = t->text;
            uint32_t (*search_func)(const char *, const char *) = t->search_func;

            const char *haystack = NULL;
            if (search_in_path || (auto_search_in_path && t->has_separator)) {
                if (!haystack_path) {
                    btree_node_get_path_full (node, full_path, sizeof (full_path));
                    haystack_path = full_path;
                }
                haystack = haystack_path;
            }
            else {
                haystack = haystack_name;
            }
            if (!search_func (haystack, ptr)) {
                break;
            }
        }

    }
    ctx->num_results = num_results;
    return NULL;
}

#ifdef DEBUG
static struct timeval tm1;
#endif

static inline void timer_start()
{
#ifdef DEBUG
    gettimeofday(&tm1, NULL);
#endif
}

static inline void timer_stop()
{
#ifdef DEBUG
    struct timeval tm2;
    gettimeofday(&tm2, NULL);

    unsigned long long t = 1000 * (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) / 1000;
    trace ("%llu ms\n", t);
#endif
}

static void *
search_regex_thread (void * user_data)
{
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert (ctx != NULL);
    assert (ctx->results != NULL);

    search_token_t **token = ctx->token;
    search_token_t *t = token[0];
    const char *error;
    int erroffset;
    pcre *regex = pcre_compile (t->text,
                                ctx->query->match_case ? 0 : PCRE_CASELESS,
                                &error,
                                &erroffset,
                                NULL);

    int ovector[OVECCOUNT] = {0};

    if (!regex) {
        return NULL;
    }

    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t max_results = ctx->query->max_results;
    const bool search_in_path = ctx->query->search_in_path;
    const bool auto_search_in_path = ctx->query->auto_search_in_path;
    DynamicArray *entries = db_get_entries (ctx->query->db);
    BTreeNode **results = ctx->results;
    const FsearchFilter filter = ctx->query->filter;

    uint32_t num_results = 0;
    char full_path[PATH_MAX] = "";
    for (uint32_t i = start; i <= end; ++i) {
        if (max_results && num_results == max_results) {
            break;
        }
        BTreeNode *node = darray_get_item (entries, i);
        if (!node) {
            continue;
        }

        if (!filter_node (node, filter)) {
            continue;
        }

        const char *haystack = NULL;
        if (search_in_path || (auto_search_in_path && t->has_separator)) {
            btree_node_get_path_full (node, full_path, sizeof (full_path));
            haystack = full_path;
        }
        else {
            haystack = node->name;
        }
        size_t haystack_len = strlen (haystack);

        if (pcre_exec (regex,
                       NULL,
                       haystack,
                       haystack_len,
                       0,
                       0,
                       ovector,
                       OVECCOUNT)
            >= 0) {
            results[num_results] = node;
            num_results++;
        }
    }
    ctx->num_results = num_results;
    pcre_free (regex);

    return NULL;
}

static uint32_t
search_wildcard_icase (const char *haystack, const char *needle)
{
    return !fnmatch (needle, haystack, FNM_CASEFOLD) ? 1 : 0;
}

static uint32_t
search_wildcard (const char *haystack, const char *needle)
{
    return !fnmatch (needle, haystack, 0) ? 1 : 0;
}

static uint32_t
search_normal_icase_u8 (const char *haystack, const char *needle)
{
    // TODO: make this faster
    return utf8casestr (haystack, needle) ? 1 : 0;
}

static uint32_t
search_normal_icase (const char *haystack, const char *needle)
{
    return strcasestr (haystack, needle) ? 1 : 0;
}

static uint32_t
search_normal (const char *haystack, const char *needle)
{
    return strstr (haystack, needle) ? 1 : 0;
}

static void
search_token_free (void * data)
{
    search_token_t *token = data;
    assert (token != NULL);

    if (token->text != NULL) {
        g_free (token->text);
        token->text = NULL;
    }
    g_free (token);
    token = NULL;
}

static search_token_t *
search_token_new (const char *text, bool match_case, bool auto_match_case)
{
    search_token_t *new = calloc (1, sizeof (search_token_t));
    assert (new != NULL);

    new->text = g_strdup (text);
    new->text_len = strlen (text);
    new->has_separator = strchr (text, '/') ? 1 : 0;

    if (auto_match_case && fs_str_utf8_has_upper (text)) {
        match_case = true;
    }

    if (strchr (text, '*') || strchr (text, '?')) {
        if (match_case) {
            new->search_func = search_wildcard;
        }
        else {
            new->search_func = search_wildcard_icase;
        }
    }
    else {
        if (match_case) {
            new->search_func = search_normal;
        }
        else {
            bool is_utf8 = false;
            // TODO: this might not work at all times?
            if (g_utf8_strlen (text, -1) != new->text_len) {
                is_utf8 = true;
            }

            if (is_utf8) {
                new->search_func = search_normal_icase_u8;
            }
            else {
                new->search_func = search_normal_icase;
            }
        }
    }
    return new;
}

static search_token_t **
db_search_build_token (FsearchQuery *q)
{
    assert (q != NULL);
    assert (q->text != NULL);

    char *tmp_query_copy = strdup (q->text);
    assert (tmp_query_copy != NULL);

    // check if regex characters are present
    const bool is_reg = fs_str_is_regex (q->text);
    if (is_reg && q->enable_regex) {
        search_token_t **token = calloc (2, sizeof (search_token_t *));
        token[0] = search_token_new (tmp_query_copy, q->match_case, q->auto_match_case);
        token[1] = NULL;
        g_free (tmp_query_copy);
        tmp_query_copy = NULL;

        return token;
    }

    // whitespace is regarded as AND so split query there in multiple token
    char **tmp_token = fs_str_split (tmp_query_copy); 
    assert (tmp_token != NULL);

    uint32_t tmp_token_len = g_strv_length (tmp_token);
    search_token_t **token = calloc (tmp_token_len + 1, sizeof (search_token_t *));
    for (uint32_t i = 0; i < tmp_token_len; i++) {
        trace ("[search] token %d: %s\n", i, tmp_token[i]);
        token[i] = search_token_new (tmp_token[i], q->match_case, q->auto_match_case);
    }

    g_free (tmp_query_copy);
    tmp_query_copy = NULL;
    g_strfreev (tmp_token);
    tmp_token = NULL;

    return token;
}

static DatabaseSearchResult *
db_search_empty (FsearchQuery *query)
{
    assert (query != NULL);
    assert (query->db != NULL);

    const uint32_t num_entries = db_get_num_entries (query->db);
    const uint32_t num_results = MIN (query->max_results, num_entries);
    GPtrArray *results = g_ptr_array_sized_new (num_results);
    g_ptr_array_set_free_func (results, (GDestroyNotify)db_search_entry_free);

    DynamicArray *entries = db_get_entries (query->db);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;
    uint32_t pos = 0;
    for (uint32_t i = 0; pos < num_results && i < num_entries; ++i) {
        BTreeNode *node = darray_get_item (entries, i);
        if (!node) {
            continue;
        }

        if (!filter_node (node, query->filter)) {
            continue;
        }
        if (node->is_dir) {
            num_folders++;
        }
        else {
            num_files++;
        }
        DatabaseSearchEntry *entry = db_search_entry_new (node, pos);
        g_ptr_array_add (results, entry);
        pos++;
    }
    DatabaseSearchResult *result_ctx = calloc (1, sizeof (DatabaseSearchResult));
    assert (result_ctx != NULL);
    result_ctx->results = results;
    result_ctx->num_folders = num_folders;
    result_ctx->num_files = num_files;
    return result_ctx;
}

static DatabaseSearchResult *
db_search (DatabaseSearch *search, FsearchQuery *q)
{
    assert (search != NULL);

    search_token_t **token = db_search_build_token (q);

    const uint32_t num_threads = fsearch_thread_pool_get_num_threads (search->pool);
    const uint32_t num_entries = db_get_num_entries (q->db);
    const uint32_t num_items_per_thread = num_entries / num_threads;

    search_thread_context_t *thread_data[num_threads];
    memset (thread_data, 0, num_threads * sizeof (search_thread_context_t *));

    const uint32_t max_results = q->max_results;
    const bool limit_results = max_results ? true : false;
    const bool is_reg = fs_str_is_regex (q->text);
    uint32_t num_token = 0;
    while (token[num_token]) {
        num_token++;
    }
    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;

    timer_start ();
    GList *temp = fsearch_thread_pool_get_threads (search->pool);
    for (uint32_t i = 0; i < num_threads; i++) {
        thread_data[i] = search_thread_context_new (q,
                                                    token,
                                                    num_token,
                                                    start_pos,
                                                    i == num_threads - 1 ? num_entries - 1 : end_pos);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;

        fsearch_thread_pool_push_data (search->pool,
                                       temp,
                                       is_reg && q->enable_regex ?
                                           search_regex_thread : search_thread,
                                       thread_data[i]);
        temp = temp->next;
    }

    temp = fsearch_thread_pool_get_threads (search->pool);
    while (temp) {
        fsearch_thread_pool_wait_for_thread (search->pool, temp);
        temp = temp->next;
    }

    trace ("[search] search finished in ");
    timer_stop ();

    // get total number of entries found
    uint32_t num_results = 0;
    for (uint32_t i = 0; i < num_threads; ++i) {
        num_results += thread_data[i]->num_results;
    }

    GPtrArray *results = g_ptr_array_sized_new (MIN (num_results, max_results));
    g_ptr_array_set_free_func (results, (GDestroyNotify)db_search_entry_free);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;

    uint32_t pos = 0;
    for (uint32_t i = 0; i < num_threads; i++) {
        search_thread_context_t *ctx = thread_data[i];
        if (!ctx) {
            break;
        }
        for (uint32_t j = 0; j < ctx->num_results; ++j) {
            if (limit_results) {
                if (pos >= max_results) {
                    break;
                }
            }
            BTreeNode *node = ctx->results[j];
            if (node->is_dir) {
                num_folders++;
            }
            else {
                num_files++;
            }
            DatabaseSearchEntry *entry = db_search_entry_new (node, pos);
            g_ptr_array_add (results, entry);
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

    for (uint32_t i = 0; i < num_token; ++i) {
        search_token_free (token[i]);
        token[i] = NULL;
    }
    free (token);
    token = NULL;

    DatabaseSearchResult *result_ctx = calloc (1, sizeof (DatabaseSearchResult));
    assert (result_ctx != NULL);
    result_ctx->results = results;
    result_ctx->num_folders = num_folders;
    result_ctx->num_files = num_files;
    return result_ctx;
}

void
db_search_results_clear (DatabaseSearch *search)
{
    assert (search != NULL);

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
    assert (search != NULL);

    db_search_results_clear (search);
    g_mutex_lock (&search->query_mutex);
    if (search->query_ctx) {
        fsearch_query_free (search->query_ctx);
        search->query_ctx = NULL;
    }
    g_mutex_unlock (&search->query_mutex);

    search->search_thread_terminate = true;
    g_cond_signal (&search->search_thread_start_cond);
    g_thread_join (search->search_thread);
    g_mutex_clear (&search->query_mutex);
    g_cond_clear (&search->search_thread_start_cond);
    g_free (search);
    search = NULL;
    return;
}

BTreeNode *
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
db_search_entry_new (BTreeNode *node, uint32_t pos)
{
    DatabaseSearchEntry *entry = calloc (1, sizeof (DatabaseSearchEntry));
    assert (entry != NULL);

    entry->node = node;
    entry->pos = pos;
    return entry;
}

DatabaseSearch *
db_search_new (FsearchThreadPool *pool)
{
    DatabaseSearch *db_search = calloc (1, sizeof (DatabaseSearch));
    assert (db_search != NULL);

    db_search->pool = pool;
    g_mutex_init (&db_search->query_mutex);
    g_cond_init (&db_search->search_thread_start_cond);
    db_search->search_thread = g_thread_new("fsearch_search_thread", fsearch_search_thread, db_search);
    return db_search;
}

uint32_t
db_search_get_num_results (DatabaseSearch *search)
{
    assert (search != NULL);
    return search->results->len;
}

uint32_t
db_search_get_num_files (DatabaseSearch *search)
{
    assert (search != NULL);
    return search->num_files;
}

uint32_t
db_search_get_num_folders (DatabaseSearch *search)
{
    assert (search != NULL);
    return search->num_folders;
}

static void
update_index (DatabaseSearch *search)
{
    assert (search != NULL);

    for (uint32_t i = 0; i < search->results->len; ++i) {
        DatabaseSearchEntry *entry = g_ptr_array_index (search->results, i);
        entry->pos = i;
    }
}

void
db_search_remove_entry (DatabaseSearch *search, DatabaseSearchEntry *entry)
{
    if (search == NULL) {
        return;
    }
    if (entry == NULL) {
        return;
    }

    g_ptr_array_remove (search->results, (void *) entry);
    update_index (search);
}

GPtrArray *
db_search_get_results (DatabaseSearch *search)
{
    assert (search != NULL);
    return search->results;
}

void
db_search_queue (DatabaseSearch *search, FsearchQuery *query)
{
    g_mutex_lock (&search->query_mutex);
    if (search->query_ctx) {
        if (search->query_ctx->db) {
            db_unref (search->query_ctx->db);
        }
        search->query_ctx->callback_cancelled (search->query_ctx->callback_cancelled_data);
        fsearch_query_free (search->query_ctx);
        search->query_ctx = NULL;
    }
    search->query_ctx = query;
    g_mutex_unlock (&search->query_mutex);
    g_cond_signal (&search->search_thread_start_cond);
}


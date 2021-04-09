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
#include "database_search.h"

#include <assert.h>
#include <ctype.h>
#include <fnmatch.h>
#include <pcre.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "array.h"
#include "debug.h"
#include "fsearch_limits.h"
#include "fsearch_timer.h"
#include "fsearch_window.h"
#include "string_utils.h"
#include "token.h"

typedef struct search_context_s {
    FsearchQuery *query;
    BTreeNode **results;
    bool *terminate;
    uint32_t num_results;
    uint32_t num_folders;
    uint32_t num_files;
    uint32_t start_pos;
    uint32_t end_pos;
} search_thread_context_t;

static DatabaseSearchResult *
db_search(DatabaseSearch *search, FsearchQuery *q);

static DatabaseSearchResult *
db_search_empty(FsearchQuery *query);

static DatabaseSearchResult *
db_search_result_new(DynamicArray *entries, uint32_t num_folders, uint32_t num_files) {
    DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
    assert(result_ctx != NULL);
    result_ctx->entries = entries;
    result_ctx->num_folders = num_folders;
    result_ctx->num_files = num_files;
    return result_ctx;
}

static void
db_search_cancelled(FsearchQuery *query) {
    if (!query) {
        return;
    }
    if (query->callback_cancelled) {
        query->callback_cancelled(query->callback_cancelled_data);
    }
    fsearch_query_free(query);
    query = NULL;
}

static gpointer
db_search_thread(gpointer user_data) {
    DatabaseSearch *search = user_data;

    while (true) {
        if (search->search_thread_terminate) {
            break;
        }

        FsearchQuery *query = g_async_queue_timeout_pop(search->search_queue, 500000);
        if (!query) {
            continue;
        }
        search->search_terminate = false;

        const bool empty_query = fs_str_is_empty(query->text);

        GTimer *timer = fsearch_timer_start();
        DatabaseSearchResult *result = NULL;
        if (empty_query && !query->pass_on_empty_query) {
            result = db_search_result_new(NULL, 0, 0);
        }
        else if (empty_query && (!query->filter || query->filter->type == FSEARCH_FILTER_NONE)) {
            result = db_search_empty(query);
        }
        else {
            result = db_search(search, query);
        }
        if (result) {
            const double seconds = g_timer_elapsed(timer, NULL);
            trace("[search] search %d.%d finished in %.2f ms\n", query->window_id, query->id, seconds * 1000);

            result->cb_data = query->callback_data;
            result->query = query;
            query->callback(result);
        }
        else {
            trace("[search] search %d.%d cancelled\n", query->window_id, query->id);
            db_search_cancelled(query);
        }
        g_timer_stop(timer);
        g_timer_destroy(timer);
        timer = NULL;
    }
    return NULL;
}

static void
search_thread_context_free(search_thread_context_t *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->results) {
        g_free(ctx->results);
        ctx->results = NULL;
    }
    if (ctx) {
        g_free(ctx);
        ctx = NULL;
    }
}

static search_thread_context_t *
search_thread_context_new(FsearchQuery *query, bool *terminate, uint32_t start_pos, uint32_t end_pos) {
    search_thread_context_t *ctx = calloc(1, sizeof(search_thread_context_t));
    assert(ctx != NULL);
    assert(end_pos >= start_pos);

    ctx->query = query;
    ctx->terminate = terminate;
    ctx->results = calloc(end_pos - start_pos + 1, sizeof(BTreeNode *));
    assert(ctx->results != NULL);

    ctx->num_results = 0;
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static inline bool
filter_node(BTreeNode *node, FsearchQuery *query, const char *haystack) {
    if (!query->filter) {
        return true;
    }
    if (query->filter->type == FSEARCH_FILTER_NONE && query->filter->query == NULL) {
        return true;
    }
    bool is_dir = node->is_dir;
    if (query->filter->type == FSEARCH_FILTER_FILES && is_dir) {
        return false;
    }
    if (query->filter->type == FSEARCH_FILTER_FOLDERS && !is_dir) {
        return false;
    }
    if (query->filter_token) {
        uint32_t num_found = 0;
        while (true) {
            if (num_found == query->num_filter_token) {
                return true;
            }
            FsearchToken *t = query->filter_token[num_found++];
            if (!t) {
                return false;
            }

            if (!t->search_func(haystack, t->text, t)) {
                return false;
            }
        }
        return false;
    }
    return true;
}

static void *
db_search_worker(void *user_data) {
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    FsearchQuery *query = ctx->query;
    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t num_token = query->num_token;
    FsearchToken **token = query->token;
    const uint32_t search_in_path = query->flags.search_in_path;
    const uint32_t auto_search_in_path = query->flags.auto_search_in_path;
    DynamicArray *entries = query->entries;
    BTreeNode **results = ctx->results;

    if (!entries) {
        ctx->num_results = 0;
        trace("[database_search] entries empty\n");
        return NULL;
    }

    uint32_t num_results = 0;
    uint32_t num_files = 0;
    uint32_t num_folders = 0;

    bool path_set = false;

    GString *path_string = g_string_sized_new(PATH_MAX);
    for (uint32_t i = start; i <= end; i++) {
        if (*ctx->terminate) {
            return NULL;
        }
        BTreeNode *node = darray_get_item(entries, i);
        if (!node) {
            continue;
        }
        const char *haystack_name = node->name;
        if (search_in_path || query->filter->search_in_path) {
            btree_node_fill_path_string_full(node, path_string);
            path_set = true;
        }

        if (!filter_node(node, query, query->filter->search_in_path ? path_string->str : haystack_name)) {
            continue;
        }

        uint32_t num_found = 0;
        while (true) {
            if (num_found == num_token) {
                results[num_results] = node;
                num_results++;
                node->is_dir ? num_folders++ : num_files++;
                break;
            }
            FsearchToken *t = token[num_found++];
            if (!t) {
                break;
            }

            const char *haystack = NULL;
            if (search_in_path || (auto_search_in_path && t->has_separator)) {
                if (!path_set) {
                    btree_node_fill_path_string_full(node, path_string);
                    path_set = true;
                }
                haystack = path_string->str;
            }
            else {
                haystack = haystack_name;
            }
            if (!t->search_func(haystack, t->text, t)) {
                break;
            }
        }
    }
    g_string_free(path_string, TRUE);
    path_string = NULL;

    ctx->num_results = num_results;
    ctx->num_folders = num_folders;
    ctx->num_files = num_files;

    return NULL;
}

static DatabaseSearchResult *
db_search_empty(FsearchQuery *query) {
    assert(query != NULL);
    assert(query->entries != NULL);

    DynamicArray *entries = query->entries;
    const uint32_t num_entries = darray_get_num_items(query->entries);

    DynamicArray *results = darray_new(num_entries);

    void **data = darray_get_data(entries, NULL);
    darray_add_items(results, data, num_entries);

    return db_search_result_new(results, query->num_folders, query->num_files);
}

static DatabaseSearchResult *
db_search(DatabaseSearch *search, FsearchQuery *q) {
    assert(search != NULL);

    const uint32_t num_entries = darray_get_num_items(q->entries);
    if (num_entries == 0) {
        return db_search_result_new(NULL, 0, 0);
    }
    const uint32_t num_threads = MIN(fsearch_thread_pool_get_num_threads(search->pool), num_entries);
    const uint32_t num_items_per_thread = num_entries / num_threads;

    search_thread_context_t *thread_data[num_threads];
    memset(thread_data, 0, num_threads * sizeof(search_thread_context_t *));

    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;

    if (!q->token) {
        return db_search_result_new(NULL, 0, 0);
    }

    GList *threads = fsearch_thread_pool_get_threads(search->pool);
    for (uint32_t i = 0; i < num_threads; i++) {
        thread_data[i] = search_thread_context_new(q,
                                                   &search->search_terminate,
                                                   start_pos,
                                                   i == num_threads - 1 ? num_entries - 1 : end_pos);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;

        fsearch_thread_pool_push_data(search->pool, threads, db_search_worker, thread_data[i]);
        threads = threads->next;
    }

    threads = fsearch_thread_pool_get_threads(search->pool);
    while (threads) {
        fsearch_thread_pool_wait_for_thread(search->pool, threads);
        threads = threads->next;
    }
    if (search->search_terminate) {
        for (uint32_t i = 0; i < num_threads; i++) {
            search_thread_context_t *ctx = thread_data[i];
            search_thread_context_free(ctx);
        }
        return NULL;
    }

    // get total number of entries found
    uint32_t num_results = 0;
    for (uint32_t i = 0; i < num_threads; ++i) {
        num_results += thread_data[i]->num_results;
    }

    DynamicArray *results = darray_new(num_results);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;

    for (uint32_t i = 0; i < num_threads; i++) {
        search_thread_context_t *ctx = thread_data[i];
        if (!ctx) {
            break;
        }

        num_folders += ctx->num_folders;
        num_files += ctx->num_files;

        darray_add_items(results, (void **)ctx->results, ctx->num_results);

        search_thread_context_free(ctx);
    }

    return db_search_result_new(results, num_folders, num_files);
}

static void
db_search_clear_queue(GAsyncQueue *queue) {
    while (true) {
        // clear all queued queries
        FsearchQuery *queued_query = g_async_queue_try_pop(queue);
        if (!queued_query) {
            break;
        }
        db_search_cancelled(queued_query);
    }
}

void
db_search_result_free(DatabaseSearchResult *result) {
    if (!result) {
        return;
    }

    if (result->entries) {
        darray_free(result->entries);
        result->entries = NULL;
    }
    if (result->query) {
        fsearch_query_free(result->query);
        result->query = NULL;
    }

    result->num_files = 0;
    result->num_folders = 0;

    free(result);
    result = NULL;
}

void
db_search_free(DatabaseSearch *search) {
    assert(search != NULL);

    db_search_clear_queue(search->search_queue);
    search->search_thread_terminate = true;
    g_thread_join(search->search_thread);
    g_free(search);
    search = NULL;
    return;
}

DatabaseSearch *
db_search_new(FsearchThreadPool *pool) {
    DatabaseSearch *db_search = calloc(1, sizeof(DatabaseSearch));
    assert(db_search != NULL);

    db_search->pool = pool;
    db_search->search_queue = g_async_queue_new();
    db_search->search_thread = g_thread_new("fsearch_search_thread", db_search_thread, db_search);
    return db_search;
}

void
db_search_queue(DatabaseSearch *search, FsearchQuery *query) {

    db_search_clear_queue(search->search_queue);
    search->search_terminate = true;
    g_async_queue_push(search->search_queue, query);
}


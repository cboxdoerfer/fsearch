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

#define G_LOG_DOMAIN "fsearch-search"

#include "fsearch_database_search.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_array.h"
#include "fsearch_query_match_context.h"
#include "fsearch_string_utils.h"
#include "fsearch_task.h"
#include "fsearch_task_ids.h"
#include "fsearch_query_node.h"
#include "fsearch_utf.h"

#define THRESHOLD_FOR_PARALLEL_SEARCH 1000

struct DatabaseSearchResult {
    DynamicArray *files;
    DynamicArray *folders;

    FsearchDatabase *db;
    FsearchDatabaseIndexType sort_type;

    volatile int ref_count;
};

typedef struct DatabaseSearchWorkerContext {
    FsearchQuery *query;
    void **results;
    DynamicArray *entries;
    GCancellable *cancellable;
    uint32_t num_results;
    uint32_t start_pos;
    uint32_t end_pos;
} DatabaseSearchWorkerContext;

static DatabaseSearchResult *
db_search(FsearchQuery *q, GCancellable *cancellable);

static DatabaseSearchResult *
db_search_empty(FsearchQuery *q);

static DatabaseSearchResult *
db_search_result_new(void) {
    DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
    assert(result_ctx != NULL);
    result_ctx->ref_count = 1;
    return result_ctx;
}

static void
db_search_result_free(DatabaseSearchResult *result) {
    g_clear_pointer(&result->folders, darray_unref);
    g_clear_pointer(&result->files, darray_unref);
    g_clear_pointer(&result->db, db_unref);
    g_clear_pointer(&result, free);
}

DatabaseSearchResult *
db_search_result_ref(DatabaseSearchResult *result) {
    if (!result || result->ref_count <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&result->ref_count);
    return result;
}

void
db_search_result_unref(DatabaseSearchResult *result) {
    if (!result || result->ref_count <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&result->ref_count)) {
        g_clear_pointer(&result, db_search_result_free);
    }
}

FsearchDatabaseIndexType
db_search_result_get_sort_type(DatabaseSearchResult *result) {
    return result->sort_type;
}

FsearchDatabase *
db_search_result_get_db(DatabaseSearchResult *result) {
    return db_ref(result->db);
}

DynamicArray *
db_search_result_get_files(DatabaseSearchResult *result) {
    return darray_ref(result->files);
}

DynamicArray *
db_search_result_get_folders(DatabaseSearchResult *result) {
    return darray_ref(result->folders);
}

static gpointer
db_search_task(gpointer data, GCancellable *cancellable) {
    FsearchQuery *query = data;

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    DatabaseSearchResult *result = NULL;
    if (fsearch_query_matches_everything(query)) {
        result = db_search_empty(query);
    }
    else {
        result = db_search(query, cancellable);
    }

    const char *debug_message = NULL;
    const double seconds = g_timer_elapsed(timer, NULL);
    if (!g_cancellable_is_cancelled(cancellable)) {
        debug_message = "[%s] finished in %.2f ms";
    }
    else {
        debug_message = "[%s] aborted after %.2f ms";
    }
    g_timer_stop(timer);
    g_clear_pointer(&timer, g_timer_destroy);

    g_debug(debug_message, query->query_id, seconds * 1000);

    return result;
}

static void
db_search_worker_context_free(DatabaseSearchWorkerContext *ctx) {
    if (!ctx) {
        return;
    }

    g_clear_pointer(&ctx->results, free);
    g_clear_pointer(&ctx->entries, darray_unref);
    g_clear_pointer(&ctx, free);
}

static DatabaseSearchWorkerContext *
db_search_worker_context_new(FsearchQuery *query,
                             GCancellable *cancellable,
                             DynamicArray *entries,
                             uint32_t start_pos,
                             uint32_t end_pos) {
    DatabaseSearchWorkerContext *ctx = calloc(1, sizeof(DatabaseSearchWorkerContext));
    assert(ctx != NULL);
    assert(end_pos >= start_pos);

    ctx->query = query;
    ctx->cancellable = cancellable;
    ctx->results = calloc(end_pos - start_pos + 1, sizeof(void *));
    assert(ctx->results != NULL);

    ctx->num_results = 0;
    ctx->entries = darray_ref(entries);
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static void
db_search_worker(void *data) {
    DatabaseSearchWorkerContext *ctx = data;
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    FsearchQueryMatchContext *matcher = fsearch_query_match_context_new();

    FsearchQuery *query = ctx->query;
    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    FsearchDatabaseEntry **results = (FsearchDatabaseEntry **)ctx->results;
    DynamicArray *entries = ctx->entries;

    if (!entries) {
        ctx->num_results = 0;
        g_debug("[db_search] entries empty");
        return;
    }

    uint32_t num_results = 0;
    for (uint32_t i = start; i <= end; i++) {
        if (G_UNLIKELY(g_cancellable_is_cancelled(ctx->cancellable))) {
            break;
        }
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        fsearch_query_match_context_set_entry(matcher, entry);
        if (fsearch_query_match(query, matcher)) {
            results[num_results++] = entry;
        }
    }
    g_clear_pointer(&matcher, fsearch_query_match_context_free);

    ctx->num_results = num_results;
}

static DynamicArray *
db_search_entries(FsearchQuery *q,
                  GCancellable *cancellable,
                  DynamicArray *entries,
                  FsearchThreadPoolFunc search_func) {
    const uint32_t num_entries = darray_get_num_items(entries);
    if (num_entries == 0) {
        return NULL;
    }
    const uint32_t num_threads =
        num_entries < THRESHOLD_FOR_PARALLEL_SEARCH ? 1 : fsearch_thread_pool_get_num_threads(q->pool);
    const uint32_t num_items_per_thread = num_entries / num_threads;

    DatabaseSearchWorkerContext *thread_data[num_threads];
    memset(thread_data, 0, sizeof(thread_data));

    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;

    if (!q->token) {
        return NULL;
    }

    GList *threads = fsearch_thread_pool_get_threads(q->pool);
    for (uint32_t i = 0; i < num_threads; i++) {

        thread_data[i] = db_search_worker_context_new(q,
                                                      cancellable,
                                                      entries,
                                                      start_pos,
                                                      i == num_threads - 1 ? num_entries - 1 : end_pos);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;

        fsearch_thread_pool_push_data(q->pool, threads, search_func, thread_data[i]);
        threads = threads->next;
    }

    threads = fsearch_thread_pool_get_threads(q->pool);
    while (threads) {
        fsearch_thread_pool_wait_for_thread(q->pool, threads);
        threads = threads->next;
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        for (uint32_t i = 0; i < num_threads; i++) {
            g_clear_pointer(&thread_data[i], db_search_worker_context_free);
        }
        return NULL;
    }

    // get total number of entries found
    uint32_t num_results = 0;
    for (uint32_t i = 0; i < num_threads; ++i) {
        num_results += thread_data[i]->num_results;
    }

    DynamicArray *results = darray_new(num_results);

    for (uint32_t i = 0; i < num_threads; i++) {
        DatabaseSearchWorkerContext *ctx = thread_data[i];
        if (!ctx) {
            break;
        }

        darray_add_items(results, (void **)ctx->results, ctx->num_results);

        g_clear_pointer(&ctx, db_search_worker_context_free);
    }

    return results;
}

static DatabaseSearchResult *
db_search_empty(FsearchQuery *q) {
    DatabaseSearchResult *result = db_search_result_new();
    DynamicArray *files = NULL;
    DynamicArray *folders = NULL;

    FsearchDatabaseIndexType sort_type;

    db_lock(q->db);
    db_get_entries_sorted(q->db, q->sort_order, &sort_type, &folders, &files);
    result->folders = folders;
    result->files = files;
    result->db = db_ref(q->db);
    result->sort_type = sort_type;
    db_unlock(q->db);
    return result;
}

static DatabaseSearchResult *
db_search(FsearchQuery *q, GCancellable *cancellable) {
    DynamicArray *files_in = NULL;
    DynamicArray *folders_in = NULL;

    FsearchDatabaseIndexType sort_type;

    db_lock(q->db);
    db_get_entries_sorted(q->db, q->sort_order, &sort_type, &folders_in, &files_in);

    DynamicArray *files_res = NULL;
    DynamicArray *folders_res = NULL;

    const uint32_t num_folders = folders_in ? darray_get_num_items(folders_in) : 0;
    folders_res = num_folders > 0 ? db_search_entries(q, cancellable, folders_in, db_search_worker) : NULL;
    g_clear_pointer(&folders_in, darray_unref);
    if (g_cancellable_is_cancelled(cancellable)) {
        goto search_was_cancelled;
    }
    const uint32_t num_files = files_in ? darray_get_num_items(files_in) : 0;
    files_res = num_files > 0 ? db_search_entries(q, cancellable, files_in, db_search_worker) : NULL;
    g_clear_pointer(&files_in, darray_unref);
    if (g_cancellable_is_cancelled(cancellable)) {
        goto search_was_cancelled;
    }

    DatabaseSearchResult *result = db_search_result_new();
    result->files = files_res;
    result->folders = folders_res;
    result->db = db_ref(q->db);
    result->sort_type = sort_type;

    db_unlock(q->db);
    return result;

search_was_cancelled:
    g_clear_pointer(&folders_res, darray_unref);
    g_clear_pointer(&files_res, darray_unref);

    db_unlock(q->db);
    return NULL;
}

void
db_search_queue(FsearchTaskQueue *queue,
                FsearchQuery *query,
                FsearchTaskFinishedFunc finished_func,
                FsearchTaskCancelledFunc cancelled_func) {
    fsearch_task_queue(queue,
                       FSEARCH_TASK_ID_SEARCH,
                       db_search_task,
                       finished_func,
                       cancelled_func,
                       FSEARCH_TASK_CLEAR_SAME_ID,
                       query);
}

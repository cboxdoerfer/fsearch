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
#include "fsearch_limits.h"
#include "fsearch_string_utils.h"
#include "fsearch_task.h"
#include "fsearch_token.h"

struct _DatabaseSearch {
    FsearchTaskQueue *queue;
};

typedef struct search_context_s {
    DynamicArray *entries;
    FsearchQuery *query;
    void **results;
    GCancellable *cancellable;
    uint32_t num_results;
    uint32_t num_folders;
    uint32_t num_files;
    uint32_t start_pos;
    uint32_t end_pos;
} search_thread_context_t;

static DatabaseSearchResult *
db_search(FsearchQuery *q, GCancellable *cancellable);

static DatabaseSearchResult *
db_search_empty(FsearchQuery *query);

static DatabaseSearchResult *
db_search_result_new(FsearchQuery *query) {
    DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
    assert(result_ctx != NULL);
    result_ctx->query = query;
    return result_ctx;
}

uint32_t
db_search_result_get_num_entries(DatabaseSearchResult *result) {
    return db_search_result_get_num_files(result) + db_search_result_get_num_folders(result);
}

uint32_t
db_search_result_get_num_files(DatabaseSearchResult *result) {
    return result->files ? darray_get_num_items(result->files) : 0;
}

uint32_t
db_search_result_get_num_folders(DatabaseSearchResult *result) {
    return result->folders ? darray_get_num_items(result->folders) : 0;
}

FsearchQuery *
db_search_result_get_query(DatabaseSearchResult *result) {
    return result->query;
}

const char *
db_search_result_get_name(DatabaseSearchResult *result, uint32_t pos) {
    void *entry = db_search_result_get_entry(result, pos);
    if (!entry) {
        return NULL;
    }
    return db_entry_get_name(entry);
}

GString *
db_search_result_get_path(DatabaseSearchResult *result, uint32_t pos) {
    void *entry = db_search_result_get_entry(result, pos);
    if (!entry) {
        return NULL;
    }
    return db_entry_get_path(entry);
}

off_t
db_search_result_get_size(DatabaseSearchResult *result, uint32_t pos) {
    void *entry = db_search_result_get_entry(result, pos);
    if (!entry) {
        return 0;
    }
    return db_entry_get_size(entry);
}

void *
db_search_result_get_entry(DatabaseSearchResult *result, uint32_t pos) {
    uint32_t num_entries = db_search_result_get_num_entries(result);
    if (pos >= num_entries) {
        return NULL;
    }
    uint32_t num_folders = db_search_result_get_num_folders(result);
    if (pos < num_folders) {
        return darray_get_item(result->folders, pos);
    }
    else {
        return darray_get_item(result->files, pos - num_folders);
    }
}

FsearchDatabaseEntryType
db_search_result_get_type(DatabaseSearchResult *result, uint32_t pos) {
    uint32_t num_entries = db_search_result_get_num_entries(result);
    if (pos >= num_entries) {
        return DATABASE_ENTRY_TYPE_NONE;
    }
    uint32_t num_folders = db_search_result_get_num_folders(result);
    if (pos < num_folders) {
        return DATABASE_ENTRY_TYPE_FOLDER;
    }
    else {
        return DATABASE_ENTRY_TYPE_FILE;
    }
}

static gpointer
db_search_task(gpointer data, GCancellable *cancellable) {
    FsearchQuery *query = data;

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    DatabaseSearchResult *result = NULL;
    if (fs_str_is_empty(query->text) && !query->pass_on_empty_query) {
        result = db_search_result_new(query);
    }
    else if (fsearch_query_matches_everything(query)) {
        result = db_search_empty(query);
    }
    else {
        result = db_search(query, cancellable);
    }

    const char *debug_message = NULL;
    const double seconds = g_timer_elapsed(timer, NULL);
    if (!g_cancellable_is_cancelled(cancellable)) {
        debug_message = "[query %d.%d] finished in %.2f ms";
    }
    else {
        debug_message = "[query %d.%d] aborted after %.2f ms";
    }
    g_timer_stop(timer);
    g_timer_destroy(timer);
    timer = NULL;

    g_debug(debug_message, query->window_id, query->id, seconds * 1000);

    return result;
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

    g_free(ctx);
    ctx = NULL;
}

static search_thread_context_t *
search_thread_context_new(DynamicArray *entries,
                          FsearchQuery *query,
                          GCancellable *cancellable,
                          uint32_t start_pos,
                          uint32_t end_pos) {
    search_thread_context_t *ctx = calloc(1, sizeof(search_thread_context_t));
    assert(ctx != NULL);
    assert(end_pos >= start_pos);

    ctx->entries = entries;
    ctx->query = query;
    ctx->cancellable = cancellable;
    ctx->results = calloc(end_pos - start_pos + 1, sizeof(void *));
    assert(ctx->results != NULL);

    ctx->num_results = 0;
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static inline bool
filter_entry(FsearchDatabaseEntry *entry, FsearchQuery *query, const char *haystack) {
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

static void
db_search_worker(search_thread_context_t *ctx, DynamicArray *entries) {
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    FsearchQuery *query = ctx->query;
    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t num_token = query->num_token;
    FsearchToken **token = query->token;
    const uint32_t search_in_path = query->flags.search_in_path;
    const uint32_t auto_search_in_path = query->flags.auto_search_in_path;
    FsearchDatabaseEntry **results = (FsearchDatabaseEntry **)ctx->results;

    if (!entries) {
        ctx->num_results = 0;
        g_debug("[db_search] entries empty");
        return;
    }

    uint32_t num_results = 0;
    uint32_t num_files = 0;
    uint32_t num_folders = 0;

    bool path_set = false;

    GString *path_string = g_string_sized_new(PATH_MAX);
    for (uint32_t i = start; i <= end; i++) {
        if (g_cancellable_is_cancelled(ctx->cancellable)) {
            return;
        }
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        if (!entry) {
            continue;
        }
        const char *haystack_name = db_entry_get_name(entry);
        if (!haystack_name) {
            continue;
        }
        if (search_in_path || query->filter->search_in_path) {
            g_string_truncate(path_string, 0);
            db_entry_append_path(entry, path_string);
            g_string_append_c(path_string, G_DIR_SEPARATOR);
            g_string_append(path_string, haystack_name);
            path_set = true;
        }

        if (!filter_entry(entry, query, query->filter->search_in_path ? path_string->str : haystack_name)) {
            continue;
        }

        uint32_t num_found = 0;
        while (true) {
            if (num_found == num_token) {
                results[num_results] = entry;
                num_results++;
                num_files++;
                break;
            }
            FsearchToken *t = token[num_found++];
            if (!t) {
                break;
            }

            const char *haystack = NULL;
            if (search_in_path || (auto_search_in_path && t->has_separator)) {
                if (!path_set) {
                    g_string_truncate(path_string, 0);
                    db_entry_append_path(entry, path_string);
                    g_string_append_c(path_string, G_DIR_SEPARATOR);
                    g_string_append(path_string, haystack_name);
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

    return;
}

static void *
db_search_folders_worker(void *user_data) {
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    db_search_worker(ctx, ctx->query->folders);
    return NULL;
}

static void *
db_search_files_worker(void *user_data) {
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    db_search_worker(ctx, ctx->query->files);
    return NULL;
}

static DatabaseSearchResult *
db_search_empty(FsearchQuery *query) {
    assert(query != NULL);
    assert(query->folders != NULL);
    assert(query->files != NULL);

    const uint32_t num_folders = darray_get_num_items(query->folders);
    const uint32_t num_files = darray_get_num_items(query->files);

    DynamicArray *files = darray_new(num_files);
    DynamicArray *folders = darray_new(num_folders);

    void **folders_data = darray_get_data(query->folders, NULL);
    void **files_data = darray_get_data(query->files, NULL);
    darray_add_items(files, files_data, num_files);
    darray_add_items(folders, folders_data, num_folders);

    DatabaseSearchResult *result = db_search_result_new(query);
    result->files = files;
    result->num_files = num_files;
    result->folders = folders;
    result->num_folders = num_folders;
    return result;
}

static DynamicArray *
db_search_entries(FsearchQuery *q, GCancellable *cancellable, DynamicArray *entries, void *(*search_func)(void *)) {
    const uint32_t num_entries = darray_get_num_items(entries);
    const uint32_t num_threads = MIN(fsearch_thread_pool_get_num_threads(q->pool), num_entries);
    const uint32_t num_items_per_thread = num_entries / num_threads;

    search_thread_context_t *thread_data[num_threads];
    memset(thread_data, 0, num_threads * sizeof(search_thread_context_t *));

    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;

    if (!q->token) {
        return NULL;
    }

    GList *threads = fsearch_thread_pool_get_threads(q->pool);
    for (uint32_t i = 0; i < num_threads; i++) {
        thread_data[i] = search_thread_context_new(entries,
                                                   q,
                                                   cancellable,
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

    return results;
}

static DatabaseSearchResult *
db_search(FsearchQuery *q, GCancellable *cancellable) {
    const uint32_t num_folder_entries = darray_get_num_items(q->folders);
    const uint32_t num_file_entries = darray_get_num_items(q->files);
    if (num_folder_entries == 0 && num_file_entries == 0) {
        return db_search_result_new(q);
    }

    DynamicArray *files = NULL;
    DynamicArray *folders = NULL;

    folders = db_search_entries(q, cancellable, q->folders, db_search_folders_worker);
    if (g_cancellable_is_cancelled(cancellable)) {
        goto search_was_cancelled;
    }
    files = db_search_entries(q, cancellable, q->files, db_search_files_worker);
    if (g_cancellable_is_cancelled(cancellable)) {
        goto search_was_cancelled;
    }

    DatabaseSearchResult *result = db_search_result_new(q);
    if (files) {
        result->files = files;
        result->num_files = darray_get_num_items(files);
    }
    if (folders) {
        result->folders = folders;
        result->num_folders = darray_get_num_items(folders);
    }

    return result;

search_was_cancelled:
    if (folders) {
        darray_free(folders);
        folders = NULL;
    }
    if (files) {
        darray_free(files);
        files = NULL;
    }
    return NULL;
}

void
db_search_result_free(DatabaseSearchResult *result) {
    if (!result) {
        return;
    }

    if (result->folders) {
        darray_free(result->folders);
        result->folders = NULL;
    }
    if (result->files) {
        darray_free(result->files);
        result->files = NULL;
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
db_search_queue(FsearchTaskQueue *queue,
                FsearchQuery *query,
                FsearchTaskFinishedFunc finished_func,
                FsearchTaskCancelledFunc cancelled_func) {
    FsearchTask *task = fsearch_task_new(0, db_search_task, finished_func, cancelled_func, query);
    fsearch_task_queue(queue, task, FSEARCH_TASK_CLEAR_ALL);
}

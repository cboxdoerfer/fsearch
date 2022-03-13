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

#define G_LOG_DOMAIN "fsearch-thread-pool"

#include <stdio.h>

#include "fsearch_limits.h"
#include "fsearch_thread_pool.h"

struct FsearchThreadPool {
    GList *threads;
    uint32_t num_threads;
};

typedef struct {
    GThread *thread;
    FsearchThreadPoolFunc thread_func;

    gpointer *thread_data;

    GMutex mutex;
    GCond start_cond;
    GCond finished_cond;
    FsearchThreadStatus status;
    bool terminate;
} FsearchThreadPoolContext;

static bool
thread_pool_has_thread(FsearchThreadPool *pool, GList *thread) {
    GList *temp = pool->threads;
    while (temp) {
        if (temp == thread) {
            return true;
        }
        temp = temp->next;
    }
    return false;
}

static gpointer
fsearch_thread_pool_thread(gpointer user_data) {
    FsearchThreadPoolContext *ctx = user_data;

    g_mutex_lock(&ctx->mutex);
    while (!ctx->terminate) {
        g_cond_wait(&ctx->start_cond, &ctx->mutex);
        ctx->status = THREAD_BUSY;
        if (ctx->thread_data) {
            ctx->thread_func(ctx->thread_data);
            ctx->status = THREAD_FINISHED;
            ctx->thread_data = NULL;
            g_cond_signal(&ctx->finished_cond);
        }
        ctx->status = THREAD_IDLE;
    }
    g_mutex_unlock(&ctx->mutex);
    return NULL;
}

static void
thread_context_free(FsearchThreadPoolContext *ctx) {
    g_return_if_fail(ctx);

    g_mutex_lock(&ctx->mutex);
    if (ctx->thread_data) {
        g_debug("[thread_pool] search data still there");
    }

    // terminate thread
    ctx->terminate = true;
    g_cond_signal(&ctx->start_cond);
    g_mutex_unlock(&ctx->mutex);
    g_thread_join(g_steal_pointer(&ctx->thread));

    g_mutex_clear(&ctx->mutex);
    g_cond_clear(&ctx->start_cond);
    g_cond_clear(&ctx->finished_cond);

    g_clear_pointer(&ctx, g_free);
}

static FsearchThreadPoolContext *
thread_context_new(void) {
    FsearchThreadPoolContext *ctx = g_new0(FsearchThreadPoolContext, 1);
    g_assert_nonnull(ctx);

    ctx->thread_data = NULL;
    ctx->thread_func = NULL;
    ctx->terminate = false;
    ctx->status = THREAD_IDLE;
    g_mutex_init(&ctx->mutex);
    g_cond_init(&ctx->start_cond);
    g_cond_init(&ctx->finished_cond);

    ctx->thread = g_thread_new("thread pool", fsearch_thread_pool_thread, ctx);
    return ctx;
}

FsearchThreadPool *
fsearch_thread_pool_init(void) {
    FsearchThreadPool *pool = g_new0(FsearchThreadPool, 1);
    pool->threads = NULL;
    pool->num_threads = 0;

    uint32_t num_cpus = MIN(g_get_num_processors(), FSEARCH_THREAD_LIMIT);
    for (uint32_t i = 0; i < num_cpus; i++) {
        FsearchThreadPoolContext *ctx = thread_context_new();
        if (ctx) {
            pool->threads = g_list_prepend(pool->threads, ctx);
            pool->num_threads++;
        }
    }

    return pool;
}

void
fsearch_thread_pool_free(FsearchThreadPool *pool) {
    g_return_if_fail(pool);

    g_list_free_full(g_steal_pointer(&pool->threads), (GDestroyNotify)thread_context_free);
    g_clear_pointer(&pool, g_free);
}

GList *
fsearch_thread_pool_get_threads(FsearchThreadPool *pool) {
    g_return_val_if_fail(pool, NULL);
    return pool->threads;
}

gpointer
fsearch_thread_pool_get_data(FsearchThreadPool *pool, GList *thread) {
    if (!pool || !thread) {
        return NULL;
    }
    if (!thread_pool_has_thread(pool, thread)) {
        return NULL;
    }
    FsearchThreadPoolContext *ctx = thread->data;
    if (!ctx) {
        return NULL;
    }
    return ctx->thread_data;
}

bool
fsearch_thread_pool_task_is_idle(FsearchThreadPool *pool, GList *thread) {
    bool res = false;
    if (!thread_pool_has_thread(pool, thread)) {
        return res;
    }
    FsearchThreadPoolContext *ctx = thread->data;
    if (!ctx) {
        return res;
    }

    res = ctx->status == THREAD_IDLE ? true : false;

    return res;
}

bool
fsearch_thread_pool_task_is_busy(FsearchThreadPool *pool, GList *thread) {
    bool res = false;
    if (!thread_pool_has_thread(pool, thread)) {
        return res;
    }
    FsearchThreadPoolContext *ctx = thread->data;
    if (!ctx) {
        return res;
    }

    res = ctx->status == THREAD_BUSY ? true : false;

    return res;
}

bool
fsearch_thread_pool_wait_for_thread(FsearchThreadPool *pool, GList *thread) {
    FsearchThreadPoolContext *ctx = thread->data;
    g_mutex_lock(&ctx->mutex);
    while (fsearch_thread_pool_task_is_busy(pool, thread)) {
        g_debug("[thread_pool] busy, waiting...");
        g_cond_wait(&ctx->finished_cond, &ctx->mutex);
        g_debug("[thread_pool] continue...");
    }
    g_mutex_unlock(&ctx->mutex);
    return true;
}

uint32_t
fsearch_thread_pool_get_num_threads(FsearchThreadPool *pool) {
    g_return_val_if_fail(pool, 0);
    return pool->num_threads;
}

bool
fsearch_thread_pool_push_data(FsearchThreadPool *pool,
                              GList *thread,
                              FsearchThreadPoolFunc thread_func,
                              gpointer thread_data) {
    if (!pool || !thread || !thread_func || !thread_data) {
        return false;
    }
    if (!thread_pool_has_thread(pool, thread)) {
        return false;
    }
    FsearchThreadPoolContext *ctx = thread->data;
    g_mutex_lock(&ctx->mutex);
    ctx->thread_func = thread_func;
    ctx->thread_data = thread_data;
    ctx->status = THREAD_BUSY;

    g_cond_signal(&ctx->start_cond);
    g_mutex_unlock(&ctx->mutex);
    return true;
}

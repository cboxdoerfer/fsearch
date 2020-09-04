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

#include <stdio.h>

#include "debug.h"
#include "fsearch_thread_pool.h"

struct _FsearchThreadPool {
    GList *threads;
    uint32_t num_threads;
};

typedef struct thread_context_s {
    GThread *thread;
    gpointer (*thread_func)(gpointer thread_data);

    gpointer *thread_data;

    GMutex mutex;
    GCond start_cond;
    GCond finished_cond;
    FsearchThreadStatus status;
    bool terminate;
} thread_context_t;

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
    thread_context_t *ctx = user_data;

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
thread_context_free(thread_context_t *ctx) {
    if (!ctx) {
        return;
    }

    g_mutex_lock(&ctx->mutex);
    if (ctx->thread_data) {
        trace("[thread_pool] search data still there\n");
    }

    // terminate thread
    ctx->terminate = true;
    g_cond_signal(&ctx->start_cond);
    g_mutex_unlock(&ctx->mutex);
    g_thread_join(ctx->thread);

    g_mutex_clear(&ctx->mutex);
    g_cond_clear(&ctx->start_cond);
    g_cond_clear(&ctx->finished_cond);
    g_free(ctx);
    ctx = NULL;
}

static thread_context_t *
thread_context_new(void) {
    thread_context_t *ctx = g_new0(thread_context_t, 1);
    if (!ctx) {
        return NULL;
    }
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

    uint32_t num_cpus = g_get_num_processors();
    for (uint32_t i = 0; i < num_cpus; i++) {
        thread_context_t *ctx = thread_context_new();
        if (ctx) {
            pool->threads = g_list_prepend(pool->threads, ctx);
            pool->num_threads++;
        }
    }

    return pool;
}

void
fsearch_thread_pool_free(FsearchThreadPool *pool) {
    if (!pool) {
        return;
    }
    GList *thread = pool->threads;
    for (uint32_t i = 0; thread && i < pool->num_threads; i++) {
        thread_context_t *ctx = thread->data;
        thread_context_free(ctx);
        thread = thread->next;
    }
    pool->num_threads = 0;
    g_free(pool);
    pool = NULL;
}

GList *
fsearch_thread_pool_get_threads(FsearchThreadPool *pool) {
    if (!pool) {
        return NULL;
    }
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
    thread_context_t *ctx = thread->data;
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
    thread_context_t *ctx = thread->data;
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
    thread_context_t *ctx = thread->data;
    if (!ctx) {
        return res;
    }

    res = ctx->status == THREAD_BUSY ? true : false;

    return res;
}

bool
fsearch_thread_pool_wait_for_thread(FsearchThreadPool *pool, GList *thread) {
    thread_context_t *ctx = thread->data;
    g_mutex_lock(&ctx->mutex);
    while (fsearch_thread_pool_task_is_busy(pool, thread)) {
        trace("[thread_pool] busy, waiting...\n");
        g_cond_wait(&ctx->finished_cond, &ctx->mutex);
        trace("[thread_pool] continue...\n");
    }
    g_mutex_unlock(&ctx->mutex);
    return true;
}

uint32_t
fsearch_thread_pool_get_num_threads(FsearchThreadPool *pool) {
    if (!pool) {
        return 0;
    }
    return pool->num_threads;
}

bool
fsearch_thread_pool_push_data(FsearchThreadPool *pool,
                              GList *thread,
                              ThreadFunc thread_func,
                              gpointer thread_data) {
    if (!pool || !thread || !thread_func || !thread_data) {
        return false;
    }
    if (!thread_pool_has_thread(pool, thread)) {
        return false;
    }
    thread_context_t *ctx = thread->data;
    g_mutex_lock(&ctx->mutex);
    ctx->thread_func = thread_func;
    ctx->thread_data = thread_data;
    ctx->status = THREAD_BUSY;

    g_cond_signal(&ctx->start_cond);
    g_mutex_unlock(&ctx->mutex);
    return true;
}


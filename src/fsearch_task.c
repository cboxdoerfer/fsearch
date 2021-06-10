#define G_LOG_DOMAIN "fsearch-task"

#include "fsearch_task.h"

#include <assert.h>
#include <stdbool.h>

typedef struct FsearchTask {
    enum {
        FSEARCH_TASK_TYPE_QUIT,
        FSEARCH_TASK_TYPE_NORMAL,
    } type;
    int id;
    GCancellable *task_cancellable;
    FsearchTaskFunc task_func;
    FsearchTaskFinishedFunc task_finished_func;
    FsearchTaskCancelledFunc task_cancelled_func;
    gpointer data;
} FsearchTask;

struct FsearchTaskQueue {
    GAsyncQueue *queue;
    GThread *queue_thread;
    FsearchTask *current_task;
    GMutex current_task_lock;
};

static void
fsearch_task_free(FsearchTask *task) {
    g_object_unref(task->task_cancellable);
    task->task_cancellable = NULL;

    free(task);
    task = NULL;
}

static FsearchTask *
fsearch_task_new(int id,
                 FsearchTaskFunc task_func,
                 FsearchTaskFinishedFunc task_finished_func,
                 FsearchTaskCancelledFunc task_cancelled_func,
                 gpointer data) {
    FsearchTask *task = calloc(1, sizeof(FsearchTask));
    g_assert(task != NULL);
    task->type = FSEARCH_TASK_TYPE_NORMAL;
    task->task_cancellable = g_cancellable_new();
    task->task_func = task_func;
    task->task_finished_func = task_finished_func;
    task->task_cancelled_func = task_cancelled_func;
    task->data = data;
    task->id = id;

    return task;
}

static gpointer
fsearch_task_queue_thread(FsearchTaskQueue *queue) {
    while (true) {
        FsearchTask *task = g_async_queue_pop(queue->queue);
        if (!task) {
            continue;
        }
        if (task->type == FSEARCH_TASK_TYPE_QUIT) {
            // quit task queue thread
            g_debug("[queue_thread] quit");
            free(task);
            task = NULL;
            break;
        }
        g_mutex_lock(&queue->current_task_lock);
        queue->current_task = task;
        g_mutex_unlock(&queue->current_task_lock);

        g_cancellable_reset(task->task_cancellable);
        gpointer result = task->task_func(task->data, task->task_cancellable);

        g_mutex_lock(&queue->current_task_lock);
        queue->current_task = NULL;
        g_mutex_unlock(&queue->current_task_lock);

        g_cancellable_reset(task->task_cancellable);
        task->task_finished_func(result, task->data);

        fsearch_task_free(task);
        task = NULL;
    }
    return NULL;
}

void
fsearch_task_queue_cancel_current(FsearchTaskQueue *queue) {
    g_mutex_lock(&queue->current_task_lock);
    if (queue->current_task) {
        g_cancellable_cancel(queue->current_task->task_cancellable);
    }
    g_mutex_unlock(&queue->current_task_lock);
}

static void
fsearch_task_queue_clear(FsearchTaskQueue *queue, FsearchTaskQueueClearPolicy clear_policy, int id) {
    if (clear_policy == FSEARCH_TASK_CLEAR_NONE) {
        return;
    }

    GQueue *task_queue = g_queue_new();

    g_async_queue_lock(queue->queue);
    while (true) {
        // clear all queued tasks
        FsearchTask *task = g_async_queue_try_pop_unlocked(queue->queue);
        if (!task) {
            break;
        }
        if (clear_policy == FSEARCH_TASK_CLEAR_SAME_ID && task->id != id) {
            // remember tasks which need to be inserted back into the async queue later
            g_queue_push_tail(task_queue, task);
            continue;
        }

        if (task->task_cancelled_func) {
            task->task_cancelled_func(task->data);
        }
        fsearch_task_free(task);
        task = NULL;
    }

    // insert all the tasks back into the async queue, which still need to be processed
    while (true) {
        FsearchTask *task = g_queue_pop_head(task_queue);
        if (task) {
            g_async_queue_push_unlocked(queue->queue, task);
        }
        else {
            break;
        }
    }

    g_async_queue_unlock(queue->queue);

    g_queue_free(task_queue);
    task_queue = NULL;
}

void
fsearch_task_queue_free(FsearchTaskQueue *queue) {
    assert(queue != NULL);

    fsearch_task_queue_clear(queue, FSEARCH_TASK_CLEAR_ALL, -1);

    g_mutex_lock(&queue->current_task_lock);
    if (queue->current_task) {
        g_cancellable_cancel(queue->current_task->task_cancellable);
    }
    g_mutex_unlock(&queue->current_task_lock);

    FsearchTask *task = calloc(1, sizeof(FsearchTask));
    g_assert(task != NULL);
    task->type = FSEARCH_TASK_TYPE_QUIT;
    g_async_queue_push(queue->queue, task);

    g_thread_join(queue->queue_thread);
    queue->queue_thread = NULL;

    g_mutex_clear(&queue->current_task_lock);

    g_async_queue_unref(queue->queue);
    queue->queue = NULL;

    g_free(queue);
    queue = NULL;

    return;
}

FsearchTaskQueue *
fsearch_task_queue_new(const char *name) {
    FsearchTaskQueue *queue = calloc(1, sizeof(FsearchTaskQueue));
    assert(queue != NULL);

    queue->queue = g_async_queue_new();
    queue->queue_thread = g_thread_new(name, (GThreadFunc)fsearch_task_queue_thread, queue);

    g_mutex_init(&queue->current_task_lock);

    return queue;
}

void
fsearch_task_queue(FsearchTaskQueue *queue,
                   gint id,
                   FsearchTaskFunc task_func,
                   FsearchTaskFinishedFunc task_finished_func,
                   FsearchTaskCancelledFunc task_cancelled_func,
                   FsearchTaskQueueClearPolicy clear_policy,
                   gpointer data) {
    FsearchTask *task = fsearch_task_new(id, task_func, task_finished_func, task_cancelled_func, data);
    if (clear_policy != FSEARCH_TASK_CLEAR_NONE) {
        fsearch_task_queue_clear(queue, clear_policy, task->id);
        g_mutex_lock(&queue->current_task_lock);
        if (queue->current_task) {
            if (clear_policy != FSEARCH_TASK_CLEAR_SAME_ID || queue->current_task->id == task->id) {
                g_cancellable_cancel(queue->current_task->task_cancellable);
            }
        }
        g_mutex_unlock(&queue->current_task_lock);
    }
    g_async_queue_push(queue->queue, task);
}


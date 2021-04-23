#include "fsearch_task.h"

#include <assert.h>
#include <stdbool.h>

struct _FsearchTask {
    GAsyncQueue *task_queue;
    GThread *task_thread;
    GCancellable *task_cancellable;
    GCancellable *task_thread_cancellable;

    FsearchTaskFunc task_func;
    FsearchTaskFunc task_cancelled_func;
    gpointer data;
};

static gpointer
fsearch_task_thread(FsearchTask *task) {
    while (true) {
        if (g_cancellable_is_cancelled(task->task_thread_cancellable)) {
            break;
        }

        gpointer user_data = g_async_queue_timeout_pop(task->task_queue, 500000);
        if (!user_data) {
            continue;
        }
        g_cancellable_reset(task->task_cancellable);
        task->task_func(task->data, user_data, task->task_cancellable);
    }
    return NULL;
}

static void
fsearch_task_queue_clear(FsearchTask *task) {
    while (true) {
        // clear all queued tasks
        gpointer user_data = g_async_queue_try_pop(task->task_queue);
        if (!user_data) {
            break;
        }
        if (task->task_cancelled_func) {
            task->task_cancelled_func(task->data, user_data, NULL);
        }
    }
}

void
fsearch_task_free(FsearchTask *task) {
    assert(task != NULL);

    fsearch_task_queue_clear(task);

    g_cancellable_cancel(task->task_thread_cancellable);
    g_thread_join(task->task_thread);
    task->task_thread = NULL;

    g_async_queue_unref(task->task_queue);
    task->task_queue = NULL;

    g_object_unref(task->task_cancellable);
    task->task_cancellable = NULL;

    g_object_unref(task->task_thread_cancellable);
    task->task_thread_cancellable = NULL;

    g_free(task);
    task = NULL;

    return;
}

FsearchTask *
fsearch_task_new(const char *thread_name,
                 FsearchTaskFunc task_func,
                 FsearchTaskFunc task_cancelled_func,
                 gpointer data) {
    FsearchTask *task = calloc(1, sizeof(FsearchTask));
    assert(task != NULL);

    task->task_func = task_func;
    task->task_cancelled_func = task_cancelled_func;
    task->data = data;

    task->task_queue = g_async_queue_new();
    task->task_cancellable = g_cancellable_new();
    task->task_thread_cancellable = g_cancellable_new();
    task->task_thread = g_thread_new(thread_name, (GThreadFunc)fsearch_task_thread, task);

    return task;
}

void
fsearch_task_queue(FsearchTask *task, gpointer user_data) {
    fsearch_task_queue_clear(task);
    g_cancellable_cancel(task->task_cancellable);
    g_async_queue_push(task->task_queue, user_data);
}

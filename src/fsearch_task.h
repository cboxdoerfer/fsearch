
#pragma once

#include <gio/gio.h>
#include <glib.h>

typedef struct _FsearchTaskQueue FsearchTaskQueue;
typedef struct _FsearchTask FsearchTask;

typedef gpointer (*FsearchTaskFunc)(gpointer data, GCancellable *cancellable);
typedef void (*FsearchTaskCancelledFunc)(FsearchTask *task, gpointer data);
typedef void (*FsearchTaskFinishedFunc)(FsearchTask *task, gpointer result, gpointer data);

typedef enum {
    FSEARCH_TASK_CLEAR_NONE = -1,
    FSEARCH_TASK_CLEAR_SAME_ID,
    FSEARCH_TASK_CLEAR_ALL,
} FsearchTaskQueueClearPolicy;

void
fsearch_task_queue_free(FsearchTaskQueue *queue);

FsearchTaskQueue *
fsearch_task_queue_new(const char *name);

void
fsearch_task_queue(FsearchTaskQueue *queue, FsearchTask *task, FsearchTaskQueueClearPolicy clear_policy);

void
fsearch_task_queue_cancel_current(FsearchTaskQueue *queue);

void
fsearch_task_free(FsearchTask *task);

FsearchTask *
fsearch_task_new(int id,
                 FsearchTaskFunc task_func,
                 FsearchTaskFinishedFunc task_finished_func,
                 FsearchTaskCancelledFunc task_cancelled_func,
                 gpointer data);

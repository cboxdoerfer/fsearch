
#pragma once

#include <gio/gio.h>
#include <glib.h>

typedef struct _FsearchTaskQueue FsearchTaskQueue;

typedef gpointer (*FsearchTaskFunc)(gpointer data, GCancellable *cancellable);
typedef void (*FsearchTaskCancelledFunc)(gpointer data);
typedef void (*FsearchTaskFinishedFunc)(gpointer result, gpointer data);

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
fsearch_task_queue(FsearchTaskQueue *queue,
                   gint id,
                   FsearchTaskFunc task_func,
                   FsearchTaskFinishedFunc task_finished_func,
                   FsearchTaskCancelledFunc task_cancelled_func,
                   FsearchTaskQueueClearPolicy clear_policy,
                   gpointer data);

void
fsearch_task_queue_cancel_current(FsearchTaskQueue *queue);

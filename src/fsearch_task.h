
#pragma once

#include <gio/gio.h>
#include <glib.h>

typedef struct _FsearchTask FsearchTask;

typedef void (*FsearchTaskFunc)(gpointer data, gpointer user_data, GCancellable *cancellable);

void
fsearch_task_free(FsearchTask *task);

FsearchTask *
fsearch_task_new(const char *name, FsearchTaskFunc task_func, FsearchTaskFunc task_cancelled_func, gpointer data);

void
fsearch_task_queue(FsearchTask *task, gpointer user_data);

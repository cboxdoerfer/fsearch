#include "fsearch_main_context_utils.h"

#include <glib.h>
#include <glibconfig.h>
#include <stddef.h>

typedef struct {
    FsearchMainContextFunc user_func;
    gpointer user_data;
    GMutex mutex;
    GCond cond;
    gboolean done;
} BlockingCallContext;

static gboolean
blocking_wrapper_func(gpointer data) {
    BlockingCallContext *task = data;

    task->user_func(task->user_data);

    // Signal completion
    g_mutex_lock(&task->mutex);
    task->done = TRUE;
    g_cond_signal(&task->cond);
    g_mutex_unlock(&task->mutex);

    return G_SOURCE_REMOVE;
}

void
fsearch_main_context_blocking_call(GMainContext *context, FsearchMainContextFunc func, gpointer data) {
    g_return_if_fail(context);
    g_return_if_fail(func);

    // If the current thread owns the context we have to run func directly
    if (g_main_context_is_owner(context)) {
        func(data);
        return;
    }

    // Ensure that someone else is running this context. Otherwise we'll deadlock.
    if (g_main_context_acquire (context)) {
        g_main_context_release (context);
        g_warning ("fsearch_utils_main_context_invoke_sync: Target context has no running loop.");
        return;
    }

    BlockingCallContext task;

    // Initialize synchronization primitives
    g_mutex_init(&task.mutex);
    g_cond_init(&task.cond);
    task.user_func = func;
    task.user_data = data;
    task.done = FALSE;

    // Lock and dispatch
    g_mutex_lock(&task.mutex);

    g_main_context_invoke(context, blocking_wrapper_func, &task);

    // Wait for the 'done' predicate to become true
    while (!task.done) {
        g_cond_wait(&task.cond, &task.mutex);
    }

    g_mutex_unlock(&task.mutex);

    // Cleanup
    g_mutex_clear(&task.mutex);
    g_cond_clear(&task.cond);
}
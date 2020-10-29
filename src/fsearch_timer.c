#include <assert.h>

#include "debug.h"
#include "fsearch_timer.h"

GTimer *
fsearch_timer_start() {
    GTimer *t = g_timer_new();
    g_timer_start(t);
    return t;
}

void
fsearch_timer_elapsed(GTimer *t, const char *format) {
#ifdef DEBUG
    gulong microseconds;
    double seconds = g_timer_elapsed(t, &microseconds);
    trace(format, seconds * 1000);
#endif
}

void
fsearch_timer_stop(GTimer *t, const char *format) {
    assert(t != NULL);
    g_timer_stop(t);
    fsearch_timer_elapsed(t, format);
    g_timer_destroy(t);
    t = NULL;
}


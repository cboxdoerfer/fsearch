#pragma once

#include <glib.h>

typedef void
(*FsearchMainContextFunc)(gpointer data);

void
fsearch_main_context_blocking_call(GMainContext *context, FsearchMainContextFunc func, gpointer data);
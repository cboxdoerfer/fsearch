#pragma once

#include <gtk/gtk.h>

#include "fsearch_filter.h"

typedef struct FsearchFilterEditor FsearchFilterEditor;
typedef void(FsearchFilterEditorResponse)(FsearchFilter *, char *, char *, char *, FsearchQueryFlags, gpointer);

void
fsearch_filter_editor_run(const char *title,
                          GtkWindow *parent_window,
                          FsearchFilter *filter,
                          FsearchFilterEditorResponse callback,
                          gpointer data);

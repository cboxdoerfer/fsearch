#pragma once

#include <gtk/gtk.h>

#include "fsearch_preferences_ui.h"

GList *
pref_include_treeview_data_get(GtkTreeView *view);

GList *
pref_exclude_treeview_data_get(GtkTreeView *view);

void
pref_treeview_row_remove(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata);

void
pref_include_treeview_row_add(GtkTreeModel *model, const char *path);

void
pref_exclude_treeview_row_add(GtkTreeModel *model, const char *path);

GtkTreeModel *
pref_include_treeview_init(GtkTreeView *view, GList *locations);

GtkTreeModel *
pref_exclude_treeview_init(GtkTreeView *view, GList *locations);


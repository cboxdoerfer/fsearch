#pragma once

#include <gtk/gtk.h>

#include "fsearch_preferences_ui.h"

GList *
pref_index_treeview_data_get(GtkTreeView *view);

GList *
pref_exclude_treeview_data_get(GtkTreeView *view);

void
pref_treeview_row_remove(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata);

void
pref_index_treeview_row_add(GtkTreeModel *index_model, const char *path);

void
pref_exclude_treeview_row_add(GtkTreeModel *model, const char *path);

GtkTreeModel *
pref_index_treeview_init(GtkTreeView *view, GList *locations);

GtkTreeModel *
pref_exclude_treeview_init(GtkTreeView *view, GList *locations);


#include <glib/gi18n.h>

#include "fsearch_preferences_widgets.h"

#include "fsearch_exclude_path.h"
#include "fsearch_include_path.h"

enum { COL_INCLUDE_ENABLE, COL_INCLUDE_PATH, COL_INCLUDE_UPDATE, NUM_INCLUDE_COLUMNS };

enum { COL_EXCLUDE_ENABLE, COL_EXCLUDE_PATH, NUM_EXCLUDE_COLUMNS };

static void
on_exclude_model_modified(GtkTreeModel *model, GtkTreePath *path, gpointer user_data) {
    FsearchPreferences *pref = user_data;
    pref->update_db = true;
}

static void
on_include_model_modified(GtkTreeModel *model, GtkTreePath *path, gpointer user_data) {
    FsearchPreferences *pref = user_data;
    pref->update_db = true;
}

static void
column_text_append(GtkTreeView *view, const char *name, gboolean expand, int id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(name, renderer, "text", id, NULL);
    gtk_tree_view_column_set_expand(col, expand);
    gtk_tree_view_column_set_sort_column_id(col, id);
    gtk_tree_view_append_column(view, col);
}

static bool
on_column_toggled(gchar *path_str, GtkTreeModel *model, int col) {
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    gboolean val;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, col, &val, -1);

    val ^= 1;

    gtk_list_store_set(GTK_LIST_STORE(model), &iter, col, val, -1);
    gtk_tree_path_free(path);

    return val;
}

static void
on_column_exclude_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    FsearchPreferences *pref = data;
    on_column_toggled(path_str, pref->exclude_model, COL_EXCLUDE_ENABLE);
    pref->update_db = true;
}

static void
on_column_include_enable_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    FsearchPreferences *pref = data;
    on_column_toggled(path_str, pref->include_model, COL_INCLUDE_ENABLE);
    pref->update_db = true;
}

static void
on_column_include_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    FsearchPreferences *pref = data;
    on_column_toggled(path_str, pref->include_model, COL_INCLUDE_UPDATE);
    pref->update_db = true;
}

static void
column_toggle_append(GtkTreeView *view,
                     GtkTreeModel *model,
                     const char *name,
                     int id,
                     GCallback cb,
                     gpointer user_data) {
    GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "xalign", 0.0, NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(name, renderer, "active", id, NULL);
    gtk_tree_view_column_set_sort_column_id(col, id);
    gtk_tree_view_append_column(view, col);
    g_signal_connect(renderer, "toggled", cb, user_data);
}

GList *
pref_include_treeview_data_get(GtkTreeView *view) {
    GList *data = NULL;
    GtkTreeModel *model = gtk_tree_view_get_model(view);

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
        gchar *path = NULL;
        gboolean update = FALSE;
        gboolean enable = FALSE;
        gtk_tree_model_get(model,
                           &iter,
                           COL_INCLUDE_ENABLE,
                           &enable,
                           COL_INCLUDE_PATH,
                           &path,
                           COL_INCLUDE_UPDATE,
                           &update,
                           -1);

        if (path) {
            FsearchIncludePath *fs_path = fsearch_include_path_new(path, enable, update, 0, 0);
            data = g_list_append(data, fs_path);
            g_free(path);
            path = NULL;
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return data;
}

GList *
pref_exclude_treeview_data_get(GtkTreeView *view) {
    GList *data = NULL;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    if (!model) {
        return data;
    }

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
        gchar *path = NULL;
        gboolean enable = FALSE;
        gtk_tree_model_get(model, &iter, COL_EXCLUDE_PATH, &path, COL_EXCLUDE_ENABLE, &enable, -1);

        if (path) {
            FsearchExcludePath *fs_path = fsearch_exclude_path_new(path, enable);
            data = g_list_append(data, fs_path);
            g_free(path);
            path = NULL;
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return data;
}

void
pref_treeview_row_remove(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata) {
    gtk_list_store_remove(GTK_LIST_STORE(model), iter);
}

void
pref_include_treeview_row_add(FsearchPreferences *pref, const char *path) {
    FsearchIncludePath *fs_path = fsearch_include_path_new(path, true, true, 0, 0);

    GtkTreeIter iter;
    gtk_list_store_append(GTK_LIST_STORE(pref->include_model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(pref->include_model),
                       &iter,
                       COL_INCLUDE_ENABLE,
                       fs_path->enabled,
                       COL_INCLUDE_PATH,
                       fs_path->path,
                       COL_INCLUDE_UPDATE,
                       fs_path->update,
                       -1);
    pref->update_db = true;
}

void
pref_exclude_treeview_row_add(FsearchPreferences *pref, const char *path) {
    FsearchExcludePath *fs_path = fsearch_exclude_path_new(path, true);

    GtkTreeIter iter;
    gtk_list_store_append(GTK_LIST_STORE(pref->exclude_model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(pref->exclude_model),
                       &iter,
                       COL_EXCLUDE_ENABLE,
                       fs_path->enabled,
                       COL_EXCLUDE_PATH,
                       fs_path->path,
                       -1);
    pref->update_db = true;
}

void
pref_include_treeview_init(GtkTreeView *view, FsearchPreferences *pref) {
    GtkListStore *store = gtk_list_store_new(NUM_INCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));

    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("Active"),
                         COL_INCLUDE_ENABLE,
                         G_CALLBACK(on_column_include_enable_toggled),
                         pref);
    column_text_append(view, _("Path"), TRUE, COL_INCLUDE_PATH);
    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("Update"),
                         COL_INCLUDE_UPDATE,
                         G_CALLBACK(on_column_include_toggled),
                         pref);

    for (GList *l = pref->config->locations; l != NULL; l = l->next) {
        GtkTreeIter iter = {};
        FsearchIncludePath *fs_path = l->data;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store,
                           &iter,
                           COL_INCLUDE_ENABLE,
                           fs_path->enabled,
                           COL_INCLUDE_PATH,
                           fs_path->path,
                           COL_INCLUDE_UPDATE,
                           fs_path->update,
                           -1);
    }

    pref->include_model = GTK_TREE_MODEL(store);
    g_signal_connect(pref->include_model, "row-changed", G_CALLBACK(on_include_model_modified), (gpointer)pref);
    g_signal_connect(pref->include_model, "row-deleted", G_CALLBACK(on_include_model_modified), (gpointer)pref);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(view, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

void
pref_exclude_treeview_init(GtkTreeView *view, FsearchPreferences *pref) {
    GtkListStore *store = gtk_list_store_new(NUM_EXCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("Active"),
                         COL_EXCLUDE_ENABLE,
                         G_CALLBACK(on_column_exclude_toggled),
                         pref);
    column_text_append(view, _("Path"), TRUE, COL_EXCLUDE_PATH);

    for (GList *l = pref->config->exclude_locations; l != NULL; l = l->next) {
        GtkTreeIter iter = {};
        FsearchExcludePath *fs_path = l->data;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COL_EXCLUDE_ENABLE, fs_path->enabled, COL_EXCLUDE_PATH, fs_path->path, -1);
    }

    pref->exclude_model = GTK_TREE_MODEL(store);
    g_signal_connect(pref->exclude_model, "row-changed", G_CALLBACK(on_exclude_model_modified), pref);
    g_signal_connect(pref->exclude_model, "row-deleted", G_CALLBACK(on_exclude_model_modified), pref);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(view, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

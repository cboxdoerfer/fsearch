#include <glib/gi18n.h>

#include "fsearch_preferences_widgets.h"

#include "fsearch_exclude_path.h"
#include "fsearch_index.h"

enum { COL_INDEX_ENABLE, COL_INDEX_PATH, COL_INDEX_UPDATE, COL_INDEX_ONE_FS, NUM_INDEX_COLUMNS };

enum { COL_EXCLUDE_ENABLE, COL_EXCLUDE_PATH, NUM_EXCLUDE_COLUMNS };

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
    g_clear_pointer(&path, gtk_tree_path_free);

    return val;
}

static void
on_column_exclude_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *exclude_model = data;
    on_column_toggled(path_str, exclude_model, COL_EXCLUDE_ENABLE);
}

static void
on_column_index_enable_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *index_model = data;
    on_column_toggled(path_str, index_model, COL_INDEX_ENABLE);
}

static void
on_column_index_one_fs_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *index_model = data;
    on_column_toggled(path_str, index_model, COL_INDEX_ONE_FS);
}

static void
on_column_index_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *index_model = data;
    on_column_toggled(path_str, index_model, COL_INDEX_UPDATE);
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
pref_index_treeview_data_get(GtkTreeView *view) {
    GList *data = NULL;
    GtkTreeModel *model = gtk_tree_view_get_model(view);

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
        gchar *path = NULL;
        gboolean update = FALSE;
        gboolean enable = FALSE;
        gboolean one_filesystem = FALSE;
        gtk_tree_model_get(model,
                           &iter,
                           COL_INDEX_ENABLE,
                           &enable,
                           COL_INDEX_PATH,
                           &path,
                           COL_INDEX_UPDATE,
                           &update,
                           COL_INDEX_ONE_FS,
                           &one_filesystem,
                           -1);

        if (path) {
            FsearchIndex *index = fsearch_index_new(FSEARCH_INDEX_FOLDER_TYPE, path, enable, update, one_filesystem, 0);
            data = g_list_append(data, index);
            g_clear_pointer(&path, g_free);
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
            g_clear_pointer(&path, g_free);
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
pref_index_treeview_row_add(GtkTreeModel *index_model, const char *path) {
    FsearchIndex *index = fsearch_index_new(FSEARCH_INDEX_FOLDER_TYPE, path, true, true, false, 0);

    GtkTreeIter iter;
    gtk_list_store_append(GTK_LIST_STORE(index_model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(index_model),
                       &iter,
                       COL_INDEX_ENABLE,
                       index->enabled,
                       COL_INDEX_PATH,
                       index->path,
                       COL_INDEX_UPDATE,
                       index->update,
                       COL_INDEX_ONE_FS,
                       index->one_filesystem,
                       -1);
}

void
pref_exclude_treeview_row_add(GtkTreeModel *exclude_model, const char *path) {
    FsearchExcludePath *fs_path = fsearch_exclude_path_new(path, true);

    GtkTreeIter iter;
    gtk_list_store_append(GTK_LIST_STORE(exclude_model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(exclude_model),
                       &iter,
                       COL_EXCLUDE_ENABLE,
                       fs_path->enabled,
                       COL_EXCLUDE_PATH,
                       fs_path->path,
                       -1);
}

GtkTreeModel *
pref_index_treeview_init(GtkTreeView *view, GList *indexes) {
    GtkListStore *store =
        gtk_list_store_new(NUM_INDEX_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));

    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("Active"),
                         COL_INDEX_ENABLE,
                         G_CALLBACK(on_column_index_enable_toggled),
                         store);
    column_text_append(view, _("Path"), TRUE, COL_INDEX_PATH);
    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("One Filesystem"),
                         COL_INDEX_ONE_FS,
                         G_CALLBACK(on_column_index_one_fs_toggled),
                         store);
    // column_toggle_append(view,
    //                      GTK_TREE_MODEL(store),
    //                      _("Update"),
    //                      COL_INDEX_UPDATE,
    //                      G_CALLBACK(on_column_index_toggled),
    //                      store);

    for (GList *l = indexes; l != NULL; l = l->next) {
        GtkTreeIter iter = {};
        FsearchIndex *index = l->data;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store,
                           &iter,
                           COL_INDEX_ENABLE,
                           index->enabled,
                           COL_INDEX_PATH,
                           index->path,
                           COL_INDEX_UPDATE,
                           index->update,
                           COL_INDEX_ONE_FS,
                           index->one_filesystem,
                           -1);
    }

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(view, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);

    return GTK_TREE_MODEL(store);
}

GtkTreeModel *
pref_exclude_treeview_init(GtkTreeView *view, GList *locations) {
    GtkListStore *store = gtk_list_store_new(NUM_EXCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
    column_toggle_append(view,
                         GTK_TREE_MODEL(store),
                         _("Active"),
                         COL_EXCLUDE_ENABLE,
                         G_CALLBACK(on_column_exclude_toggled),
                         store);
    column_text_append(view, _("Path"), TRUE, COL_EXCLUDE_PATH);

    for (GList *l = locations; l != NULL; l = l->next) {
        GtkTreeIter iter = {};
        FsearchExcludePath *fs_path = l->data;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COL_EXCLUDE_ENABLE, fs_path->enabled, COL_EXCLUDE_PATH, fs_path->path, -1);
    }

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(view, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);

    return GTK_TREE_MODEL(store);
}


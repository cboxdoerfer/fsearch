/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "database_search.h"
#include "debug.h"
#include "fsearch.h"
#include "fsearch_limits.h"
#include "list_model.h"
#include "listview.h"

static void
on_listview_column_width_changed(GtkTreeViewColumn *col, GParamSpec *pspec, gpointer user_data) {
    int32_t id = gtk_tree_view_column_get_sort_column_id(col) + 1;
    int32_t width = gtk_tree_view_column_get_width(col);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    switch (id) {
    case LIST_MODEL_COL_NAME:
        config->name_column_width = width;
        break;
    case LIST_MODEL_COL_PATH:
        config->path_column_width = width;
        break;
    case LIST_MODEL_COL_TYPE:
        config->type_column_width = width;
        break;
    case LIST_MODEL_COL_SIZE:
        config->size_column_width = width;
        break;
    case LIST_MODEL_COL_CHANGED:
        config->modified_column_width = width;
        break;
    default:
        trace("[listview] width of unknown column changed\n");
    }
}

static gboolean
on_listview_header_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (gdk_event_triggers_context_menu((GdkEvent *)event)) {
        GtkBuilder *builder = gtk_builder_new_from_resource("/org/fsearch/fsearch/menus.ui");
        GMenuModel *menu_model =
            G_MENU_MODEL(gtk_builder_get_object(builder, "fsearch_listview_column_popup_menu"));
        GtkWidget *list = gtk_tree_view_column_get_tree_view(GTK_TREE_VIEW_COLUMN(user_data));
        GtkWidget *menu_widget = gtk_menu_new_from_model(G_MENU_MODEL(menu_model));
        gtk_menu_attach_to_widget(GTK_MENU(menu_widget), list, NULL);
#if !GTK_CHECK_VERSION(3, 22, 0)
        gtk_menu_popup(GTK_MENU(menu_widget), NULL, NULL, NULL, NULL, event->button, event->time);
#else
        gtk_menu_popup_at_pointer(GTK_MENU(menu_widget), NULL);
#endif
        g_object_unref(builder);
        return TRUE;
    }
    return FALSE;
}

static void
listview_column_add_label(GtkTreeViewColumn *col, const char *title) {
    gtk_tree_view_column_set_title(col, title);
    g_signal_connect(gtk_tree_view_column_get_button(col),
                     "button-press-event",
                     G_CALLBACK(on_listview_header_clicked),
                     col);
}

static void
listview_column_set_size(GtkTreeViewColumn *col, int32_t size) {
    gtk_tree_view_column_set_fixed_width(col, size);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_column_set_expand(col, FALSE);
}

static void
listview_path_cell_data_func(GtkTreeViewColumn *col,
                             GtkCellRenderer *cell,
                             GtkTreeModel *tree_model,
                             GtkTreeIter *iter,
                             gpointer data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)data;
    FsearchQueryHighlight *q = fsearch_application_window_get_query_highlight(win);
    if (!q) {
        return;
    }

    DatabaseSearchEntry *entry = iter->user_data;
    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return;
    }

    char path[PATH_MAX] = "";
    if ((q->has_separator && q->auto_search_in_path) || q->search_in_path) {
        btree_node_get_path(node, path, sizeof(path));
    }
    else {
        g_object_set(G_OBJECT(cell), "attributes", NULL, NULL);
        return;
    }

    PangoAttrList *attr = fsearch_query_highlight_match(q, path);
    if (!attr) {
        return;
    }

    g_object_set(G_OBJECT(cell), "attributes", attr, NULL);
    pango_attr_list_unref(attr);
}

static void
listview_name_cell_data_func(GtkTreeViewColumn *col,
                             GtkCellRenderer *cell,
                             GtkTreeModel *tree_model,
                             GtkTreeIter *iter,
                             gpointer data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)data;
    FsearchQueryHighlight *q = fsearch_application_window_get_query_highlight(win);
    if (!q) {
        return;
    }

    DatabaseSearchEntry *entry = iter->user_data;
    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return;
    }

    PangoAttrList *attr = fsearch_query_highlight_match(q, node->name);
    if (!attr) {
        return;
    }

    g_object_set(G_OBJECT(cell), "attributes", attr, NULL);
    pango_attr_list_unref(attr);
}

static void
listview_add_name_column(GtkTreeView *list,
                         int32_t size,
                         int32_t pos,
                         FsearchApplicationWindow *win) {
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    GtkCellRenderer *renderer = NULL;
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (config->show_listview_icons) {
        renderer = gtk_cell_renderer_pixbuf_new();
        g_object_set(G_OBJECT(renderer), "stock-size", GTK_ICON_SIZE_LARGE_TOOLBAR, NULL);
        gtk_tree_view_column_pack_start(col, renderer, FALSE);
        gtk_tree_view_column_add_attribute(col, renderer, "pixbuf", LIST_MODEL_COL_ICON);
    }

    renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);

    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", LIST_MODEL_COL_NAME);
    listview_column_set_size(col, size);
    gtk_tree_view_column_set_sort_column_id(col, SORT_ID_NAME);
    gtk_tree_view_insert_column(list, col, pos);
    gtk_tree_view_column_set_expand(col, TRUE);
    listview_column_add_label(col, _("Name"));

    if (config->highlight_search_terms) {
        gtk_tree_view_column_set_cell_data_func(
            col, renderer, (GtkTreeCellDataFunc)listview_name_cell_data_func, win, NULL);
    }

    g_signal_connect(col, "notify::width", G_CALLBACK(on_listview_column_width_changed), NULL);
}

static void
listview_add_path_column(GtkTreeView *list,
                         int32_t size,
                         int32_t pos,
                         FsearchApplicationWindow *win) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_object_set(G_OBJECT(renderer), "foreground", "grey", NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", LIST_MODEL_COL_PATH);
    listview_column_set_size(col, size);
    gtk_tree_view_column_set_sort_column_id(col, SORT_ID_PATH);
    gtk_tree_view_insert_column(list, col, pos);
    listview_column_add_label(col, _("Path"));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (config->highlight_search_terms) {
        gtk_tree_view_column_set_cell_data_func(
            col, renderer, (GtkTreeCellDataFunc)listview_path_cell_data_func, win, NULL);
    }

    g_signal_connect(col, "notify::width", G_CALLBACK(on_listview_column_width_changed), NULL);
}

static void
listview_add_size_column(GtkTreeView *list, int32_t size, int32_t pos) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, "xalign", 1.0, NULL);
    g_object_set(G_OBJECT(renderer), "foreground", "grey", NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", LIST_MODEL_COL_SIZE);
    gtk_tree_view_column_set_alignment(col, 1.0);
    listview_column_set_size(col, size);
    gtk_tree_view_column_set_sort_column_id(col, SORT_ID_SIZE);
    gtk_tree_view_insert_column(list, col, pos);
    listview_column_add_label(col, _("Size"));

    g_signal_connect(col, "notify::width", G_CALLBACK(on_listview_column_width_changed), NULL);
}

static void
listview_add_modified_column(GtkTreeView *list, int32_t size, int32_t pos) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, "xalign", 1.0, NULL);
    g_object_set(G_OBJECT(renderer), "foreground", "grey", NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", LIST_MODEL_COL_CHANGED);
    gtk_tree_view_column_set_alignment(col, 1.0);
    listview_column_set_size(col, size);
    gtk_tree_view_column_set_sort_column_id(col, SORT_ID_CHANGED);
    gtk_tree_view_insert_column(list, col, pos);
    listview_column_add_label(col, _("Date Modified"));

    g_signal_connect(col, "notify::width", G_CALLBACK(on_listview_column_width_changed), NULL);
}

static void
listview_add_type_column(GtkTreeView *list, int32_t size, int32_t pos) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_object_set(G_OBJECT(renderer), "foreground", "grey", NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", LIST_MODEL_COL_TYPE);
    listview_column_set_size(col, size);
    gtk_tree_view_column_set_sort_column_id(col, SORT_ID_TYPE);
    gtk_tree_view_insert_column(list, col, pos);
    listview_column_add_label(col, _("Type"));

    g_signal_connect(col, "notify::width", G_CALLBACK(on_listview_column_width_changed), NULL);
}

void
listview_add_column(GtkTreeView *list,
                    uint32_t col_type,
                    int32_t size,
                    int32_t pos,
                    FsearchApplicationWindow *win) {
    switch (col_type) {
    case LIST_MODEL_COL_ICON:
    case LIST_MODEL_COL_NAME:
        listview_add_name_column(list, size, pos, win);
        break;
    case LIST_MODEL_COL_PATH:
        listview_add_path_column(list, size, pos, win);
        break;
    case LIST_MODEL_COL_TYPE:
        listview_add_type_column(list, size, pos);
        break;
    case LIST_MODEL_COL_CHANGED:
        listview_add_modified_column(list, size, pos);
        break;
    case LIST_MODEL_COL_SIZE:
        listview_add_size_column(list, size, pos);
        break;
    default:
        trace("[listview] trying add column of unknown column type\n");
    }
}

void
listview_add_default_columns(GtkTreeView *view, FsearchApplicationWindow *win) {
    listview_add_column(view, LIST_MODEL_COL_NAME, 250, 0, win);
    listview_add_column(view, LIST_MODEL_COL_PATH, 250, 1, win);
    listview_add_column(view, LIST_MODEL_COL_TYPE, 100, 2, NULL);
    listview_add_column(view, LIST_MODEL_COL_SIZE, 75, 3, NULL);
    listview_add_column(view, LIST_MODEL_COL_CHANGED, 125, 4, NULL);
}

void
listview_remove_column_at_pos(GtkTreeView *view, int32_t pos) {}

void
listview_remove_column(GtkTreeView *view, uint32_t col_type) {
    for (int i = 0; i < LIST_MODEL_N_COLUMNS; i++) {
        GtkTreeViewColumn *col = gtk_tree_view_get_column(view, i);
        int32_t id = gtk_tree_view_column_get_sort_column_id(col) + 1;
        if (id == col_type) {
            gtk_tree_view_remove_column(view, col);
            break;
        }
    }
}

uint32_t
listview_column_get_pos(GtkTreeView *view, uint32_t col_type) {
    return 0;
}

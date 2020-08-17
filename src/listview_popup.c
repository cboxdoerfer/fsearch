/*
   FSearch - A fast file search utility
   Copyright © 2018 Christian Boxdörfer

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

#include "listview_popup.h"
#include "database_search.h"

static void
fill_open_with_menu (GtkTreeView *view, GtkBuilder *builder, GtkTreeIter *iter)
{
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (!entry) {
        return;
    }

    BTreeNode * node = db_search_entry_get_node (entry);

    GList *app_list = NULL;
    char *content_type = NULL;

    if (node->is_dir) {
        content_type = g_content_type_from_mime_type ("inode/directory");
    }
    else {
        content_type = g_content_type_guess (node->name, NULL, 0, NULL);
    }

    if (!content_type) {
        goto clean_up;
    }

    app_list = g_app_info_get_all_for_type (content_type);

    GMenu *menu_mime = G_MENU (gtk_builder_get_object (builder,
                                                       "fsearch_listview_menu_open_with_mime_section"));

    for (GList *list_iter = app_list; list_iter; list_iter = list_iter->next) {
        GAppInfo *app_info = list_iter->data;
        const char *display_name = g_app_info_get_display_name (app_info);
        const char *app_id = g_app_info_get_id (app_info);

        char detailed_action[1024] = "";
        snprintf (detailed_action, sizeof (detailed_action), "win.open_with('%s')", app_id);

        GMenuItem *menu_item = g_menu_item_new (display_name, detailed_action);
        g_menu_item_set_icon (menu_item, g_app_info_get_icon (app_info));
        g_menu_append_item (menu_mime, menu_item);
        g_object_unref (menu_item);
    }

    char detailed_action[1024] = "";
    snprintf (detailed_action, sizeof (detailed_action), "win.open_with_other('%s')", content_type);
    GMenuItem *open_with_item = g_menu_item_new (_("Other Application…"), detailed_action);
    g_menu_append_item (menu_mime, open_with_item);
    g_object_unref (open_with_item);

clean_up:
    if (content_type) {
        g_free (content_type);
        content_type = NULL;
    }
    if (app_list) {
        g_list_free_full (app_list, g_object_unref);
        app_list = NULL;
    }
}

void
listview_popup_menu (GtkWidget *widget, GdkEventButton *event)
{
    GtkTreeView *view = GTK_TREE_VIEW (widget);
    GtkTreeSelection *selection = gtk_tree_view_get_selection (view);
    GtkTreeModel *model = gtk_tree_view_get_model (view);
    if (!model) {
        return;
    }

#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    int button = 0;
    int event_time = 0;
#pragma GCC diagnostic pop

    GtkTreeIter iter;
    gboolean iter_set = FALSE;
    GtkTreePath *path = NULL;
    if (event) {
        if (!gtk_tree_view_get_path_at_pos (view,
                                            event->x,
                                            event->y,
                                            &path,
                                            NULL,
                                            NULL,
                                            NULL)) {
            // clicked empty area
            // -> unselect everything
            gtk_tree_selection_unselect_all (selection);
            return;
        }
        if (!gtk_tree_selection_path_is_selected (selection, path)) {
            // clicked on an unselected item
            // -> unselected everything else
            // -> select that single item
            gtk_tree_selection_unselect_all (selection);
            gtk_tree_selection_select_path (selection, path);
        }
        iter_set = gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_path_free (path);

        button = event->button;
        event_time = event->time;
    }
    else {
        // Find the first selected entry
        GList *selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);
        if (selected_rows) {
            GtkTreePath *selected_path = selected_rows->data;
            iter_set = gtk_tree_model_get_iter(model, &iter, selected_path);
            g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);
        }

        button = 0;
        event_time = gtk_get_current_event_time ();
    }

    if (!iter_set) {
        return;
    }

    GtkBuilder *builder = gtk_builder_new_from_resource ("/org/fsearch/fsearch/menus.ui");

    fill_open_with_menu (GTK_TREE_VIEW (widget), builder, &iter);

    GMenu *menu_root = G_MENU (gtk_builder_get_object (builder,
                                                       "fsearch_listview_popup_menu"));
    GtkWidget *menu_widget = gtk_menu_new_from_model (G_MENU_MODEL (menu_root));
    g_object_unref (builder);

    gtk_menu_attach_to_widget (GTK_MENU (menu_widget),
                               GTK_WIDGET (widget),
                               NULL);
#if !GTK_CHECK_VERSION (3,22,0)
    gtk_menu_popup (GTK_MENU (menu_widget), NULL, NULL, NULL, NULL,
                    button, event_time);
#else
    gtk_menu_popup_at_pointer (GTK_MENU (menu_widget), NULL);
#endif
}


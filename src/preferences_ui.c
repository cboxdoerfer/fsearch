/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "fsearch.h"

enum
{
    COLUMN_NAME,
    NUM_COLUMNS
};

static bool model_changed = false;
static FsearchConfig *main_config = NULL;

static void
location_tree_row_deleted (GtkTreeModel *tree_model,
                           GtkTreePath  *path,
                           gpointer      user_data)
{
    model_changed = true;
}

static void
location_tree_row_inserted (GtkTreeModel *tree_model,
                            GtkTreePath  *path,
                            GtkTreeIter  *iter,
                            gpointer      user_data)
{
    model_changed = true;
}

static void
location_tree_row_changed (GtkTreeModel *tree_model,
                           GtkTreePath  *path,
                           GtkTreeIter  *iter,
                           gpointer      user_data)
{
    model_changed = true;
}

static GtkTreeModel *
create_folder_model (void)
{

    /* create list store */
    GtkListStore *store = gtk_list_store_new (NUM_COLUMNS,
                                              G_TYPE_STRING);

    /* add data to the list store */
    for (GList *l = main_config->locations; l != NULL; l = l->next) {
        GtkTreeIter iter;
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            COLUMN_NAME, l->data,
                            -1);
    }
    g_signal_connect ((gpointer)store, "row-changed", G_CALLBACK (location_tree_row_changed), NULL);
    g_signal_connect ((gpointer)store, "row-deleted", G_CALLBACK (location_tree_row_deleted), NULL);
    g_signal_connect ((gpointer)store, "row-inserted", G_CALLBACK (location_tree_row_inserted), NULL);

    return GTK_TREE_MODEL (store);
}

void
limit_num_results_toggled (GtkToggleButton *togglebutton,
                           gpointer user_data)
{
    GtkWidget *spin = GTK_WIDGET (user_data);
    gtk_widget_set_sensitive (spin, gtk_toggle_button_get_active (togglebutton));
}

static void
on_scan_folder_button_clicked (GtkButton *button,
                               gpointer user_data)
{
    //GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
    //GtkTreeSelection *sel = gtk_tree_view_get_selection (tree_view);
}

static void
remove_list_store_item (GtkTreeModel *model,
                        GtkTreePath  *path,
                        GtkTreeIter  *iter,
                        gpointer      userdata)
{
    gtk_list_store_remove (GTK_LIST_STORE (model), iter);
}

static void
on_remove_folder_button_clicked (GtkButton *button,
                                 gpointer user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection (tree_view);
    gtk_tree_selection_selected_foreach (sel, remove_list_store_item, NULL);
}

static void
on_add_folder_button_clicked (GtkButton *button,
                              gpointer user_data)
{
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    GtkTreeModel *model = GTK_TREE_MODEL (user_data);

    GtkWidget *window = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
                                                     GTK_WINDOW (window),
                                                     action,
                                                     "_Cancel",
                                                     GTK_RESPONSE_CANCEL,
                                                     "_Open",
                                                     GTK_RESPONSE_ACCEPT,
                                                     NULL);

    gint res = gtk_dialog_run (GTK_DIALOG (dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        char *uri = gtk_file_chooser_get_uri (chooser);
        char *path = g_filename_from_uri (uri, NULL, NULL);
        g_free (uri);
        if (path) {
            if (!g_list_find_custom (main_config->locations, path, (GCompareFunc)strcmp)) {
                GtkTreeIter iter;
                gtk_list_store_append (GTK_LIST_STORE (model), &iter);
                gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_NAME, path, -1);
                g_free (path);
            }
        }
    }

    gtk_widget_destroy (dialog);
}

void
on_folder_tree_view_selection_changed (GtkTreeSelection *sel,
                                       gpointer user_data)
{
    GList *button_list = (GList *)user_data;

    gboolean selected = gtk_tree_selection_get_selected (sel, NULL, NULL);
    for (GList *l = button_list; l; l = l->next) {
        gtk_widget_set_sensitive (GTK_WIDGET (l->data), selected);
    }
}

void
preferences_ui_launch (FsearchConfig *config, GtkWindow *window)
{
    main_config = config;
    GtkWidget *dialog = gtk_dialog_new_with_buttons ("Preferences",
                                                     GTK_WINDOW (window),
                                                     GTK_DIALOG_MODAL| GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_OK",
                                                     GTK_RESPONSE_OK,
                                                     "_Cancel",
                                                     GTK_RESPONSE_CANCEL,
                                                     NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_box_set_spacing (GTK_BOX (content_area), 4);
    gtk_container_set_border_width (GTK_CONTAINER (content_area), 4);

    GtkWidget *notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (content_area), notebook, TRUE, TRUE, 0);

    //gtk_box_pack_start (GTK_BOX (content_area), hbox, FALSE, FALSE, 0);

    // First page: Interface
    GtkWidget *interface_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (interface_box), 8);

    GtkWidget *interface_page_label = gtk_label_new ("Interface");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), interface_box, interface_page_label);

    GtkWidget *interface_table = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (interface_table), 4);
    gtk_grid_set_column_spacing (GTK_GRID (interface_table), 4);
    gtk_box_pack_start (GTK_BOX (interface_box), interface_table, TRUE, TRUE, 0);

    GtkWidget *enable_dark_theme = gtk_check_button_new_with_label ("Enable Dark Theme");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_dark_theme), main_config->enable_dark_theme);
    gtk_grid_attach (GTK_GRID (interface_table), enable_dark_theme, 0, 0, 1, 1);

    GtkWidget *enable_list_tooltips = gtk_check_button_new_with_label ("Show Tooltips");
    gtk_widget_set_tooltip_text (enable_list_tooltips, "When hovering a search results show a tooltip with the full path of that item.");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_list_tooltips), main_config->enable_list_tooltips);
    gtk_grid_attach (GTK_GRID (interface_table), enable_list_tooltips, 0, 1, 1, 1);

    GtkWidget *restore_window_size = gtk_check_button_new_with_label ("Restore Window Size on Startup");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (restore_window_size), main_config->restore_window_size);
    gtk_grid_attach (GTK_GRID (interface_table), restore_window_size, 0, 2, 1, 1);

    // First page: Search
    GtkWidget *search_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (search_box), 8);

    GtkWidget *search_page_label = gtk_label_new ("Search");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), search_box, search_page_label);

    GtkWidget *search_table = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (search_table), 4);
    gtk_grid_set_column_spacing (GTK_GRID (search_table), 4);
    gtk_box_pack_start (GTK_BOX (search_box), search_table, TRUE, TRUE, 0);

    GtkWidget *limit_num_results = gtk_check_button_new_with_label ("Limit Number of Results:");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (limit_num_results), main_config->limit_results);
    gtk_widget_set_tooltip_text (limit_num_results, "Limiting the number of search results increases the performance a lot. That's because the GtkTreeView is quite slow when you add lots of items to it.");
    gtk_grid_attach (GTK_GRID (search_table), limit_num_results, 0, 0, 1, 1);

    GtkWidget *num_results = gtk_spin_button_new_with_range (10.0, 1000000.0, 1.0);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (num_results), (double)main_config->num_results);
    gtk_widget_set_sensitive (num_results, main_config->limit_results);
    gtk_grid_attach (GTK_GRID (search_table), num_results, 1, 0, 1, 1);
    gtk_widget_set_hexpand (num_results, TRUE);

    g_signal_connect ((gpointer)limit_num_results, "toggled", G_CALLBACK (limit_num_results_toggled), num_results);

    // Second page: Database:w
    GtkWidget *database_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (database_box), 8);

    GtkWidget *database_page_label = gtk_label_new ("Database");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), database_box, database_page_label);

    GtkWidget *database_table = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (database_table), 4);
    gtk_grid_set_column_spacing (GTK_GRID (database_table), 4);
    //gtk_grid_set_column_homogeneous (GTK_GRID (database_table), TRUE);
    gtk_grid_set_row_homogeneous (GTK_GRID (database_table), FALSE);
    gtk_box_pack_start (GTK_BOX (database_box), database_table, TRUE, TRUE, 0);

    GtkWidget *update_database_on_launch = gtk_check_button_new_with_label ("Update Database on Application Launch");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (update_database_on_launch),
                                  main_config->update_database_on_launch);
    gtk_grid_attach (GTK_GRID (database_table), update_database_on_launch, 0, 0, 1, 1);

    GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_grid_attach (GTK_GRID (database_table), scrolled_window, 0, 1, 1, 1);
    gtk_widget_set_hexpand (scrolled_window, TRUE);
    gtk_widget_set_size_request (scrolled_window, 300, 150);

    GtkTreeModel *model = create_folder_model ();
    GtkWidget *folder_tree_view = gtk_tree_view_new_with_model (model);
    gtk_tree_view_set_search_column (GTK_TREE_VIEW (folder_tree_view), COLUMN_NAME);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (folder_tree_view), FALSE);
    gtk_container_add (GTK_CONTAINER (scrolled_window), folder_tree_view);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    //g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes ("Name", renderer, "text", COLUMN_NAME, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW(folder_tree_view), col);

    GtkWidget *folder_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_set_homogeneous (GTK_BOX (folder_box), FALSE);
    gtk_grid_attach (GTK_GRID (database_table), folder_box, 1, 1, 1, 1);

    GtkWidget *add_folder = gtk_button_new_with_label ("Add");
    gtk_box_pack_start (GTK_BOX (folder_box), add_folder, FALSE, FALSE, 0);
    g_signal_connect ((gpointer)add_folder, "clicked", G_CALLBACK (on_add_folder_button_clicked), model);

    GtkWidget *remove_folder = gtk_button_new_with_label ("Remove");
    gtk_box_pack_start (GTK_BOX (folder_box), remove_folder, FALSE, FALSE, 0);
    gtk_widget_set_sensitive (remove_folder, FALSE);
    g_signal_connect ((gpointer)remove_folder, "clicked", G_CALLBACK (on_remove_folder_button_clicked), folder_tree_view);

    GtkWidget *scan_folder = gtk_button_new_with_label ("Scan");
    gtk_box_pack_start (GTK_BOX (folder_box), scan_folder, FALSE, FALSE, 0);
    gtk_widget_set_sensitive (scan_folder, FALSE);
    g_signal_connect ((gpointer)scan_folder, "clicked", G_CALLBACK (on_scan_folder_button_clicked), folder_tree_view);

    GList *button_list = NULL;
    button_list = g_list_append (button_list, remove_folder);
    button_list = g_list_append (button_list, scan_folder);

    GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (folder_tree_view));
    g_signal_connect ((gpointer)sel, "changed", G_CALLBACK (on_folder_tree_view_selection_changed), button_list);

    gtk_widget_show_all (notebook);

    model_changed = false;

    gint response = gtk_dialog_run (GTK_DIALOG (dialog));

    if (response == GTK_RESPONSE_OK)
    {
        main_config->limit_results = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (limit_num_results));
        main_config->num_results = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (num_results));
        main_config->enable_dark_theme = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (enable_dark_theme));
        main_config->enable_list_tooltips = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (enable_list_tooltips));
        main_config->restore_window_size = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (restore_window_size));
        main_config->update_database_on_launch = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (update_database_on_launch));
        g_object_set(gtk_settings_get_default(),
                     "gtk-application-prefer-dark-theme",
                     main_config->enable_dark_theme,
                     NULL );

        if (model_changed) {
            g_list_free_full (main_config->locations, (GDestroyNotify)free);
            main_config->locations = NULL;

            GtkTreeIter iter;
            gboolean valid = gtk_tree_model_get_iter_first (model, &iter);
            gint row_count = 0;

            while (valid) {
                gchar *path = NULL;

                gtk_tree_model_get (model, &iter,
                        COLUMN_NAME, &path,
                        -1);

                // Do something with the data
                if (path) {
                    if (!g_list_find_custom (main_config->locations, path, (GCompareFunc)strcmp)) {
                        main_config->locations = g_list_append (main_config->locations, path);
                    }
                }

                valid = gtk_tree_model_iter_next (model, &iter);
                row_count++;
            }
            update_database ();
        }
    }

    g_list_free (button_list);
    gtk_widget_destroy (dialog);
}



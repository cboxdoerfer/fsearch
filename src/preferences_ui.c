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
create_tree_model (GList *list)
{

    /* create list store */
    GtkListStore *store = gtk_list_store_new (NUM_COLUMNS,
                                              G_TYPE_STRING);

    /* add data to the list store */
    for (GList *l = list; l != NULL; l = l->next) {
        GtkTreeIter iter;
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            COLUMN_NAME, l->data,
                            -1);
    }
    g_signal_connect ((gpointer)store,
                      "row-changed",
                      G_CALLBACK (location_tree_row_changed),
                      NULL);
    g_signal_connect ((gpointer)store,
                      "row-deleted",
                      G_CALLBACK (location_tree_row_deleted),
                      NULL);
    g_signal_connect ((gpointer)store,
                      "row-inserted",
                      G_CALLBACK (location_tree_row_inserted),
                      NULL);

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
remove_list_store_item (GtkTreeModel *model,
                        GtkTreePath  *path,
                        GtkTreeIter  *iter,
                        gpointer      userdata)
{
    gtk_list_store_remove (GTK_LIST_STORE (model), iter);
}

static void
on_remove_button_clicked (GtkButton *button,
                          gpointer user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection (tree_view);
    gtk_tree_selection_selected_foreach (sel, remove_list_store_item, NULL);
}

static void
run_file_chooser_dialog (GtkButton *button, GtkTreeModel *model, GList *list)
{
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

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
            if (!g_list_find_custom (list, path, (GCompareFunc)strcmp)) {
                GtkTreeIter iter;
                gtk_list_store_append (GTK_LIST_STORE (model), &iter);
                gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_NAME, path, -1);
                g_free (path);
            }
        }
    }

    gtk_widget_destroy (dialog);
}

static void
on_exclude_add_button_clicked (GtkButton *button,
                               gpointer user_data)
{
    run_file_chooser_dialog (button, user_data, main_config->exclude_locations);
}

static void
on_include_add_button_clicked (GtkButton *button,
                               gpointer user_data)
{
    run_file_chooser_dialog (button, user_data, main_config->locations);
}

static void
on_list_selection_changed (GtkTreeSelection *sel,
                           gpointer user_data)
{
    gboolean selected = gtk_tree_selection_get_selected (sel, NULL, NULL);
    gtk_widget_set_sensitive (GTK_WIDGET (user_data), selected);
}

static GtkWidget *
builder_get_object (GtkBuilder *builder, const char *name)
{
    return GTK_WIDGET (gtk_builder_get_object (builder, name));
}

static GList *
update_location_config (GtkTreeModel *model, GList *list)
{
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first (model, &iter);
    gint row_count = 0;

    while (valid) {
        gchar *path = NULL;

        gtk_tree_model_get (model, &iter,
                            COLUMN_NAME, &path,
                            -1);

        if (path) {
            if (!g_list_find_custom (list,
                                     path,
                                     (GCompareFunc)strcmp)) {
                list = g_list_append (list, path);
            }
        }

        valid = gtk_tree_model_iter_next (model, &iter);
        row_count++;
    }
    return list;
}

void
preferences_ui_launch (FsearchConfig *config, GtkWindow *window)
{
    main_config = config;
    GtkBuilder *builder = gtk_builder_new_from_resource ("/org/fsearch/fsearch/preferences.ui");
    GtkWidget *dialog = GTK_WIDGET (gtk_builder_get_object (builder, "FsearchPreferencesWindow"));
    gtk_window_set_transient_for (GTK_WINDOW (dialog), window);

    gtk_dialog_add_button (GTK_DIALOG (dialog), "_OK", GTK_RESPONSE_OK);
    gtk_dialog_add_button (GTK_DIALOG (dialog), "_Cancel", GTK_RESPONSE_CANCEL);

    // Interface page
    GtkToggleButton *enable_dark_theme_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                       "enable_dark_theme_button"));
    gtk_toggle_button_set_active (enable_dark_theme_button,
                                  main_config->enable_dark_theme);

    GtkToggleButton *show_tooltips_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                   "show_tooltips_button"));
    gtk_toggle_button_set_active (show_tooltips_button,
                                  main_config->enable_list_tooltips);

    GtkToggleButton *restore_win_size_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                      "restore_win_size_button"));
    gtk_toggle_button_set_active (restore_win_size_button,
                                  main_config->restore_window_size);

    // Search page
    GtkToggleButton *auto_search_in_path_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                         "auto_search_in_path_button"));
    gtk_toggle_button_set_active (auto_search_in_path_button,
                                  main_config->auto_search_in_path);

    GtkToggleButton *limit_num_results_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                       "limit_num_results_button"));
    gtk_toggle_button_set_active (limit_num_results_button,
                                  main_config->limit_results);

    GtkWidget *limit_num_results_spin = builder_get_object (builder,
                                                            "limit_num_results_spin");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (limit_num_results_spin),
                               (double)main_config->num_results);
    gtk_widget_set_sensitive (limit_num_results_spin,
                              main_config->limit_results);
    g_signal_connect (limit_num_results_button,
                      "toggled",
                      G_CALLBACK (limit_num_results_toggled),
                      limit_num_results_spin);

    // Database page
    GtkToggleButton *update_db_at_start_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                        "update_db_at_start_button"));
    gtk_toggle_button_set_active (update_db_at_start_button,
                                  main_config->update_database_on_launch);

    // Include page
    GtkTreeModel *include_model = create_tree_model (main_config->locations);
    GtkTreeView *include_list = GTK_TREE_VIEW (builder_get_object (builder,
                                                                   "include_list"));
    gtk_tree_view_set_model (include_list, include_model);
    gtk_tree_view_set_search_column (include_list, COLUMN_NAME);
    gtk_tree_view_set_headers_visible (include_list, FALSE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes ("Name",
                                                                       renderer,
                                                                       "text",
                                                                       COLUMN_NAME,
                                                                       NULL);
    gtk_tree_view_append_column (include_list, col);

    GtkWidget *include_add_button = builder_get_object (builder,
                                                        "include_add_button");
    g_signal_connect (include_add_button,
                      "clicked",
                      G_CALLBACK (on_include_add_button_clicked),
                      include_model);

    GtkWidget *include_remove_button = builder_get_object (builder,
                                                           "include_remove_button");
    g_signal_connect (include_remove_button,
                      "clicked",
                      G_CALLBACK (on_remove_button_clicked),
                      include_list);

    GtkTreeSelection *sel = gtk_tree_view_get_selection (include_list);
    g_signal_connect (sel,
                      "changed",
                      G_CALLBACK (on_list_selection_changed),
                      include_remove_button);

    GtkToggleButton *follow_symlinks_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                     "follow_symlinks_button"));
    gtk_toggle_button_set_active (follow_symlinks_button,
                                  main_config->follow_symlinks);


    // Exclude model
    GtkTreeModel *exclude_model = create_tree_model (main_config->exclude_locations);
    GtkTreeView *exclude_list = GTK_TREE_VIEW (builder_get_object (builder,
                                                                   "exclude_list"));
    gtk_tree_view_set_model (exclude_list, exclude_model);
    gtk_tree_view_set_search_column (exclude_list, COLUMN_NAME);
    gtk_tree_view_set_headers_visible (exclude_list, FALSE);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes ("Name",
                                                    renderer,
                                                    "text",
                                                    COLUMN_NAME,
                                                    NULL);
    gtk_tree_view_append_column (exclude_list, col);

    GtkWidget *exclude_add_button = builder_get_object (builder,
                                                        "exclude_add_button");
    g_signal_connect (exclude_add_button,
                      "clicked",
                      G_CALLBACK (on_exclude_add_button_clicked),
                      exclude_model);

    GtkWidget *exclude_remove_button = builder_get_object (builder,
                                                           "exclude_remove_button");
    g_signal_connect (exclude_remove_button,
                      "clicked",
                      G_CALLBACK (on_remove_button_clicked),
                      exclude_list);

    GtkTreeSelection *exclude_selection = gtk_tree_view_get_selection (exclude_list);
    g_signal_connect (exclude_selection,
                      "changed",
                      G_CALLBACK (on_list_selection_changed),
                      exclude_remove_button);

    GtkToggleButton *exclude_hidden_items_button = GTK_TOGGLE_BUTTON (builder_get_object (builder,
                                                                                          "exclude_hidden_items_button"));
    gtk_toggle_button_set_active (exclude_hidden_items_button,
                                  main_config->exclude_hidden_items);


    model_changed = false;

    gint response = gtk_dialog_run (GTK_DIALOG (dialog));

    if (response == GTK_RESPONSE_OK) {
        main_config->auto_search_in_path = gtk_toggle_button_get_active (auto_search_in_path_button);
        main_config->limit_results = gtk_toggle_button_get_active (limit_num_results_button);

        main_config->num_results = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (limit_num_results_spin));

        main_config->enable_dark_theme = gtk_toggle_button_get_active (enable_dark_theme_button);

        main_config->enable_list_tooltips = gtk_toggle_button_get_active (show_tooltips_button);

        main_config->restore_window_size = gtk_toggle_button_get_active (restore_win_size_button);

        main_config->update_database_on_launch = gtk_toggle_button_get_active (update_db_at_start_button);


        bool old_exclude_hidden_items = main_config->exclude_hidden_items;
        main_config->exclude_hidden_items = gtk_toggle_button_get_active (exclude_hidden_items_button);
        if (old_exclude_hidden_items != main_config->exclude_hidden_items) {
            model_changed = true;
        }

        bool old_follow_symlinks = main_config->follow_symlinks;
        main_config->follow_symlinks = gtk_toggle_button_get_active (follow_symlinks_button);
        if (old_follow_symlinks != main_config->follow_symlinks) {
            model_changed = true;
        }


        g_object_set(gtk_settings_get_default(),
                     "gtk-application-prefer-dark-theme",
                     main_config->enable_dark_theme,
                     NULL );

        if (model_changed) {
            g_list_free_full (main_config->locations, (GDestroyNotify)free);
            g_list_free_full (main_config->exclude_locations, (GDestroyNotify)free);
            main_config->locations = NULL;
            main_config->exclude_locations = NULL;

            main_config->locations = update_location_config (include_model, main_config->locations);
            main_config->exclude_locations = update_location_config (exclude_model, main_config->exclude_locations);
            update_database ();
        }
    }

    g_object_unref (builder);
    gtk_widget_destroy (dialog);
}



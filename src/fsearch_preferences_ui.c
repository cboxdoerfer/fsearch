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

#define G_LOG_DOMAIN "fsearch-preferences-ui"

#include "fsearch_preferences_ui.h"
#include "fsearch_exclude_path.h"
#include "fsearch_filter_editor.h"
#include "fsearch_index.h"
#include "fsearch_preferences_widgets.h"
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FsearchConfig *new_config;
    void (*finished_cb)(FsearchConfig *);

    GtkWindow *window;
    GtkBuilder *builder;
    GtkWidget *dialog;

    GtkWidget *main_notebook;

    // Interface page
    GtkToggleButton *enable_dark_theme_button;
    GtkToggleButton *show_menubar_button;
    GtkToggleButton *show_tooltips_button;
    GtkToggleButton *restore_win_size_button;
    GtkToggleButton *exit_on_escape_button;
    GtkToggleButton *restore_sort_order_button;
    GtkToggleButton *restore_column_config_button;
    GtkToggleButton *double_click_path_button;
    GtkToggleButton *single_click_open_button;
    GtkToggleButton *launch_desktop_files_button;
    GtkToggleButton *show_icons_button;
    GtkToggleButton *highlight_search_terms;
    GtkToggleButton *show_base_2_units;
    GtkBox *action_after_file_open_box;
    GtkFrame *action_after_file_open_frame;
    GtkComboBox *action_after_file_open;
    GtkToggleButton *action_after_file_open_keyboard;
    GtkToggleButton *action_after_file_open_mouse;
    GtkToggleButton *show_indexing_status;

    // Search page
    GtkToggleButton *auto_search_in_path_button;
    GtkToggleButton *auto_match_case_button;
    GtkToggleButton *search_as_you_type_button;
    GtkToggleButton *hide_results_button;

    GtkTreeView *filter_list;
    GtkTreeModel *filter_model;
    GtkWidget *filter_add_button;
    GtkWidget *filter_edit_button;
    GtkWidget *filter_remove_button;
    GtkWidget *filter_revert_button;
    GtkTreeSelection *filter_selection;

    // Database page
    GtkToggleButton *update_db_at_start_button;
    GtkToggleButton *auto_update_checkbox;
    GtkBox *auto_update_box;
    GtkBox *auto_update_spin_box;
    GtkWidget *auto_update_hours_spin_button;
    GtkWidget *auto_update_minutes_spin_button;

    // Dialog page
    GtkToggleButton *show_dialog_failed_opening;

    // Include page
    GtkTreeView *index_list;
    GtkTreeModel *index_model;
    GtkWidget *index_add_button;
    GtkWidget *index_remove_button;
    GtkTreeSelection *sel;

    // Exclude model
    GtkTreeView *exclude_list;
    GtkTreeModel *exclude_model;
    GtkWidget *exclude_add_button;
    GtkWidget *exclude_remove_button;
    GtkTreeSelection *exclude_selection;
    GtkToggleButton *exclude_hidden_items_button;
    GtkEntry *exclude_files_entry;
    gchar *exclude_files_str;
} FsearchPreferencesInterface;

enum { COLUMN_NAME, NUM_COLUMNS };

guint help_reset_timeout_id = 0;
static GtkWidget *help_stack = NULL;
static GtkWidget *help_expander = NULL;
static GtkWidget *help_description = NULL;

static void
on_toggle_set_sensitive(GtkToggleButton *togglebutton, gpointer user_data) {
    GtkWidget *spin = GTK_WIDGET(user_data);
    gtk_widget_set_sensitive(spin, gtk_toggle_button_get_active(togglebutton));
}

static void
on_auto_update_minutes_spin_button_changed(GtkSpinButton *spin_button, gpointer user_data) {
    GtkSpinButton *hours_spin = GTK_SPIN_BUTTON(user_data);
    double minutes = gtk_spin_button_get_value(spin_button);
    double hours = gtk_spin_button_get_value(hours_spin);

    if (hours == 0 && minutes == 0) {
        gtk_spin_button_set_value(spin_button, 1.0);
    }
}

static void
on_auto_update_hours_spin_button_changed(GtkSpinButton *spin_button, gpointer user_data) {
    GtkSpinButton *minutes_spin = GTK_SPIN_BUTTON(user_data);
    double hours = gtk_spin_button_get_value(spin_button);
    double minutes = gtk_spin_button_get_value(minutes_spin);

    if (hours == 0 && minutes == 0) {
        gtk_spin_button_set_value(minutes_spin, 1.0);
    }
}

static void
on_remove_button_clicked(GtkButton *button, gpointer user_data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(user_data);
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_selected_foreach(sel, pref_treeview_row_remove, NULL);
}

typedef struct {
    GtkTreeModel *model;
    void (*add_path_cb)(GtkTreeModel *, const char *);
} FsearchPreferencesFileChooserContext;

#if !GTK_CHECK_VERSION(3, 20, 0)
static void
on_file_chooser_dialog_response(GtkFileChooserDialog *dialog, GtkResponseType response, gpointer user_data) {
#else
static void
on_file_chooser_native_dialog_response(GtkNativeDialog *dialog, GtkResponseType response, gpointer user_data) {
#endif
    FsearchPreferencesFileChooserContext *ctx = user_data;
    g_assert(ctx);
    g_assert(ctx->add_path_cb);

    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        g_autoptr(GListModel) filenames = gtk_file_chooser_get_files(chooser);
        for (guint i = 0; i < g_list_model_get_n_items(filenames); ++i) {
            gchar *filename = g_list_model_get_item(filenames, i);
            if (filename) {
                ctx->add_path_cb(ctx->model, filename);
            }
        }
    }

    g_clear_object(&dialog);

    g_slice_free(FsearchPreferencesFileChooserContext, g_steal_pointer(&ctx));
}

static void
run_file_chooser_dialog(GtkButton *button, FsearchPreferencesFileChooserContext *ctx) {
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    GtkWindow *window = GTK_WINDOW(root);

    GtkFileChooserNative *dialog =
        gtk_file_chooser_native_new(_("Select folder"), GTK_WINDOW(window), action, _("_Select"), _("_Cancel"));

    g_signal_connect(dialog, "response", G_CALLBACK(on_file_chooser_native_dialog_response), ctx);
    gtk_native_dialog_set_transient_for(GTK_NATIVE_DIALOG(dialog), GTK_WINDOW(window));
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), true);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
}

void
on_filter_editor_edit_finished(FsearchFilter *old_filter,
                               char *name,
                               char *macro,
                               char *query,
                               FsearchQueryFlags flags,
                               gpointer data) {
    FsearchPreferencesInterface *ui = data;
    fsearch_filter_manager_edit(ui->new_config->filters, old_filter, name, macro, query, flags);
    g_clear_pointer(&name, g_free);
    g_clear_pointer(&macro, g_free);
    g_clear_pointer(&query, g_free);
    pref_filter_treeview_update(ui->filter_model, ui->new_config->filters);
}

void
on_filter_editor_add_finished(FsearchFilter *old_filter,
                              char *name,
                              char *macro,
                              char *query,
                              FsearchQueryFlags flags,
                              gpointer data) {
    FsearchPreferencesInterface *ui = data;
    if (!name) {
        return;
    }

    FsearchFilter *filter = fsearch_filter_new(name, macro, query, flags);
    g_clear_pointer(&name, g_free);
    g_clear_pointer(&macro, g_free);
    g_clear_pointer(&query, g_free);

    fsearch_filter_manager_append_filter(ui->new_config->filters, filter);
    pref_filter_treeview_row_add(ui->filter_model, filter);
    g_clear_pointer(&filter, fsearch_filter_unref);
}

static FsearchFilter *
get_selected_filter(FsearchPreferencesInterface *ui) {
    GtkTreeIter iter = {0};
    GtkTreeModel *model = NULL;
    gboolean selected = gtk_tree_selection_get_selected(ui->filter_selection, &model, &iter);
    if (!selected) {
        return NULL;
    }

    g_autofree char *name = NULL;
    gtk_tree_model_get(model, &iter, 0, &name, -1);
    g_assert(name);

    return fsearch_filter_manager_get_filter_for_name(ui->new_config->filters, name);
}

static void
on_filter_revert_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    g_clear_pointer(&ui->new_config->filters, fsearch_filter_manager_free);
    ui->new_config->filters = fsearch_filter_manager_new_with_defaults();
    pref_filter_treeview_update(ui->filter_model, ui->new_config->filters);
}

static void
on_filter_remove_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    FsearchFilter *filter = get_selected_filter(ui);
    if (filter) {
        fsearch_filter_manager_remove(ui->new_config->filters, filter);
        g_clear_pointer(&filter, fsearch_filter_unref);
        gtk_tree_selection_selected_foreach(ui->filter_selection, pref_treeview_row_remove, NULL);
    }
}

static void
on_filter_edit_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;

    fsearch_filter_editor_run(_("Edit filter"),
                              GTK_WINDOW(ui->dialog),
                              get_selected_filter(ui),
                              on_filter_editor_edit_finished,
                              ui);
}

void
on_filter_model_reordered(GtkTreeModel *tree_model,
                          GtkTreePath *path,
                          GtkTreeIter *iter,
                          gpointer new_order,
                          gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    guint n_filters = gtk_tree_model_iter_n_children(tree_model, NULL);
    fsearch_filter_manager_reorder(ui->new_config->filters, new_order, n_filters);
}

void
on_filter_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    gboolean selected = gtk_tree_selection_get_selected(ui->filter_selection, NULL, NULL);
    if (!selected) {
        return;
    }
    on_filter_edit_button_clicked(GTK_BUTTON(ui->filter_edit_button), ui);
}

static void
on_filter_add_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    fsearch_filter_editor_run(_("Add filter"), GTK_WINDOW(ui->dialog), NULL, on_filter_editor_add_finished, ui);
}

static void
on_exclude_add_button_clicked(GtkButton *button, gpointer user_data) {
    GtkTreeModel *model = user_data;
    FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
    ctx->model = model;
    ctx->add_path_cb = pref_exclude_treeview_row_add;
    run_file_chooser_dialog(button, ctx);
}

static void
on_index_add_button_clicked(GtkButton *button, gpointer user_data) {
    GtkTreeModel *model = user_data;
    FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
    ctx->model = model;
    ctx->add_path_cb = pref_index_treeview_row_add;
    run_file_chooser_dialog(button, ctx);
}

static void
on_filter_list_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;
    gboolean selected = gtk_tree_selection_get_selected(sel, NULL, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->filter_remove_button), selected);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->filter_edit_button), selected);
    return;
}

static void
on_list_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
    gboolean selected = gtk_tree_selection_get_selected(sel, NULL, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(user_data), selected);
}

static gboolean
help_reset(gpointer user_data) {
    if (help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(help_stack), GTK_WIDGET(help_description));
    }
    help_reset_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean
on_help_reset(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    if (help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }
    help_reset_timeout_id = g_timeout_add(200, help_reset, NULL);
    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_help_show(GtkWidget *widget, int x, int y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data) {
    if (help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }

    if (help_reset_timeout_id != 0) {
        g_source_remove(help_reset_timeout_id);
        help_reset_timeout_id = 0;
    }
    if (help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(help_stack), GTK_WIDGET(user_data));
    }
    return GDK_EVENT_PROPAGATE;
}

static GtkWidget *
builder_init_widget(GtkBuilder *builder, const char *name, const char *help) {
    GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, name));
    GtkWidget *help_widget = GTK_WIDGET(gtk_builder_get_object(builder, help));
    g_signal_connect(widget, "query-tooltip", G_CALLBACK(on_help_show), help_widget);
    g_signal_connect(widget, "leave-notify-event", G_CALLBACK(on_help_reset), NULL);
    g_signal_connect(widget, "focus-out-event", G_CALLBACK(on_help_reset), NULL);
    return widget;
}

static GtkToggleButton *
toggle_button_get(GtkBuilder *builder, const char *name, const char *help, bool val) {
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(builder_init_widget(builder, name, help));
    gtk_toggle_button_set_active(button, val);
    return button;
}

static void
action_after_file_open_changed(GtkComboBox *widget, gpointer user_data) {
    int active = gtk_combo_box_get_active(widget);
    if (active != ACTION_AFTER_OPEN_NOTHING) {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), TRUE);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), FALSE);
    }
}

static void
preferences_ui_get_state(FsearchPreferencesInterface *ui) {
    FsearchConfig *new_config = ui->new_config;
    new_config->search_as_you_type = gtk_toggle_button_get_active(ui->search_as_you_type_button);
    new_config->enable_dark_theme = gtk_toggle_button_get_active(ui->enable_dark_theme_button);
    new_config->show_menubar = !gtk_toggle_button_get_active(ui->show_menubar_button);
    new_config->restore_column_config = gtk_toggle_button_get_active(ui->restore_column_config_button);
    new_config->restore_sort_order = gtk_toggle_button_get_active(ui->restore_sort_order_button);
    new_config->double_click_path = gtk_toggle_button_get_active(ui->double_click_path_button);
    new_config->enable_list_tooltips = gtk_toggle_button_get_active(ui->show_tooltips_button);
    new_config->restore_window_size = gtk_toggle_button_get_active(ui->restore_win_size_button);
    new_config->exit_on_escape = gtk_toggle_button_get_active(ui->exit_on_escape_button);
    new_config->update_database_on_launch = gtk_toggle_button_get_active(ui->update_db_at_start_button);
    new_config->update_database_every = gtk_toggle_button_get_active(ui->auto_update_checkbox);
    new_config->update_database_every_hours = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->auto_update_hours_spin_button));
    new_config->update_database_every_minutes = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->auto_update_minutes_spin_button));
    new_config->show_base_2_units = gtk_toggle_button_get_active(ui->show_base_2_units);
    new_config->action_after_file_open = gtk_combo_box_get_active(ui->action_after_file_open);
    new_config->action_after_file_open_keyboard = gtk_toggle_button_get_active(ui->action_after_file_open_keyboard);
    new_config->action_after_file_open_mouse = gtk_toggle_button_get_active(ui->action_after_file_open_mouse);
    new_config->show_indexing_status = gtk_toggle_button_get_active(ui->show_indexing_status);
    // Dialogs
    new_config->show_dialog_failed_opening = gtk_toggle_button_get_active(ui->show_dialog_failed_opening);
    new_config->auto_search_in_path = gtk_toggle_button_get_active(ui->auto_search_in_path_button);
    new_config->auto_match_case = gtk_toggle_button_get_active(ui->auto_match_case_button);
    new_config->hide_results_on_empty_search = gtk_toggle_button_get_active(ui->hide_results_button);
    new_config->highlight_search_terms = gtk_toggle_button_get_active(ui->highlight_search_terms);
    new_config->single_click_open = gtk_toggle_button_get_active(ui->single_click_open_button);
    new_config->launch_desktop_files = gtk_toggle_button_get_active(ui->launch_desktop_files_button);
    new_config->show_listview_icons = gtk_toggle_button_get_active(ui->show_icons_button);
    new_config->exclude_hidden_items = gtk_toggle_button_get_active(ui->exclude_hidden_items_button);

    g_clear_pointer(&new_config->exclude_files, g_strfreev);
    GtkEntryBuffer *buffer = gtk_entry_get_buffer (ui->exclude_files_entry);
    new_config->exclude_files = g_strsplit(gtk_entry_buffer_get_text(buffer), ";", -1);

    if (new_config->indexes) {
        g_list_free_full(g_steal_pointer(&new_config->indexes), (GDestroyNotify)fsearch_index_free);
    }
    new_config->indexes = pref_index_treeview_data_get(ui->index_list);

    if (new_config->exclude_locations) {
        g_list_free_full(g_steal_pointer(&new_config->exclude_locations), (GDestroyNotify)fsearch_exclude_path_free);
    }
    new_config->exclude_locations = pref_exclude_treeview_data_get(ui->exclude_list);
}

static void
preferences_ui_cleanup(FsearchPreferencesInterface *ui) {
    g_clear_pointer(&ui->exclude_files_str, free);

    if (help_reset_timeout_id != 0) {
        g_source_remove(help_reset_timeout_id);
        help_reset_timeout_id = 0;
    }
    help_stack = NULL;
    help_expander = NULL;

    g_clear_object(&ui->builder);
    g_clear_pointer(&ui->dialog, g_object_unref);
    g_clear_pointer(&ui, free);
}

static void
on_preferences_ui_response(GtkDialog *dialog, GtkResponseType response, gpointer user_data) {
    FsearchPreferencesInterface *ui = user_data;

    if (response != GTK_RESPONSE_OK) {
        g_clear_pointer(&ui->new_config, config_free);
    }
    else {
        preferences_ui_get_state(ui);
    }

    if (ui->finished_cb) {
        ui->finished_cb(ui->new_config);
    }

    preferences_ui_cleanup(ui);
}

static void
preferences_ui_init(FsearchPreferencesInterface *ui, FsearchPreferencesPage page) {
    FsearchConfig *new_config = ui->new_config;

    ui->builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/fsearch_preferences.ui");

    ui->dialog = GTK_WIDGET(gtk_builder_get_object(ui->builder, "FsearchPreferencesWindow"));
    gtk_window_set_transient_for(GTK_WINDOW(ui->dialog), ui->window);
    gtk_dialog_add_button(GTK_DIALOG(ui->dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(ui->dialog), _("_OK"), GTK_RESPONSE_OK);
    g_signal_connect(ui->dialog, "response", G_CALLBACK(on_preferences_ui_response), ui);

    ui->main_notebook = GTK_WIDGET(gtk_builder_get_object(ui->builder, "pref_main_notebook"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->main_notebook), page);

    help_stack = GTK_WIDGET(gtk_builder_get_object(ui->builder, "help_stack"));
    help_description = GTK_WIDGET(gtk_builder_get_object(ui->builder, "help_help"));
    help_expander = GTK_WIDGET(gtk_builder_get_object(ui->builder, "help_expander"));

    // Interface page
    ui->enable_dark_theme_button =
        toggle_button_get(ui->builder, "enable_dark_theme_button", "help_dark_theme", new_config->enable_dark_theme);

    ui->show_menubar_button = toggle_button_get(ui->builder, "show_menubar_button", "help_csd", !new_config->show_menubar);

    ui->show_tooltips_button =
        toggle_button_get(ui->builder, "show_tooltips_button", "help_show_tooltips", new_config->enable_list_tooltips);

    ui->restore_win_size_button =
        toggle_button_get(ui->builder, "restore_win_size_button", "help_window_size", new_config->restore_window_size);

    ui->exit_on_escape_button =
        toggle_button_get(ui->builder, "exit_on_escape_button", "help_exit_on_escape", new_config->exit_on_escape);

    ui->restore_sort_order_button = toggle_button_get(ui->builder,
                                                      "restore_sort_order_button",
                                                      "help_restore_sort_order",
                                                      new_config->restore_sort_order);

    ui->restore_column_config_button = toggle_button_get(ui->builder,
                                                         "restore_column_config_button",
                                                         "help_restore_column_config",
                                                         new_config->restore_column_config);

    ui->double_click_path_button = toggle_button_get(ui->builder,
                                                     "double_click_path_button",
                                                     "help_double_click_path",
                                                     new_config->double_click_path);

    ui->single_click_open_button = toggle_button_get(ui->builder,
                                                     "single_click_open_button",
                                                     "help_single_click_open",
                                                     new_config->single_click_open);

    ui->launch_desktop_files_button = toggle_button_get(ui->builder,
                                                        "launch_desktop_files_button",
                                                        "help_launch_desktop_files",
                                                        new_config->launch_desktop_files);

    ui->show_icons_button =
        toggle_button_get(ui->builder, "show_icons_button", "help_show_icons", new_config->show_listview_icons);

    ui->highlight_search_terms = toggle_button_get(ui->builder,
                                                   "highlight_search_terms",
                                                   "help_highlight_search_terms",
                                                   new_config->highlight_search_terms);

    ui->show_base_2_units =
        toggle_button_get(ui->builder, "show_base_2_units", "help_units", new_config->show_base_2_units);

    ui->action_after_file_open_frame = GTK_FRAME(
        builder_init_widget(ui->builder, "action_after_file_open_frame", "help_action_after_open"));
    ui->action_after_file_open_box = GTK_BOX(gtk_builder_get_object(ui->builder, "action_after_file_open_box"));
    ui->action_after_file_open = GTK_COMBO_BOX(
        builder_init_widget(ui->builder, "action_after_file_open", "help_action_after_open"));
    gtk_combo_box_set_active(ui->action_after_file_open, new_config->action_after_file_open);

    g_signal_connect(ui->action_after_file_open,
                     "changed",
                     G_CALLBACK(action_after_file_open_changed),
                     ui->action_after_file_open_box);

    if (new_config->action_after_file_open != ACTION_AFTER_OPEN_NOTHING) {
        gtk_widget_set_sensitive(GTK_WIDGET(ui->action_after_file_open_box), TRUE);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(ui->action_after_file_open_box), FALSE);
    }

    ui->action_after_file_open_keyboard = toggle_button_get(ui->builder,
                                                            "action_after_file_open_keyboard",
                                                            "help_action_after_open",
                                                            new_config->action_after_file_open_keyboard);

    ui->action_after_file_open_mouse = toggle_button_get(ui->builder,
                                                         "action_after_file_open_mouse",
                                                         "help_action_after_open",
                                                         new_config->action_after_file_open_mouse);

    ui->show_indexing_status = toggle_button_get(ui->builder,
                                                 "show_indexing_status_button",
                                                 "help_show_indexing_status",
                                                 new_config->show_indexing_status);

    // Search page
    ui->auto_search_in_path_button =
        toggle_button_get(ui->builder, "auto_search_in_path_button", "help_auto_path", new_config->auto_search_in_path);

    ui->auto_match_case_button =
        toggle_button_get(ui->builder, "auto_match_case_button", "help_auto_case", new_config->auto_match_case);

    ui->search_as_you_type_button = toggle_button_get(ui->builder,
                                                      "search_as_you_type_button",
                                                      "help_search_as_you_type",
                                                      new_config->search_as_you_type);

    ui->hide_results_button = toggle_button_get(ui->builder,
                                                "hide_results_button",
                                                "help_hide_results",
                                                new_config->hide_results_on_empty_search);

    ui->filter_list = GTK_TREE_VIEW(builder_init_widget(ui->builder, "filter_list", "help_filter_list"));
    g_signal_connect(ui->filter_list, "row-activated", G_CALLBACK(on_filter_list_row_activated), ui);

    ui->filter_model = pref_filter_treeview_init(ui->filter_list, new_config->filters);
    g_signal_connect(ui->filter_model, "rows-reordered", G_CALLBACK(on_filter_model_reordered), ui);

    ui->filter_add_button = builder_init_widget(ui->builder, "filter_add_button", "help_filter_add");
    g_signal_connect(ui->filter_add_button, "clicked", G_CALLBACK(on_filter_add_button_clicked), ui);

    ui->filter_edit_button = builder_init_widget(ui->builder, "filter_edit_button", "help_filter_edit");
    g_signal_connect(ui->filter_edit_button, "clicked", G_CALLBACK(on_filter_edit_button_clicked), ui);

    ui->filter_remove_button = builder_init_widget(ui->builder, "filter_remove_button", "help_filter_remove");
    g_signal_connect(ui->filter_remove_button, "clicked", G_CALLBACK(on_filter_remove_button_clicked), ui);

    ui->filter_revert_button = builder_init_widget(ui->builder, "filter_revert_button", "help_filter_revert");
    g_signal_connect(ui->filter_revert_button, "clicked", G_CALLBACK(on_filter_revert_button_clicked), ui);

    ui->filter_selection = gtk_tree_view_get_selection(ui->filter_list);
    g_signal_connect(ui->filter_selection, "changed", G_CALLBACK(on_filter_list_selection_changed), ui);

    // Database page
    ui->update_db_at_start_button = toggle_button_get(ui->builder,
                                                      "update_db_at_start_button",
                                                      "help_update_database_on_start",
                                                      new_config->update_database_on_launch);

    ui->auto_update_checkbox = toggle_button_get(ui->builder,
                                                 "auto_update_checkbox",
                                                 "help_update_database_every",
                                                 new_config->update_database_every);

    ui->auto_update_box = GTK_BOX(builder_init_widget(ui->builder, "auto_update_box", "help_update_database_every"));
    ui->auto_update_spin_box = GTK_BOX(gtk_builder_get_object(ui->builder, "auto_update_spin_box"));
    gtk_widget_set_sensitive(GTK_WIDGET(ui->auto_update_spin_box), new_config->update_database_every);
    g_signal_connect(ui->auto_update_checkbox, "toggled", G_CALLBACK(on_toggle_set_sensitive), ui->auto_update_spin_box);

    ui->auto_update_hours_spin_button =
        builder_init_widget(ui->builder, "auto_update_hours_spin_button", "help_update_database_every");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->auto_update_hours_spin_button),
                              (double)new_config->update_database_every_hours);

    ui->auto_update_minutes_spin_button =
        builder_init_widget(ui->builder, "auto_update_minutes_spin_button", "help_update_database_every");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->auto_update_minutes_spin_button),
                              (double)new_config->update_database_every_minutes);

    g_signal_connect(GTK_SPIN_BUTTON(ui->auto_update_hours_spin_button),
                     "value-changed",
                     G_CALLBACK(on_auto_update_hours_spin_button_changed),
                     ui->auto_update_minutes_spin_button);
    g_signal_connect(GTK_SPIN_BUTTON(ui->auto_update_minutes_spin_button),
                     "value-changed",
                     G_CALLBACK(on_auto_update_minutes_spin_button_changed),
                     ui->auto_update_hours_spin_button);

    // Dialog page
    ui->show_dialog_failed_opening = toggle_button_get(ui->builder,
                                                       "show_dialog_failed_opening",
                                                       "help_warn_failed_open",
                                                       new_config->show_dialog_failed_opening);

    // Include page
    ui->index_list = GTK_TREE_VIEW(builder_init_widget(ui->builder, "index_list", "help_index_list"));
    ui->index_model = pref_index_treeview_init(ui->index_list, new_config->indexes);

    ui->index_add_button = builder_init_widget(ui->builder, "index_add_button", "help_index_add");
    g_signal_connect(ui->index_add_button, "clicked", G_CALLBACK(on_index_add_button_clicked), ui->index_model);

    ui->index_remove_button = builder_init_widget(ui->builder, "index_remove_button", "help_index_remove");
    g_signal_connect(ui->index_remove_button, "clicked", G_CALLBACK(on_remove_button_clicked), ui->index_list);

    ui->sel = gtk_tree_view_get_selection(ui->index_list);
    g_signal_connect(ui->sel, "changed", G_CALLBACK(on_list_selection_changed), ui->index_remove_button);

    // Exclude model
    ui->exclude_list = GTK_TREE_VIEW(builder_init_widget(ui->builder, "exclude_list", "help_exclude_list"));
    ui->exclude_model = pref_exclude_treeview_init(ui->exclude_list, new_config->exclude_locations);

    ui->exclude_add_button = builder_init_widget(ui->builder, "exclude_add_button", "help_exclude_add");
    g_signal_connect(ui->exclude_add_button, "clicked", G_CALLBACK(on_exclude_add_button_clicked), ui->exclude_model);

    ui->exclude_remove_button = builder_init_widget(ui->builder, "exclude_remove_button", "help_exclude_remove");
    g_signal_connect(ui->exclude_remove_button, "clicked", G_CALLBACK(on_remove_button_clicked), ui->exclude_list);

    ui->exclude_selection = gtk_tree_view_get_selection(ui->exclude_list);
    g_signal_connect(ui->exclude_selection, "changed", G_CALLBACK(on_list_selection_changed), ui->exclude_remove_button);

    ui->exclude_hidden_items_button = toggle_button_get(ui->builder,
                                                        "exclude_hidden_items_button",
                                                        "help_exclude_hidden",
                                                        new_config->exclude_hidden_items);

    ui->exclude_files_entry = GTK_ENTRY(builder_init_widget(ui->builder, "exclude_files_entry", "help_exclude_files"));
    ui->exclude_files_str = NULL;
    if (new_config->exclude_files) {
        ui->exclude_files_str = g_strjoinv(";", new_config->exclude_files);
        GtkEntryBuffer *buffer = gtk_entry_get_buffer(ui->exclude_files_entry);
        gtk_entry_buffer_set_text(buffer, ui->exclude_files_str, -1);
    }
}

void
preferences_ui_launch(FsearchConfig *config,
                      GtkWindow *window,
                      FsearchPreferencesPage page,
                      void (*finsihed_cb)(FsearchConfig *)) {
    FsearchPreferencesInterface *ui = calloc(1, sizeof(FsearchPreferencesInterface));
    g_assert(ui != NULL);
    ui->new_config = config;
    ui->finished_cb = finsihed_cb;
    ui->window = window;

    preferences_ui_init(ui, page);

    gtk_widget_show(ui->dialog);
}

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

#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#include "btree.h"
#include "clipboard.h"
#include "database_search.h"
#include "fsearch_config.h"
#include "fsearch_limits.h"
#include "fsearch_window_actions.h"
#include "list_model.h"
#include "listview.h"
#include "ui_utils.h"
#include "utils.h"

static void
action_set_active_bool(GActionGroup *group, const gchar *action_name, bool value) {
    g_assert(G_IS_ACTION_GROUP(group));
    g_assert(G_IS_ACTION_MAP(group));

    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(group), action_name);

    if (action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(value));
    }
}

static void
action_set_enabled(GActionGroup *group, const gchar *action_name, bool value) {
    g_assert(G_IS_ACTION_GROUP(group));
    g_assert(G_IS_ACTION_MAP(group));

    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(group), action_name);

    if (action) {
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), value);
    }
}

static bool
confirm_action(GtkWidget *parent, const char *title, const char *question, int limit, int value) {
    if (value < limit) {
        return true;
    }

    gint response = ui_utils_run_gtk_dialog(parent, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, title, question);
    if (response == GTK_RESPONSE_YES) {
        return true;
    }
    return false;
}

static bool
confirm_file_open_action(GtkWidget *parent, int num_files) {
    char question[1024] = "";
    snprintf(question, sizeof(question), _("Do you really want to open %d file(s)?"), num_files);

    return confirm_action(parent, _("Opening Files…"), question, 10, num_files);
}

static GList *
build_entry_list(GList *selection, GtkTreeModel *model) {
    if (!selection || !model) {
        return NULL;
    }

    GList *entry_list = NULL;
    GList *temp = selection;
    while (temp) {
        GtkTreePath *path = temp->data;
        GtkTreeIter iter = {0};
        if (gtk_tree_model_get_iter(model, &iter, path)) {
            entry_list = g_list_append(entry_list, iter.user_data);
        }
        temp = temp->next;
    }
    return entry_list;
}

static void
copy_file(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata) {
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    GList **file_list = (GList **)userdata;
    if (!entry) {
        return;
    }

    BTreeNode *node = db_search_entry_get_node(entry);
    char path_str[PATH_MAX] = "";
    bool res = btree_node_get_path_full(node, path_str, sizeof(path_str));
    if (res) {
        *file_list = g_list_prepend(*file_list, g_strdup(path_str));
    }
}

static bool
delete_file(DatabaseSearchEntry *entry, bool delete) {
    if (!entry) {
        return false;
    }

    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return false;
    }

    if ((delete &&node_delete(node)) || (!delete &&node_move_to_trash(node))) {
        return true;
    }
    return false;
}

static void
fsearch_delete_selection(GSimpleAction *action, GVariant *variant, bool delete, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    if (!selection) {
        return;
    }

    GtkTreeModel *model = NULL;
    GList *selected_rows = gtk_tree_selection_get_selected_rows(selection, &model);
    guint num_selected_rows = g_list_length(selected_rows);

    if (delete || num_selected_rows > 20) {
        char error_msg[PATH_MAX] = "";
        snprintf(error_msg, sizeof(error_msg), _("Do you really want to remove %d file(s)?"), num_selected_rows);
        gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                delete ? _("Deleting files…") : _("Moving files to trash…"),
                                                error_msg);
        if (response != GTK_RESPONSE_OK) {
            goto save_fail;
        }
    }
    GList *selected_entries = build_entry_list(selected_rows, model);

    bool removed_files = false;
    GList *temp = selected_entries;
    while (temp) {
        if (temp->data) {
            removed_files = delete_file(temp->data, delete);
        }
        temp = temp->next;
    }

    if (removed_files) {
        // Files were removed, update the listview
        GtkTreeView *view = fsearch_application_window_get_listview(self);
        gtk_widget_queue_draw(GTK_WIDGET(view));
    }

    if (selected_entries) {
        g_list_free(selected_entries);
    }

save_fail:
    if (selected_rows) {
        g_list_free_full(selected_rows, (GDestroyNotify)gtk_tree_path_free);
    }
}

static void
fsearch_window_action_move_to_trash(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    fsearch_delete_selection(action, variant, false, user_data);
}

static void
fsearch_window_action_delete(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    fsearch_delete_selection(action, variant, true, user_data);
}

static void
fsearch_window_action_invert_selection(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    // TODO: can be very slow. Find a way how to optimize that.
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    if (!selection) {
        return;
    }
    GtkTreeModel *model = NULL;
    GList *selected_rows = gtk_tree_selection_get_selected_rows(selection, &model);
    if (!selected_rows) {
        return;
    }
    if (!model) {
        return;
    }
    fsearch_window_listview_block_selection_changed(self, TRUE);
    gtk_tree_selection_select_all(selection);

    GList *temp = selected_rows;
    while (temp) {
        GtkTreePath *path = temp->data;
        GtkTreeIter iter = {0};
        if (gtk_tree_model_get_iter(model, &iter, path)) {
            gtk_tree_selection_unselect_iter(selection, &iter);
        }
        temp = temp->next;
    }
    fsearch_window_listview_block_selection_changed(self, FALSE);
    fsearch_window_listview_selection_changed(self);
    g_list_free_full(selected_rows, (GDestroyNotify)gtk_tree_path_free);
}

static void
fsearch_window_action_deselect_all(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    if (selection) {
        gtk_tree_selection_unselect_all(selection);
    }
}

static void
fsearch_window_action_select_all(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    GtkEntry *entry = fsearch_application_window_get_search_entry(self);
    if (entry && gtk_widget_is_focus(GTK_WIDGET(entry))) {
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    }
    else if (selection) {
        gtk_tree_selection_select_all(selection);
    }
}

static void
fsearch_window_action_cut_or_copy(GSimpleAction *action, GVariant *variant, bool copy, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    if (!selection) {
        return;
    }
    GList *file_list = NULL;
    gtk_tree_selection_selected_foreach(selection, copy_file, &file_list);
    file_list = g_list_reverse(file_list);
    clipboard_copy_file_list(file_list, copy);
}

static void
fsearch_window_action_cut(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    fsearch_window_action_cut_or_copy(action, variant, false, user_data);
}

static void
fsearch_window_action_copy(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    fsearch_window_action_cut_or_copy(action, variant, true, user_data);
}

static void
fsearch_window_action_copy_filepath(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    if (!selection) {
        return;
    }
    GList *file_list = NULL;
    gtk_tree_selection_selected_foreach(selection, copy_file, &file_list);
    file_list = g_list_reverse(file_list);
    clipboard_copy_filepath_list(file_list);
}

static void
open_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (!entry) {
        return;
    }
    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return;
    }
    if (!launch_node(node)) {
        bool *open_failed = data;
        *open_failed = true;
    }
}

static void
open_with_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (!entry) {
        return;
    }
    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return;
    }
    char path_name[PATH_MAX] = "";
    btree_node_get_path_full(node, path_name, sizeof(path_name));
    GList **list = data;
    *list = g_list_append(*list, g_file_new_for_path(path_name));
}

void
fsearch_window_action_after_file_open(bool action_mouse) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    if ((config->action_after_file_open_keyboard && !action_mouse)
        || (config->action_after_file_open_mouse && action_mouse)) {
        if (config->action_after_file_open == ACTION_AFTER_OPEN_CLOSE) {
            g_application_quit(G_APPLICATION(FSEARCH_APPLICATION_DEFAULT));
        }
        else if (config->action_after_file_open == ACTION_AFTER_OPEN_MINIMIZE) {
            gtk_window_iconify(gtk_application_get_active_window(GTK_APPLICATION(FSEARCH_APPLICATION_DEFAULT)));
        }
    }
}

static void
launch_selection_for_app_info(FsearchApplicationWindow *win, GAppInfo *app_info) {
    if (!app_info) {
        return;
    }

    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(win));
    GdkAppLaunchContext *launch_context = gdk_display_get_app_launch_context(display);
    if (!launch_context) {
        return;
    }

    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(win);
    if (!selection) {
        return;
    }
    guint selected_rows = gtk_tree_selection_count_selected_rows(selection);
    if (!confirm_file_open_action(GTK_WIDGET(win), selected_rows)) {
        return;
    }

    GList *file_list = NULL;
    gtk_tree_selection_selected_foreach(selection, open_with_cb, &file_list);
    g_app_info_launch(app_info, file_list, G_APP_LAUNCH_CONTEXT(launch_context), NULL);

    g_object_unref(launch_context);
    launch_context = NULL;

    if (file_list) {
        g_list_free_full(file_list, g_object_unref);
        file_list = NULL;
    }
}

static void
fsearch_window_action_open_with(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    const char *app_id = g_variant_get_string(variant, NULL);
    if (!app_id) {
        return;
    }
    GDesktopAppInfo *app_info = g_desktop_app_info_new(app_id);
    if (!app_info) {
        return;
    }
    launch_selection_for_app_info(self, G_APP_INFO(app_info));

    g_object_unref(app_info);
    app_info = NULL;
}

static void
on_failed_to_open_file_response(GtkDialog *dialig, GtkResponseType response, gpointer user_data) {
    if (response != GTK_RESPONSE_YES) {
        fsearch_window_action_after_file_open(false);
    }
}

static void
fsearch_window_action_open_generic(FsearchApplicationWindow *win, GtkTreeSelectionForeachFunc open_func) {
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(win);
    if (!selection) {
        return;
    }
    guint selected_rows = gtk_tree_selection_count_selected_rows(selection);
    if (!confirm_file_open_action(GTK_WIDGET(win), selected_rows)) {
        return;
    }

    bool open_failed = false;
    gtk_tree_selection_selected_foreach(selection, open_func, &open_failed);
    if (!open_failed) {
        // open succeeded
        fsearch_window_action_after_file_open(false);
    }
    else {
        // open failed
        FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
        if (config->show_dialog_failed_opening) {
            ui_utils_run_gtk_dialog_async(GTK_WIDGET(win),
                                          GTK_MESSAGE_WARNING,
                                          GTK_BUTTONS_YES_NO,
                                          _("Failed to open file"),
                                          _("Do you want to keep the window open?"),
                                          G_CALLBACK(on_failed_to_open_file_response),
                                          NULL);
        }
    }
}

static void
fsearch_window_action_close_window(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    fsearch_application_window_prepare_shutdown(self);
    fsearch_application_window_prepare_close(self);
    gtk_widget_destroy(GTK_WIDGET(self));
}

static void
fsearch_window_action_open(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, open_cb);
}

static void
open_folder_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (!entry) {
        return;
    }
    BTreeNode *node = db_search_entry_get_node(entry);
    if (!node) {
        return;
    }
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (!launch_node_path(node, config->folder_open_cmd)) {
        bool *open_failed = data;
        *open_failed = true;
    }
}

static void
fsearch_window_action_open_folder(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, open_folder_cb);
}

static void
fsearch_window_action_open_with_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }

    FsearchApplicationWindow *self = user_data;
    GAppInfo *app_info = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
    gtk_widget_destroy(GTK_WIDGET(dialog));

    launch_selection_for_app_info(self, app_info);

    g_object_unref(app_info);
}
static void
fsearch_window_action_open_with_other(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    const char *content_type = g_variant_get_string(variant, NULL);
    if (!content_type) {
        content_type = "";
    }

    GtkWidget *app_chooser_dlg =
        gtk_app_chooser_dialog_new_for_content_type(GTK_WINDOW(self), GTK_DIALOG_MODAL, content_type);
    gtk_widget_show(app_chooser_dlg);

    GtkWidget *widget = gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(app_chooser_dlg));
    g_object_set(widget, "show-fallback", TRUE, "show-other", TRUE, NULL);

    g_signal_connect(app_chooser_dlg, "response", G_CALLBACK(fsearch_window_action_open_with_response_cb), self);
}

static void
fsearch_window_action_toggle_focus(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkWidget *widget = GTK_WIDGET(fsearch_application_window_get_search_entry(self));
    if (gtk_widget_is_focus(widget)) {
        widget = GTK_WIDGET(fsearch_application_window_get_listview(self));
    }
    gtk_widget_grab_focus(widget);
}

static void
fsearch_window_action_focus_search(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkWidget *entry = GTK_WIDGET(fsearch_application_window_get_search_entry(self));
    gtk_widget_grab_focus(entry);
}

static void
fsearch_window_action_hide_window(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    gtk_window_iconify(GTK_WINDOW(self));
}

static void
fsearch_window_action_show_filter(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_filter = g_variant_get_boolean(variant);
    fsearch_window_apply_search_revealer_config(self);
}

static void
fsearch_window_action_show_search_button(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_search_button = g_variant_get_boolean(variant);
    fsearch_window_apply_search_revealer_config(self);
}

static void
fsearch_window_action_show_statusbar(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_statusbar = g_variant_get_boolean(variant);
    fsearch_window_apply_statusbar_revealer_config(self);
}

static void
fsearch_window_action_search_in_path(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool search_in_path_old = config->search_in_path;
    config->search_in_path = g_variant_get_boolean(variant);
    GtkWidget *revealer = fsearch_application_window_get_search_in_path_revealer(self);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), config->search_in_path);
    if (search_in_path_old != config->search_in_path) {
        g_idle_add((GSourceFunc)fsearch_application_window_update_search, self);
    }
}

static void
fsearch_window_action_search_mode(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool enable_regex_old = config->enable_regex;
    config->enable_regex = g_variant_get_boolean(variant);
    GtkWidget *revealer = fsearch_application_window_get_search_mode_revealer(self);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), config->enable_regex);
    if (enable_regex_old != config->enable_regex) {
        g_idle_add((GSourceFunc)fsearch_application_window_update_search, self);
    }
}

static void
fsearch_window_action_match_case(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool match_case_old = config->match_case;
    config->match_case = g_variant_get_boolean(variant);
    GtkWidget *revealer = fsearch_application_window_get_match_case_revealer(self);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), config->match_case);
    if (match_case_old != config->match_case) {
        g_idle_add((GSourceFunc)fsearch_application_window_update_search, self);
    }
}

static void
fsearch_window_action_show_name_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    GtkTreeView *list = GTK_TREE_VIEW(fsearch_application_window_get_listview(self));
    if (value == FALSE) {
        listview_remove_column(list, LIST_MODEL_COL_NAME);
    }
    else {
        listview_add_column(list, LIST_MODEL_COL_NAME, 250, 0, self);
    }
    // FsearchConfig *config = fsearch_application_get_config
    // (FSEARCH_APPLICATION_DEFAULT); config->show_name_column =
    // g_variant_get_boolean (variant);
}

static void
fsearch_window_action_show_path_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    GtkTreeView *list = GTK_TREE_VIEW(fsearch_application_window_get_listview(self));
    if (value == FALSE) {
        listview_remove_column(list, LIST_MODEL_COL_PATH);
    }
    else {
        listview_add_column(list, LIST_MODEL_COL_PATH, 250, 1, self);
    }
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_path_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_type_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    GtkTreeView *list = GTK_TREE_VIEW(fsearch_application_window_get_listview(self));
    if (value == FALSE) {
        listview_remove_column(list, LIST_MODEL_COL_TYPE);
    }
    else {
        listview_add_column(list, LIST_MODEL_COL_TYPE, 100, 2, self);
    }
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_type_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_size_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    GtkTreeView *list = GTK_TREE_VIEW(fsearch_application_window_get_listview(self));
    if (value == FALSE) {
        listview_remove_column(list, LIST_MODEL_COL_SIZE);
    }
    else {
        listview_add_column(list, LIST_MODEL_COL_SIZE, 75, 3, self);
    }
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_size_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_modified_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    GtkTreeView *list = GTK_TREE_VIEW(fsearch_application_window_get_listview(self));
    if (value == FALSE) {
        listview_remove_column(list, LIST_MODEL_COL_CHANGED);
    }
    else {
        listview_add_column(list, LIST_MODEL_COL_CHANGED, 75, 4, self);
    }
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_modified_column = g_variant_get_boolean(variant);
}

static void
action_toggle_state_cb(GSimpleAction *saction, GVariant *parameter, gpointer user_data) {
    GAction *action = G_ACTION(saction);

    GVariant *state = g_action_get_state(action);
    g_action_change_state(action, g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_variant_unref(state);
}

static GActionEntry FsearchWindowActions[] = {
    {"open", fsearch_window_action_open},
    {"open_with", fsearch_window_action_open_with, "s"},
    {"open_with_other", fsearch_window_action_open_with_other, "s"},
    {"open_folder", fsearch_window_action_open_folder},
    {"close_window", fsearch_window_action_close_window},
    {"copy_clipboard", fsearch_window_action_copy},
    {"copy_filepath_clipboard", fsearch_window_action_copy_filepath},
    {"cut_clipboard", fsearch_window_action_cut},
    {"move_to_trash", fsearch_window_action_move_to_trash},
    {"delete_selection", fsearch_window_action_delete},
    {"select_all", fsearch_window_action_select_all},
    {"deselect_all", fsearch_window_action_deselect_all},
    {"invert_selection", fsearch_window_action_invert_selection},
    {"toggle_focus", fsearch_window_action_toggle_focus},
    {"focus_search", fsearch_window_action_focus_search},
    {"hide_window", fsearch_window_action_hide_window},
    // Column popup
    {"show_name_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_name_column},
    {"show_path_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_path_column},
    {"show_type_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_type_column},
    {"show_size_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_size_column},
    {"show_modified_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_modified_column},
    //{ "update_database",     fsearch_window_action_update_database },
    // View
    {"show_statusbar", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_statusbar},
    {"show_filter", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_filter},
    {"show_search_button", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_search_button},
    // Search
    {"search_in_path", action_toggle_state_cb, NULL, "true", fsearch_window_action_search_in_path},
    {"search_mode", action_toggle_state_cb, NULL, "true", fsearch_window_action_search_mode},
    {"match_case", action_toggle_state_cb, NULL, "true", fsearch_window_action_match_case},
};

void
fsearch_window_actions_update(FsearchApplicationWindow *self) {
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection(self);
    GtkTreeView *treeview = gtk_tree_selection_get_tree_view(selection);

    gint num_rows = 0;
    if (treeview) {
        GtkTreeModel *model = gtk_tree_view_get_model(treeview);
        if (model) {
            num_rows = gtk_tree_model_iter_n_children(model, NULL);
        }
    }

    GActionGroup *group = G_ACTION_GROUP(self);

    gint num_rows_selected = gtk_tree_selection_count_selected_rows(selection);
    action_set_enabled(group, "close_window", TRUE);
    action_set_enabled(group, "select_all", num_rows);
    action_set_enabled(group, "deselect_all", num_rows_selected);
    action_set_enabled(group, "invert_selection", num_rows_selected);
    action_set_enabled(group, "copy_clipboard", num_rows_selected);
    action_set_enabled(group, "copy_filepath_clipboard", num_rows_selected);
    action_set_enabled(group, "cut_clipboard", num_rows_selected);
    action_set_enabled(group, "delete_selection", num_rows_selected);
    action_set_enabled(group, "move_to_trash", num_rows_selected);
    action_set_enabled(group, "open", num_rows_selected);
    action_set_enabled(group, "open_with", num_rows_selected >= 1 ? TRUE : FALSE);
    action_set_enabled(group, "open_with_other", num_rows_selected >= 1 ? TRUE : FALSE);
    action_set_enabled(group, "open_folder", num_rows_selected);
    action_set_enabled(group, "focus_search", TRUE);
    action_set_enabled(group, "toggle_focus", TRUE);
    action_set_enabled(group, "hide_window", TRUE);
    action_set_enabled(group, "update_database", TRUE);
    action_set_enabled(group, "show_statusbar", TRUE);
    action_set_enabled(group, "show_filter", TRUE);
    action_set_enabled(group, "show_search_button", TRUE);
    action_set_enabled(group, "show_name_column", FALSE);
    action_set_enabled(group, "show_path_column", TRUE);
    action_set_enabled(group, "show_type_column", TRUE);
    action_set_enabled(group, "show_size_column", TRUE);
    action_set_enabled(group, "show_modified_column", TRUE);

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    action_set_active_bool(group, "show_statusbar", config->show_statusbar);
    action_set_active_bool(group, "show_filter", config->show_filter);
    action_set_active_bool(group, "show_search_button", config->show_search_button);
    action_set_active_bool(group, "search_in_path", config->search_in_path);
    action_set_active_bool(group, "search_mode", config->enable_regex);
    action_set_active_bool(group, "match_case", config->match_case);
    action_set_active_bool(group, "show_name_column", true);
    action_set_active_bool(group, "show_path_column", config->show_path_column);
    action_set_active_bool(group, "show_type_column", config->show_type_column);
    action_set_active_bool(group, "show_size_column", config->show_size_column);
    action_set_active_bool(group, "show_modified_column", config->show_modified_column);
}

void
fsearch_window_actions_init(FsearchApplicationWindow *self) {
    g_action_map_add_action_entries(G_ACTION_MAP(self), FsearchWindowActions, G_N_ELEMENTS(FsearchWindowActions), self);

    fsearch_window_actions_update(self);
}

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

#include "fsearch_clipboard.h"
#include "fsearch_config.h"
#include "fsearch_database_entry.h"
#include "fsearch_file_utils.h"
#include "fsearch_list_view.h"
#include "fsearch_statusbar.h"
#include "fsearch_ui_utils.h"
#include "fsearch_window_actions.h"

static void
action_set_active_int(GActionGroup *group, const gchar *action_name, int32_t value) {
    g_assert(G_IS_ACTION_GROUP(group));
    g_assert(G_IS_ACTION_MAP(group));

    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(group), action_name);

    if (action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_int32(value));
    }
}

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

    const gint response = ui_utils_run_gtk_dialog(parent, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, title, question);
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

static void
prepend_path_uri_to_array(gpointer key, gpointer value, gpointer user_data) {
    if (!value) {
        return;
    }

    GPtrArray **file_array = (GPtrArray **)user_data;
    FsearchDatabaseEntry *entry = value;
    GString *path_full = db_entry_get_path_full(entry);
    if (!path_full) {
        return;
    }
    char *file_uri = g_filename_to_uri(g_string_free(g_steal_pointer(&path_full), FALSE), NULL, NULL);
    if (file_uri) {
        g_ptr_array_add(*file_array, file_uri);
    }
}

static void
prepend_path(gpointer key, gpointer value, gpointer user_data) {
    if (!value) {
        return;
    }

    GList **file_list = (GList **)user_data;
    FsearchDatabaseEntry *entry = value;
    GString *path_full = db_entry_get_path_full(entry);
    if (!path_full) {
        return;
    }
    *file_list = g_list_prepend(*file_list, g_string_free(g_steal_pointer(&path_full), FALSE));
}

static bool
delete_file(const char *path, bool delete) {
    if (!path) {
        return false;
    }

    if ((delete &&fsearch_file_utils_remove(path)) || (!delete &&fsearch_file_utils_trash(path))) {
        return true;
    }
    return false;
}

static void
fsearch_delete_selection(GSimpleAction *action, GVariant *variant, bool delete, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    const guint num_selected_rows = fsearch_application_window_get_num_selected(self);
    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(self, prepend_path, &file_list);

    if (delete || num_selected_rows > 20) {
        GString *warning_message = g_string_new(NULL);
        g_string_printf(warning_message, _("Do you really want to remove %d file(s)?"), num_selected_rows);
        gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                delete ? _("Deleting files…") : _("Moving files to trash…"),
                                                warning_message->str);
        g_string_free(g_steal_pointer(&warning_message), TRUE);

        if (response != GTK_RESPONSE_OK) {
            goto save_fail;
        }
    }
    bool removed_files = false;
    GList *temp = file_list;
    while (temp) {
        if (temp->data) {
            removed_files = delete_file(temp->data, delete) ? true : removed_files;
        }
        temp = temp->next;
    }

    if (removed_files) {
        // Files were removed, update the listview
        FsearchListView *view = fsearch_application_window_get_listview(self);
        gtk_widget_queue_draw(GTK_WIDGET(view));
    }

save_fail:
    if (file_list) {
        g_list_free_full(g_steal_pointer(&file_list), (GDestroyNotify)g_free);
    }
}

static void
fsearch_window_action_file_properties(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    const guint num_selected_rows = fsearch_application_window_get_num_selected(self);
    GPtrArray *file_array = g_ptr_array_sized_new(num_selected_rows);
    fsearch_application_window_selection_for_each(self, prepend_path_uri_to_array, &file_array);

    if (num_selected_rows > 20) {
        GString *warning_message = g_string_new(NULL);
        g_string_printf(warning_message, _("Do you really want to open %d file property windows?"), num_selected_rows);
        gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                _("Opening file properties…"),
                                                warning_message->str);
        g_string_free(g_steal_pointer(&warning_message), TRUE);

        if (response != GTK_RESPONSE_OK) {
            goto save_fail;
        }
    }

    // ensure we have a NULL terminated array
    g_ptr_array_add(file_array, NULL);

    char **file_uris = (char **)g_ptr_array_free(file_array, FALSE);
    if (file_uris) {
        GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(FSEARCH_APPLICATION_DEFAULT));
        if (connection) {
            GError *error = NULL;
            g_dbus_connection_call_sync(connection,
                                        "org.freedesktop.FileManager1",
                                        "/org/freedesktop/FileManager1",
                                        "org.freedesktop.FileManager1",
                                        "ShowItemProperties",
                                        g_variant_new("(^ass)", file_uris, ""),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
            if (error) {
                g_debug("[file_properties] %s", error->message);
                g_clear_pointer(&error, g_error_free);
            }
        }
        g_clear_pointer(&file_uris, g_strfreev);
    }

    return;

save_fail:
    if (file_array) {
        g_ptr_array_set_free_func(file_array, (GDestroyNotify)g_free);
        g_ptr_array_free(g_steal_pointer(&file_array), TRUE);
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
    FsearchApplicationWindow *self = user_data;
    fsearch_application_window_invert_selection(self);
}

static void
fsearch_window_action_deselect_all(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_application_window_unselect_all(self);
}

static void
fsearch_window_action_select_all(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkEntry *entry = fsearch_application_window_get_search_entry(self);
    if (entry && gtk_widget_is_focus(GTK_WIDGET(entry))) {
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    }
    else {
        fsearch_application_window_select_all(self);
    }
}

static void
fsearch_window_action_cut_or_copy(GSimpleAction *action, GVariant *variant, bool copy, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(self, prepend_path, &file_list);
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
    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(self, prepend_path, &file_list);
    file_list = g_list_reverse(file_list);
    clipboard_copy_filepath_list(file_list);
}

static void
open_cb(gpointer key, gpointer value, gpointer data) {
    if (!value) {
        return;
    }
    FsearchDatabaseEntry *entry = value;
    GString *path_full = db_entry_get_path_full(entry);

    if (!fsearch_file_utils_launch(path_full)) {
        bool *open_failed = data;
        *open_failed = true;
    }
    g_string_free(g_steal_pointer(&path_full), TRUE);
}

static void
open_with_cb(gpointer key, gpointer value, gpointer data) {
    if (!value) {
        return;
    }
    FsearchDatabaseEntry *entry = value;

    GString *path_full = db_entry_get_path_full(entry);
    if (!path_full) {
        return;
    }
    GList **list = data;
    *list = g_list_append(*list, g_file_new_for_path(path_full->str));
    g_string_free(g_steal_pointer(&path_full), TRUE);
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

    const guint selected_rows = fsearch_application_window_get_num_selected(win);
    if (!confirm_file_open_action(GTK_WIDGET(win), (gint)selected_rows)) {
        return;
    }

    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(win));
    GdkAppLaunchContext *launch_context = gdk_display_get_app_launch_context(display);
    if (!launch_context) {
        return;
    }

    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(win, open_with_cb, &file_list);
    g_app_info_launch(app_info, file_list, G_APP_LAUNCH_CONTEXT(launch_context), NULL);

    g_clear_object(&launch_context);

    if (file_list) {
        g_list_free_full(g_steal_pointer(&file_list), g_object_unref);
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

    g_clear_object(&app_info);
}

static void
on_failed_to_open_file_response(GtkDialog *dialig, GtkResponseType response, gpointer user_data) {
    if (response != GTK_RESPONSE_YES) {
        fsearch_window_action_after_file_open(false);
    }
}

static void
fsearch_window_action_open_generic(FsearchApplicationWindow *win, GHFunc open_func) {
    const guint selected_rows = fsearch_application_window_get_num_selected(win);
    if (!confirm_file_open_action(GTK_WIDGET(win), selected_rows)) {
        return;
    }

    bool open_failed = false;
    fsearch_application_window_selection_for_each(win, open_func, &open_failed);
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
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    fsearch_application_window_prepare_shutdown(self);
    gtk_widget_destroy(GTK_WIDGET(self));
}

static void
fsearch_window_action_open(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, open_cb);
}

static void
open_folder_cb(gpointer key, gpointer value, gpointer data) {
    if (!value) {
        return;
    }
    FsearchDatabaseEntry *entry = value;

    GString *path = db_entry_get_path(entry);
    GString *path_full = db_entry_get_path_full(entry);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (!fsearch_file_utils_launch_with_command(path, path_full, config->folder_open_cmd)) {
        bool *open_failed = data;
        *open_failed = true;
    }
    g_string_free(g_steal_pointer(&path), TRUE);
    g_string_free(g_steal_pointer(&path_full), TRUE);
}

static void
fsearch_window_action_open_folder(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, open_folder_cb);
}

static void
on_fsearch_window_action_open_with_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }

    FsearchApplicationWindow *self = user_data;
    GAppInfo *app_info = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
    gtk_widget_destroy(GTK_WIDGET(dialog));

    launch_selection_for_app_info(self, app_info);

    g_clear_object(&app_info);
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

    g_signal_connect(app_chooser_dlg, "response", G_CALLBACK(on_fsearch_window_action_open_with_response), self);
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
    fsearch_application_window_apply_search_revealer_config(self);
}

static void
fsearch_window_action_show_search_button(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_search_button = g_variant_get_boolean(variant);
    fsearch_application_window_apply_search_revealer_config(self);
}

static void
fsearch_window_action_show_statusbar(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_statusbar = g_variant_get_boolean(variant);
    fsearch_application_window_apply_statusbar_revealer_config(self);
}

static void
fsearch_window_action_search_in_path(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool search_in_path_old = config->search_in_path;
    config->search_in_path = g_variant_get_boolean(variant);
    FsearchStatusbar *sb = fsearch_application_window_get_statusbar(self);
    fsearch_statusbar_set_revealer_visibility(sb, FSEARCH_STATUSBAR_REVEALER_SEARCH_IN_PATH, config->search_in_path);
    if (search_in_path_old != config->search_in_path) {
        fsearch_application_window_update_query_flags(self);
    }
}

static void
fsearch_window_action_search_mode(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool enable_regex_old = config->enable_regex;
    config->enable_regex = g_variant_get_boolean(variant);
    FsearchStatusbar *sb = fsearch_application_window_get_statusbar(self);
    fsearch_statusbar_set_revealer_visibility(sb, FSEARCH_STATUSBAR_REVEALER_REGEX, config->enable_regex);
    if (enable_regex_old != config->enable_regex) {
        fsearch_application_window_update_query_flags(self);
    }
}

static void
fsearch_window_action_match_case(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    bool match_case_old = config->match_case;
    config->match_case = g_variant_get_boolean(variant);
    FsearchStatusbar *sb = fsearch_application_window_get_statusbar(self);
    fsearch_statusbar_set_revealer_visibility(sb, FSEARCH_STATUSBAR_REVEALER_MATCH_CASE, config->match_case);
    if (match_case_old != config->match_case) {
        fsearch_application_window_update_query_flags(self);
    }
}

static void
fsearch_window_action_set_filter(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    guint active_filter = g_variant_get_int32(variant);
    fsearch_application_window_set_active_filter(self, active_filter);
}

static void
fsearch_window_action_show_path_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    gboolean value = g_variant_get_boolean(variant);
    FsearchListView *list = FSEARCH_LIST_VIEW(fsearch_application_window_get_listview(self));
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_PATH);
    if (!col) {
        return;
    }
    fsearch_list_view_column_set_visible(list, col, value);
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_path_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_extension_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    FsearchListView *list = FSEARCH_LIST_VIEW(fsearch_application_window_get_listview(self));
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_EXTENSION);
    if (!col) {
        return;
    }
    fsearch_list_view_column_set_visible(list, col, value);
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_extension_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_type_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    FsearchListView *list = FSEARCH_LIST_VIEW(fsearch_application_window_get_listview(self));
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_FILETYPE);
    if (!col) {
        return;
    }
    fsearch_list_view_column_set_visible(list, col, value);
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_type_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_size_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    FsearchListView *list = FSEARCH_LIST_VIEW(fsearch_application_window_get_listview(self));
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_SIZE);
    if (!col) {
        return;
    }
    fsearch_list_view_column_set_visible(list, col, value);
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_size_column = g_variant_get_boolean(variant);
}

static void
fsearch_window_action_show_modified_column(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state(action, variant);
    gboolean value = g_variant_get_boolean(variant);
    FsearchListView *list = FSEARCH_LIST_VIEW(fsearch_application_window_get_listview(self));
    FsearchListViewColumn *col =
        fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_MODIFICATION_TIME);
    if (!col) {
        return;
    }
    fsearch_list_view_column_set_visible(list, col, value);
    g_simple_action_set_state(action, variant);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    config->show_modified_column = g_variant_get_boolean(variant);
}

static void
action_toggle_state_cb(GSimpleAction *saction, GVariant *parameter, gpointer user_data) {
    GAction *action = G_ACTION(saction);

    GVariant *state = g_action_get_state(action);
    g_action_change_state(action, g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_clear_pointer(&state, g_variant_unref);
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
    {"file_properties", fsearch_window_action_file_properties},
    {"move_to_trash", fsearch_window_action_move_to_trash},
    {"delete_selection", fsearch_window_action_delete},
    {"select_all", fsearch_window_action_select_all},
    {"deselect_all", fsearch_window_action_deselect_all},
    {"invert_selection", fsearch_window_action_invert_selection},
    {"toggle_focus", fsearch_window_action_toggle_focus},
    {"focus_search", fsearch_window_action_focus_search},
    {"hide_window", fsearch_window_action_hide_window},
    // Column popup
    {"show_path_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_path_column},
    {"show_type_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_type_column},
    {"show_extension_column", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_extension_column},
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
    {"filter", NULL, "i", "0", fsearch_window_action_set_filter},
};

void
fsearch_window_actions_update(FsearchApplicationWindow *self) {
    const gint num_rows = fsearch_application_window_get_num_results(self);

    GActionGroup *group = G_ACTION_GROUP(self);

    FsearchListView *view = fsearch_application_window_get_listview(self);
    const gint active_filter = fsearch_application_window_get_active_filter(self);

    gint num_rows_selected = 0;
    if (view) {
        num_rows_selected = fsearch_application_window_get_num_selected(self);
    }

    const bool has_file_manager_on_bus = fsearch_application_has_file_manager_on_bus(FSEARCH_APPLICATION_DEFAULT);

    action_set_enabled(group, "close_window", TRUE);
    action_set_enabled(group, "select_all", num_rows >= 1 ? TRUE : FALSE);
    action_set_enabled(group, "deselect_all", num_rows_selected);
    action_set_enabled(group, "invert_selection", num_rows_selected);
    action_set_enabled(group, "copy_clipboard", num_rows_selected);
    action_set_enabled(group, "copy_filepath_clipboard", num_rows_selected);
    action_set_enabled(group, "cut_clipboard", num_rows_selected);
    action_set_enabled(group, "delete_selection", FALSE);
    action_set_enabled(group, "file_properties", has_file_manager_on_bus && num_rows_selected >= 1 ? TRUE : FALSE);
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
    action_set_enabled(group, "show_extension_column", TRUE);
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
    action_set_active_bool(group, "show_extension_column", config->show_extension_column);
    action_set_active_bool(group, "show_size_column", config->show_size_column);
    action_set_active_bool(group, "show_modified_column", config->show_modified_column);
    action_set_active_int(group, "filter", active_filter);
}

void
fsearch_window_actions_init(FsearchApplicationWindow *self) {
    g_action_map_add_action_entries(G_ACTION_MAP(self), FsearchWindowActions, G_N_ELEMENTS(FsearchWindowActions), self);

    fsearch_window_actions_update(self);
}

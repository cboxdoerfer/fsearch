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

#ifndef __MACH__
#include <gio/gdesktopappinfo.h>
#endif

#include <glib/gi18n.h>
#include <stdint.h>

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
    g_return_if_fail(value);

    GPtrArray **file_array = (GPtrArray **)user_data;
    FsearchDatabaseEntry *entry = value;
    GString *path_full = db_entry_get_path_full(entry);
    g_return_if_fail(path_full);

    char *file_uri = g_filename_to_uri(g_string_free(g_steal_pointer(&path_full), FALSE), NULL, NULL);
    if (file_uri) {
        g_ptr_array_add(*file_array, file_uri);
    }
}

static void
prepend_string_to_list(GList **string_list,
                       FsearchDatabaseEntry *entry,
                       GString *(*get_string_func)(FsearchDatabaseEntry *)) {
    if (!entry || !string_list || !get_string_func) {
        return;
    }

    GString *string = get_string_func(entry);
    if (!string) {
        return;
    }
    *string_list = g_list_prepend(*string_list, g_string_free(g_steal_pointer(&string), FALSE));
}

static void
append_line(GString *str, const char *text) {
    if (str->len > 0) {
        g_string_append_c(str, '\n');
    }
    g_string_append(str, text);
}

static void
append_line_to_string(GString *buffer, FsearchDatabaseEntry *entry, GString *(*get_string_func)(FsearchDatabaseEntry *)) {
    if (!entry || !buffer || !get_string_func) {
        return;
    }

    g_autoptr(GString) string = get_string_func(entry);
    if (!string) {
        return;
    }
    append_line(buffer, string->str);
}

static void
prepend_full_path_to_list(gpointer key, gpointer value, gpointer user_data) {
    prepend_string_to_list(user_data, value, db_entry_get_path_full);
}

static void
append_full_path_to_string(gpointer key, gpointer value, gpointer user_data) {
    append_line_to_string(user_data, value, db_entry_get_path_full);
}

static void
append_path_to_string(gpointer key, gpointer value, gpointer user_data) {
    append_line_to_string(user_data, value, db_entry_get_path);
}

static void
append_name_to_string(gpointer key, gpointer value, gpointer user_data) {
    append_line_to_string(user_data, value, db_entry_get_name_for_display);
}

static void
fsearch_delete_selection(GSimpleAction *action, GVariant *variant, bool delete, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    g_autoptr(GString) error_message = g_string_new(NULL);

    const guint num_selected_rows = fsearch_application_window_get_num_selected(self);
    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(self, prepend_full_path_to_list, &file_list);

    if (delete || num_selected_rows > 20) {
        g_autoptr(GString) warning_message = g_string_new(NULL);
        g_string_printf(warning_message, _("Do you really want to remove %d file(s)?"), num_selected_rows);
        gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                delete ? _("Deleting files…") : _("Moving files to trash…"),
                                                warning_message->str);

        if (response != GTK_RESPONSE_OK) {
            goto save_fail;
        }
    }

    uint32_t num_trashed_or_deleted = 0;
    for (GList *f = file_list; f != NULL; f = f->next) {
        char *path = f->data;
        if (delete) {
            if (fsearch_file_utils_remove(path, error_message)) {
                num_trashed_or_deleted++;
            }
        }
        else {
            if (fsearch_file_utils_trash(path, error_message)) {
                num_trashed_or_deleted++;
            }
        }
    }

    if (error_message->len > 0) {
        ui_utils_run_gtk_dialog_async(GTK_WIDGET(self),
                                      GTK_MESSAGE_WARNING,
                                      GTK_BUTTONS_OK,
                                      _("Something went wrong."),
                                      error_message->str,
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
    }
    if (num_trashed_or_deleted > 0) {
        g_autoptr(GString) trashed_or_deleted_message = g_string_new(NULL);
        g_string_printf(trashed_or_deleted_message,
                        delete ? _("Deleted %d file(s).") : _("Moved %d file(s) to the trash."),
                        num_trashed_or_deleted);
        ui_utils_run_gtk_dialog_async(GTK_WIDGET(self),
                                      GTK_MESSAGE_INFO,
                                      GTK_BUTTONS_OK,
                                      trashed_or_deleted_message->str,
                                      _("The database needs to be updated before it becomes aware of those changes! "
                                        "This will be fixed with future updates."),
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
    }

save_fail:
    if (file_list) {
        g_list_free_full(g_steal_pointer(&file_list), (GDestroyNotify)g_free);
    }
}

static void
fsearch_window_action_file_properties(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(FSEARCH_APPLICATION_DEFAULT));
    if (!connection) {
        g_debug("[file_properties] failed to get bus connection");
    }

    const guint num_selected_rows = fsearch_application_window_get_num_selected(self);
    g_autoptr(GPtrArray) file_array = g_ptr_array_new_full(num_selected_rows, g_free);
    fsearch_application_window_selection_for_each(self, prepend_path_uri_to_array, &file_array);

    if (num_selected_rows > 20) {
        g_autoptr(GString) warning_message = g_string_new(NULL);
        g_string_printf(warning_message, _("Do you really want to open %d file property windows?"), num_selected_rows);
        gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                _("Opening file properties…"),
                                                warning_message->str);

        if (response != GTK_RESPONSE_OK) {
            return;
        }
    }

    // ensure we have a NULL terminated array
    g_ptr_array_add(file_array, NULL);

    g_auto(GStrv) file_uris = (GStrv)g_ptr_array_free(g_steal_pointer(&file_array), FALSE);
    if (!file_uris) {
        return;
    }
    g_autoptr(GError) error = NULL;
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
    fsearch_application_window_selection_for_each(self, prepend_full_path_to_list, &file_list);
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
copy_selection_as_text(FsearchApplicationWindow *win, GHFunc text_copy_func) {
    g_autoptr(GString) file_list_buffer = g_string_sized_new(8192);
    fsearch_application_window_selection_for_each(win, text_copy_func, file_list_buffer);

    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, file_list_buffer->str, (gint)file_list_buffer->len);
}

static void
fsearch_window_action_copy_full_path(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    copy_selection_as_text(win, append_full_path_to_string);
}

static void
fsearch_window_action_copy_path(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    copy_selection_as_text(win, append_path_to_string);
}

static void
fsearch_window_action_copy_name(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    copy_selection_as_text(win, append_name_to_string);
}

static void
collect_selected_entry_parent_path(gpointer key, FsearchDatabaseEntry *entry, GList **paths) {
    g_return_if_fail(paths);
    g_return_if_fail(entry);

    GString *parent_path = db_entry_get_path(entry);
    g_return_if_fail(parent_path);

    *paths = g_list_append(*paths, g_string_free(parent_path, FALSE));
}

static void
collect_selected_entry_path(gpointer key, FsearchDatabaseEntry *entry, GList **paths) {
    g_return_if_fail(paths);
    g_return_if_fail(entry);

    GString *path = db_entry_get_path_full(entry);
    g_return_if_fail(path);

    *paths = g_list_append(*paths, g_string_free(path, FALSE));
}

static void
append_path_to_list(gpointer key, gpointer value, gpointer data) {
    g_return_if_fail(value);

    FsearchDatabaseEntry *entry = value;

    GString *path_full = db_entry_get_path_full(entry);
    g_return_if_fail(path_full);

    GList **list = data;
    *list = g_list_append(*list, g_string_free(path_full, FALSE));
}

static void
append_file_to_list(gpointer key, gpointer value, gpointer data) {
    g_return_if_fail(value);

    FsearchDatabaseEntry *entry = value;

    g_autoptr(GString) path_full = db_entry_get_path_full(entry);
    g_return_if_fail(path_full);

    GList **list = data;
    *list = g_list_append(*list, g_file_new_for_path(path_full->str));
}

static void
action_after_open(bool action_mouse) {
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
    g_return_if_fail(win);
    g_return_if_fail(app_info);

    const guint selected_rows = fsearch_application_window_get_num_selected(win);
    if (!confirm_file_open_action(GTK_WIDGET(win), (gint)selected_rows)) {
        return;
    }

    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(win));
    g_autoptr(GdkAppLaunchContext) launch_context = gdk_display_get_app_launch_context(display);
    if (!launch_context) {
        return;
    }

    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(win, append_file_to_list, &file_list);
    if (file_list) {
        g_app_info_launch(app_info, file_list, G_APP_LAUNCH_CONTEXT(launch_context), NULL);
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
    #ifdef __MACH__
    g_autoptr(GAppInfo) app_info=NULL;
    #else
    g_autoptr(GDesktopAppInfo) app_info = g_desktop_app_info_new(app_id);
    #endif
    
    if (!app_info) {
        return;
    }
    launch_selection_for_app_info(self, G_APP_INFO(app_info));
}

typedef struct FsearchOpenPathContext {
    guint win_id;
    gboolean triggered_with_mouse;
    gboolean show_dialog_failed_opening;
} FsearchOpenPathContext;

static void
open_path_list_callback(gboolean result, const char *error_message, gpointer user_data) {
    g_autofree FsearchOpenPathContext *ctx = user_data;
    if (result) {
        // open succeeded
        action_after_open(ctx->triggered_with_mouse);
    }
    else if (error_message) {
        // open failed
        if (ctx->show_dialog_failed_opening) {
            GtkWindow *win = gtk_application_get_window_by_id(GTK_APPLICATION(FSEARCH_APPLICATION_DEFAULT), ctx->win_id);
            if (win) {
                ui_utils_run_gtk_dialog_async(GTK_WIDGET(win),
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_OK,
                                              _("Something went wrong."),
                                              error_message,
                                              G_CALLBACK(gtk_widget_destroy),
                                              NULL);
            }
        }
    }
}

void
fsearch_window_action_open_generic(FsearchApplicationWindow *win, bool open_parent_folder, bool triggered_with_mouse) {
    const guint selected_rows = fsearch_application_window_get_num_selected(win);
    if (!confirm_file_open_action(GTK_WIDGET(win), (gint)selected_rows)) {
        return;
    }

    g_autoptr(GString) error_message = g_string_sized_new(8192);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    GList *paths = NULL;
    if (open_parent_folder && !config->folder_open_cmd) {
        fsearch_application_window_selection_for_each(win, (GHFunc)collect_selected_entry_parent_path, &paths);
    }
    else {
        fsearch_application_window_selection_for_each(win, (GHFunc)collect_selected_entry_path, &paths);
    }

    if (open_parent_folder && config->folder_open_cmd) {
        fsearch_file_utils_open_path_list_with_command(paths, config->folder_open_cmd, error_message);

        if (error_message->len == 0) {
            // open succeeded
            action_after_open(triggered_with_mouse);
        }
        else {
            // open failed
            if (config->show_dialog_failed_opening) {
                ui_utils_run_gtk_dialog_async(GTK_WIDGET(win),
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_OK,
                                              _("Something went wrong."),
                                              error_message->str,
                                              G_CALLBACK(gtk_widget_destroy),
                                              NULL);
            }
        }
    }
    else {
        GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(win));
        g_autoptr(GdkAppLaunchContext) launch_context = gdk_display_get_app_launch_context(display);

        FsearchOpenPathContext *open_ctx = g_new0(FsearchOpenPathContext, 1);
        open_ctx->win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));
        open_ctx->triggered_with_mouse = triggered_with_mouse;
        open_ctx->show_dialog_failed_opening = config->show_dialog_failed_opening;

        fsearch_file_utils_open_path_list(paths,
                                          config->launch_desktop_files,
                                          G_APP_LAUNCH_CONTEXT(launch_context),
                                          open_path_list_callback,
                                          open_ctx);
    }

    g_list_free_full(paths, g_free);
}

static void
fsearch_window_action_close_window(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    fsearch_application_window_prepare_shutdown(self);
    gtk_widget_destroy(GTK_WIDGET(self));
}

static void
fsearch_window_action_toggle_app_menu(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_application_window_toggle_app_menu(self);
}

static void
fsearch_window_action_open(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, false, false);
}

static void
fsearch_window_action_open_folder(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_window_action_open_generic(self, true, false);
}

static void
on_fsearch_window_action_open_with_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id != GTK_RESPONSE_OK) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }

    FsearchApplicationWindow *self = user_data;
    g_autoptr(GAppInfo) app_info = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
    gtk_widget_destroy(GTK_WIDGET(dialog));

    launch_selection_for_app_info(self, app_info);
}

static void
fsearch_window_action_open_with_other(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    // The app chooser dialog expects us to provide the content type of the file we want to open.
    // Since we support multiple selections, it's possible that the content type isn't the same for
    // every selected file. So we just provide the content type of the first selected file instead.
    g_autofree char *content_type = NULL;
    GList *file_list = NULL;
    fsearch_application_window_selection_for_each(self, append_path_to_list, &file_list);
    if (file_list) {
        const char *first_selected_path = file_list->data;
        content_type = fsearch_file_utils_get_content_type(first_selected_path, NULL);
        g_list_free_full(g_steal_pointer(&file_list), g_free);
    }

    GtkWidget *app_chooser_dlg = gtk_app_chooser_dialog_new_for_content_type(GTK_WINDOW(self),
                                                                             GTK_DIALOG_MODAL,
                                                                             content_type ? content_type
                                                                                          : "application/octet-stream");
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
fsearch_window_action_cancel_current_task(GSimpleAction *action, GVariant *variant, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    fsearch_application_window_cancel_current_task(self);
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
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list, DATABASE_INDEX_TYPE_MODIFICATION_TIME);
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

    g_autoptr(GVariant) state = g_action_get_state(action);
    g_action_change_state(action, g_variant_new_boolean(!g_variant_get_boolean(state)));
}

static GActionEntry FsearchWindowActions[] = {
    {"toggle_app_menu", fsearch_window_action_toggle_app_menu},
    {"open", fsearch_window_action_open},
    {"open_with", fsearch_window_action_open_with, "s"},
    {"open_with_other", fsearch_window_action_open_with_other, "s"},
    {"open_folder", fsearch_window_action_open_folder},
    {"close_window", fsearch_window_action_close_window},
    {"copy_clipboard", fsearch_window_action_copy},
    {"copy_as_text_path_and_name_clipboard", fsearch_window_action_copy_full_path},
    {"copy_as_text_name_clipboard", fsearch_window_action_copy_name},
    {"copy_as_text_path_clipboard", fsearch_window_action_copy_path},
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
    {"cancel_task", action_toggle_state_cb, NULL, "true", fsearch_window_action_cancel_current_task},
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

    action_set_enabled(group, "toggle_app_menu", TRUE);
    action_set_enabled(group, "close_window", TRUE);
    action_set_enabled(group, "select_all", num_rows >= 1 ? TRUE : FALSE);
    action_set_enabled(group, "deselect_all", num_rows_selected);
    action_set_enabled(group, "invert_selection", num_rows_selected);
    action_set_enabled(group, "copy_clipboard", num_rows_selected);
    action_set_enabled(group, "copy_as_text_path_and_name_clipboard", num_rows_selected);
    action_set_enabled(group, "copy_as_text_name_clipboard", num_rows_selected);
    action_set_enabled(group, "copy_as_text_path_clipboard", num_rows_selected);
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

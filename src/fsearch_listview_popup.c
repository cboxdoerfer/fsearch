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

#include "fsearch.h"
#include "fsearch_listview_popup.h"

static void
add_file_properties_entry(GtkBuilder *builder) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    if (app && fsearch_application_has_file_manager_on_bus(app)) {
        GMenu *menu_properties_section = G_MENU(
            gtk_builder_get_object(builder, "fsearch_listview_menu_file_properties_section"));
        if (menu_properties_section) {
            g_autoptr(GMenuItem) properties_item = g_menu_item_new(_("Properties…"), "win.file_properties");
            g_menu_append_item(menu_properties_section, properties_item);
        }
    }
}

struct content_type_context {
    GHashTable *content_types;
    GHashTable *applications;
    bool first_run;
};

static gboolean
should_remove_application_for_application_list(gpointer key, gpointer value, gpointer user_data) {
    GList *app_infos = user_data;
    const char *current_app_id = key;
    for (GList *app_info_iter = app_infos; app_info_iter; app_info_iter = app_info_iter->next) {
        GAppInfo *app_info = app_info_iter->data;
        const char *app_id = g_app_info_get_id(app_info);
        if (!g_strcmp0(app_id, current_app_id)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void
refresh_applications_for_content_type(GHashTable *applications, const char *content_type, bool first_run) {

    GList *app_infos = g_app_info_get_all_for_type(content_type);
    if (!app_infos) {
        // there are no applications which can open this content type,
        // so we must remove everything from the hash table and return
        g_hash_table_remove_all(applications);
        return;
    }

    if (first_run) {
        // we can safely add all applications for the first entry we process
        for (GList *app_info_iter = app_infos; app_info_iter; app_info_iter = app_info_iter->next) {
            GAppInfo *app_info = app_info_iter->data;
            const char *app_id = g_app_info_get_id(app_info);
            g_hash_table_insert(applications, g_strdup(app_id), g_object_ref(app_info));
        }
    }
    else {
        // remove all applications which don't support the current content type
        g_hash_table_foreach_remove(applications, should_remove_application_for_application_list, app_infos);
    }

    g_list_free_full(g_steal_pointer(&app_infos), g_object_unref);
}

static void
intersect_supported_appliations(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseEntry *entry = value;
    if (G_UNLIKELY(!entry)) {
        return;
    }

    struct content_type_context *ctx = user_data;
    if (!ctx->first_run && g_hash_table_size(ctx->applications) == 0) {
        // there are already no applications which can open all processed entries,
        // hence we don't need to process the remaining entries
        return;
    }

    if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER) {
        // we already know the content type for folders, so we can use a slightly more
        // efficient and reliable path for them here
#ifdef _WIN32
        const char *dir_content_type = "application/x-directory";
#else
        const char *dir_content_type = "inode/directory";
#endif
        if (!g_hash_table_contains(ctx->content_types, dir_content_type)) {
            // no folder content type was added up until now, let's add one
            g_hash_table_add(ctx->content_types, g_strdup(dir_content_type));
            // refresh the table of applications which can open all selected entries
            refresh_applications_for_content_type(ctx->applications, dir_content_type, ctx->first_run);
            ctx->first_run = false;
        }
        return;
    }

    g_autoptr(GString) name = db_entry_get_name_for_display(entry);
    g_return_if_fail(name);

    g_autofree char *content_type = g_content_type_guess(name->str, NULL, 0, NULL);

    if (!g_hash_table_contains(ctx->content_types, content_type)) {
        // a new content type, add it
        g_hash_table_add(ctx->content_types, content_type);
        // refresh the table of applications which can open all selected entries
        refresh_applications_for_content_type(ctx->applications, g_steal_pointer(&content_type), ctx->first_run);
        ctx->first_run = false;
    }
}

static void
append_application_to_menu(gpointer key, gpointer value, gpointer user_data) {
    GMenu *menu_mime = user_data;
    GAppInfo *app_info = value;
    const char *display_name = g_app_info_get_display_name(app_info);
    const char *app_id = g_app_info_get_id(app_info);

    char detailed_action[1024] = "";
    snprintf(detailed_action, sizeof(detailed_action), "win.open_with('%s')", app_id);

    g_autoptr(GMenuItem) menu_item = g_menu_item_new(display_name, detailed_action);
    g_menu_item_set_icon(menu_item, g_app_info_get_icon(app_info));
    g_menu_append_item(menu_mime, menu_item);
}

static void
fill_open_with_menu(GtkBuilder *builder, FsearchDatabaseView *db_view) {

    struct content_type_context content_type_ctx = {};
    content_type_ctx.content_types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    content_type_ctx.applications = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    content_type_ctx.first_run = true;
    // find applications which can open all selected files
    // this basically computes the intersection of the lists of applications for each entry
    db_view_selection_for_each(db_view, intersect_supported_appliations, &content_type_ctx);
    g_hash_table_remove_all(content_type_ctx.content_types);
    g_clear_pointer(&content_type_ctx.content_types, g_hash_table_destroy);

    GMenu *menu_mime = G_MENU(gtk_builder_get_object(builder, "fsearch_listview_menu_open_with_mime_section"));

    // add the application menu entries to the menu
    g_hash_table_foreach(content_type_ctx.applications, append_application_to_menu, menu_mime);

    g_hash_table_remove_all(content_type_ctx.applications);
    g_clear_pointer(&content_type_ctx.applications, g_hash_table_destroy);

    // add the "Open with -> Other Application" entry
    char detailed_action[1024] = "";
    snprintf(detailed_action, sizeof(detailed_action), "win.open_with_other('%s')", "");
    g_autoptr(GMenuItem) open_with_item = g_menu_item_new(_("Other Application…"), detailed_action);
    g_menu_append_item(menu_mime, open_with_item);
}

gboolean
listview_popup_menu(GtkWidget *widget, FsearchDatabaseView *db_view) {
    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/menus.ui");

    fill_open_with_menu(builder, db_view);
    add_file_properties_entry(builder);

    GMenu *menu_root = G_MENU(gtk_builder_get_object(builder, "fsearch_listview_popup_menu"));
    GtkWidget *menu_widget = gtk_menu_new_from_model(G_MENU_MODEL(menu_root));

    gtk_menu_attach_to_widget(GTK_MENU(menu_widget), GTK_WIDGET(widget), NULL);
#if !GTK_CHECK_VERSION(3, 22, 0)
    gtk_menu_popup(GTK_MENU(menu_widget), NULL, NULL, NULL, NULL, GDK_BUTTON_SECONDARY, time);
#else
    gtk_menu_popup_at_pointer(GTK_MENU(menu_widget), NULL);
#endif
    return TRUE;
}

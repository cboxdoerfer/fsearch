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

#define G_LOG_DOMAIN "fsearch-utils"

#define _GNU_SOURCE

#include "fsearch_file_utils.h"
#include "fsearch_database.h"
#include "fsearch_limits.h"
#include "fsearch_ui_utils.h"
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <stdbool.h>
#include <stdio.h>

const char *data_folder_name = "fsearch";

void
fsearch_file_utils_init_data_dir_path(char *path, size_t len) {
    g_assert(path != NULL);
    g_assert(len >= 0);

    const gchar *xdg_data_dir = g_get_user_data_dir();
    snprintf(path, len, "%s/%s", xdg_data_dir, data_folder_name);
    return;
}

bool
fsearch_file_utils_create_dir(const char *path) {
    return !g_mkdir_with_parents(path, 0700);
}

static gboolean
keyword_eval_cb(const GMatchInfo *info, GString *res, gpointer data) {
    gchar *match = g_match_info_fetch(info, 0);
    if (!match) {
        return FALSE;
    }
    gchar *r = g_hash_table_lookup((GHashTable *)data, match);
    if (r) {
        g_string_append(res, r);
    }
    g_free(match);

    return FALSE;
}

static char *
build_folder_open_cmd(FsearchDatabaseEntry *entry, const char *cmd) {
    GString *path = db_entry_get_path(entry);
    if (!path) {
        return NULL;
    }
    GString *path_full = db_entry_get_path_full(entry);

    char *path_quoted = g_shell_quote(g_string_free(path, FALSE));
    char *path_full_quoted = g_shell_quote(g_string_free(path_full, FALSE));

    path = NULL;
    path_full = NULL;

    // The following code is mostly based on the example code found here:
    // https://developer.gnome.org/glib/stable/glib-Perl-compatible-regular-expressions.html#g-regex-replace-eval
    //
    // Create hash table which hold all valid keywords as keys
    // and their replacements as values
    // All valid keywords are:
    // - {path_raw}
    //     The raw path of a file or folder. E.g. the path of /foo/bar is /foo
    // - {path_full_raw}
    //     The raw full path of a file or folder. E.g. the full path of /foo/bar
    //     is /foo/bar
    // - {path_quoted} and {path_full_quoted}
    //     Those are the same as {path_raw} and {path_full_raw} but they get
    //     properly escaped and quoted for the usage in shells. E.g. /foo/'bar
    //     becomes '/foo/'\''bar'

    GHashTable *keywords = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(keywords, "{path_raw}", path);
    g_hash_table_insert(keywords, "{path_full_raw}", path_full);
    g_hash_table_insert(keywords, "{path}", path_quoted);
    g_hash_table_insert(keywords, "{path_full}", path_full_quoted);

    // Regular expression which matches multiple words (and underscores)
    // surrounded with {}
    GRegex *reg = g_regex_new("{[\\w]+}", 0, 0, NULL);
    // Replace all the matched keywords
    char *cmd_res = g_regex_replace_eval(reg, cmd, -1, 0, 0, keyword_eval_cb, keywords, NULL);

    g_regex_unref(reg);
    g_hash_table_destroy(keywords);
    g_free(path_quoted);
    path_quoted = NULL;
    g_free(path_full_quoted);
    path_full_quoted = NULL;

    return cmd_res;
}

static bool
open_with_cmd(FsearchDatabaseEntry *entry, const char *cmd) {
    char *cmd_res = build_folder_open_cmd(entry, cmd);
    if (!cmd_res) {
        return false;
    }

    bool result = true;
    GError *error = NULL;
    if (!g_spawn_command_line_async(cmd_res, &error)) {

        fprintf(stderr, "open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog_async(NULL,
                                      GTK_MESSAGE_ERROR,
                                      GTK_BUTTONS_OK,
                                      "Error while opening file:",
                                      error->message,
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
        g_error_free(error);
        result = false;
    }

    g_free(cmd_res);
    cmd_res = NULL;

    return result;
}

static bool
open_uri(const char *uri) {
    if (!g_file_test(uri, G_FILE_TEST_EXISTS)) {
        return false;
    }

    GError *error = NULL;
    const char *argv[3];
    argv[0] = "xdg-open";
    argv[1] = uri;
    argv[2] = NULL;

    if (!g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {

        fprintf(stderr, "xdg-open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog_async(NULL,
                                      GTK_MESSAGE_ERROR,
                                      GTK_BUTTONS_OK,
                                      "Error while opening file:",
                                      error->message,
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
        g_error_free(error);

        return false;
    }
    return true;
}

static bool
file_remove_or_trash(const char *path, bool delete) {
    GFile *file = g_file_new_for_path(path);
    if (!file) {
        return false;
    }
    bool success = false;
    if (delete) {
        success = g_file_delete(file, NULL, NULL);
    }
    else {
        success = g_file_trash(file, NULL, NULL);
    }
    g_object_unref(file);

    if (success) {
        if (delete) {
            g_debug("[file_remove] deleted file: %s", path);
        }
        else {
            g_debug("[file_remove] moved file to trash: %s", path);
        }
    }
    else {
        g_warning("[file_remove] failed removing: %s", path);
    }
    return success;
}

bool
fsearch_file_utils_remove(const char *path) {
    return file_remove_or_trash(path, true);
}

bool
fsearch_file_utils_trash(const char *path) {
    return file_remove_or_trash(path, false);
}

bool
fsearch_file_utils_launch_entry(FsearchDatabaseEntry *entry) {
    GString *path_full = db_entry_get_path_full(entry);
    if (!path_full) {
        return false;
    }
    bool res = open_uri(path_full->str);
    g_string_free(path_full, TRUE);
    path_full = NULL;
    return res;
}

bool
fsearch_file_utils_launch_entry_with_command(FsearchDatabaseEntry *entry, const char *cmd) {
    if (cmd) {
        return open_with_cmd(entry, cmd);
    }
    else {
        GString *path = db_entry_get_path(entry);
        if (!path) {
            return false;
        }
        bool res = open_uri(path->str);
        g_string_free(path, TRUE);
        path = NULL;
        return res;
    }
}

static gchar *
get_mimetype(const gchar *name) {
    if (!name) {
        return NULL;
    }
    gchar *content_type = g_content_type_guess(name, NULL, 0, NULL);
    if (!content_type) {
        return NULL;
    }
    gchar *mimetype = g_content_type_get_description(content_type);

    g_free(content_type);
    content_type = NULL;

    return mimetype;
}

gchar *
fsearch_file_utils_get_file_type_non_localized(const char *name, gboolean is_dir) {
    gchar *type = NULL;
    if (is_dir) {
        type = g_strdup("Folder");
    }
    else {
        type = get_mimetype(name);
    }
    if (type == NULL) {
        type = g_strdup("Unknown Type");
    }
    return type;
}

gchar *
fsearch_file_utils_get_file_type(const char *name, gboolean is_dir) {
    gchar *type = NULL;
    if (is_dir) {
        type = g_strdup(_("Folder"));
    }
    else {
        type = get_mimetype(name);
    }
    if (type == NULL) {
        type = g_strdup(_("Unknown Type"));
    }
    return type;
}

#define DEFAULT_FILE_ICON_NAME "application-octet-stream"

GIcon *
fsearch_file_utils_guess_icon(const char *name, bool is_dir) {
    if (is_dir) {
        return g_themed_icon_new("folder");
    }
    gchar *content_type = g_content_type_guess(name, NULL, 0, NULL);
    if (!content_type) {
        return g_themed_icon_new(DEFAULT_FILE_ICON_NAME);
    }

    GIcon *icon = g_content_type_get_icon(content_type);

    g_free(content_type);
    content_type = NULL;

    return icon ? icon : g_themed_icon_new(DEFAULT_FILE_ICON_NAME);
}

GIcon *
fsearch_file_utils_get_icon_for_path(const char *path) {
    GFile *g_file = g_file_new_for_path(path);
    if (!g_file) {
        return g_themed_icon_new("edit-delete");
    }

    GFileInfo *file_info = g_file_query_info(g_file, "standard::icon", 0, NULL, NULL);
    if (!file_info) {
        g_object_unref(g_file);
        g_file = NULL;
        return g_themed_icon_new("edit-delete");
    }

    GIcon *icon = g_file_info_get_icon(file_info);
    g_object_ref(icon);

    g_object_unref(file_info);
    file_info = NULL;

    g_object_unref(g_file);
    g_file = NULL;

    return icon;
}

char *
fsearch_file_utils_get_size_formatted(off_t size, bool show_base_2_units) {
    if (show_base_2_units) {
        return g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
    }
    else {
        return g_format_size_full(size, G_FORMAT_SIZE_DEFAULT);
    }
}
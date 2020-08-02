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

#include <stdio.h>
#include <stdbool.h>
#include <gio/gio.h>
#include "utils.h"
#include "debug.h"
#include "ui_utils.h"
#include "fsearch_limits.h"

gboolean
build_path (gchar *dest, size_t dest_len, const gchar *path, const gchar *name)
{
    if (!dest || !path || !name || dest_len <= 0) {
        return FALSE;
    }

    gint32 res = snprintf (dest, dest_len, "%s/%s", path, name);
    if (res < 0) {
        return FALSE;
    }
    else {
        return TRUE;
    }
}

gboolean
build_path_uri (gchar *dest, size_t dest_len, const gchar *path, const gchar *name)
{
    if (!dest || !path || !name || dest_len <= 0) {
        return FALSE;
    }

    gint32 res = snprintf (dest, dest_len, "file://%s/%s", path, name);
    if (res < 0) {
        return FALSE;
    }
    else {
        return TRUE;
    }
}

static gboolean
keyword_eval_cb (const GMatchInfo *info, GString *res, gpointer data)
{
    gchar *match = g_match_info_fetch (info, 0);
    if (!match) {
        return FALSE;
    }
    gchar *r = g_hash_table_lookup ((GHashTable *)data, match);
    if (r) {
        g_string_append (res, r);
    }
    g_free (match);

    return FALSE;
}

static char *
build_folder_open_cmd (BTreeNode *node, const char *cmd)
{
    if (!cmd || !node) {
        return NULL;
    }

    char path[PATH_MAX] = "";
    if (!btree_node_get_path (node, path, sizeof (path))) {
        return NULL;
    }
    char path_full[PATH_MAX] = "";
    if (!btree_node_get_path_full (node, path_full, sizeof (path_full))) {
        return NULL;
    }
    char *path_quoted = g_shell_quote (path);
    char *path_full_quoted = g_shell_quote (path_full);

    // The following code is mostly based on the example code found here:
    // https://developer.gnome.org/glib/stable/glib-Perl-compatible-regular-expressions.html#g-regex-replace-eval
    //
    // Create hash table which hold all valid keywords as keys
    // and their replacements as values
    // All valid keywords are:
    // - {path_raw}
    //     The raw path of a file or folder. E.g. the path of /foo/bar is /foo
    // - {path_full_raw}
    //     The raw full path of a file or folder. E.g. the full path of /foo/bar is /foo/bar
    // - {path_quoted} and {path_full_quoted}
    //     Those are the same as {path_raw} and {path_full_raw} but they get properly escaped and quoted
    //     for the usage in shells. E.g. /foo/'bar becomes '/foo/'\''bar'

    GHashTable *keywords = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (keywords, "{path_raw}", path);
    g_hash_table_insert (keywords, "{path_full_raw}", path_full);
    g_hash_table_insert (keywords, "{path}", path_quoted);
    g_hash_table_insert (keywords, "{path_full}", path_full_quoted);

    // Regular expression which matches multiple words (and underscores) surrouned with {}
    GRegex *reg = g_regex_new ( "{[\\w]+}", 0, 0, NULL );
    // Replace all the matched keywords
    char *cmd_res = g_regex_replace_eval (reg, cmd, -1, 0, 0, keyword_eval_cb, keywords, NULL );

    g_regex_unref (reg);
    g_hash_table_destroy (keywords);
    g_free (path_quoted);
    g_free (path_full_quoted);

    return cmd_res;
}

static bool
open_with_cmd (BTreeNode *node, const char *cmd)
{
    if (!cmd) {
        return false;
    }

    char *cmd_res = build_folder_open_cmd (node, cmd);
    if (!cmd_res) {
        return false;
    }

    bool result = true;
    GError *error = NULL;
    if (!g_spawn_command_line_async (cmd_res,
                                     &error)) {

        fprintf(stderr, "open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog (NULL,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_OK,
                                 "Error while opening file:",
                                 error->message);
        g_error_free (error);
        result = false;
    }

    g_free (cmd_res);
    cmd_res = NULL;

    return result;
}

static bool
open_uri (const char *uri)
{
    if (!uri) {
        return false;
    }

    if (!g_file_test (uri, G_FILE_TEST_EXISTS)) {
        return false;
    }

    GError *error = NULL;
    const char *argv[3];
    argv[0] = "xdg-open";
    argv[1] = uri;
    argv[2] = NULL;

    if (!g_spawn_async (NULL,
                        (gchar **) argv,
                        NULL,
                        G_SPAWN_SEARCH_PATH,
                        NULL,
                        NULL,
                        NULL,
                        &error)) {

        fprintf(stderr, "xdg-open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog (NULL,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_OK,
                                 "Error while opening file:",
                                 error->message);
        g_error_free (error);

        return false;
    }
    return true;
}

bool
node_remove (BTreeNode *node, bool delete)
{
    char path[PATH_MAX] = "";
    bool res = btree_node_get_path_full (node, path, sizeof (path));
    if (!res) {
        return false;
    }
    GFile *file = g_file_new_for_path (path);
    if (!file) {
        return false;
    }
    bool success = false;
    if (delete) {
        success = g_file_delete (file, NULL, NULL);
    }
    else {
        success = g_file_trash (file, NULL, NULL);
    }
    g_object_unref (file);

    if (success) {
        if (delete) {
            trace ("[file_remove] deleted file: %s\n", path);
        }
        else {
            trace ("[file_remove] moved file to trash: %s\n", path);
        }
    }
    else {
        trace ("[file_remove] failed removing: %s\n", path);
    }
    return success;
}

bool
node_delete (BTreeNode *node)
{
    return node_remove (node, true);
}

bool
node_move_to_trash (BTreeNode *node)
{
    return node_remove (node, false);
}

bool
launch_node (BTreeNode *node)
{
    char path[PATH_MAX] = "";
    bool res = btree_node_get_path_full (node, path, sizeof (path));
    if (res) {
        return open_uri (path);
    }
    return false;
}

bool
launch_node_path (BTreeNode *node, const char *cmd)
{
    if (cmd) {
        return open_with_cmd (node, cmd);
    }
    else {
        char path[PATH_MAX] = "";
        bool res = btree_node_get_path (node, path, sizeof (path));
        if (res) {
            return open_uri (path);
        }
    }
    return false;
}


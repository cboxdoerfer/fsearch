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
#include <linux/limits.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include "utils.h"

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

static void
open_uri (const char *uri)
{
    gchar *uri_escaped = g_filename_to_uri (uri, NULL, NULL);
    if (uri_escaped) {
        GError *error = NULL;
        if (!g_app_info_launch_default_for_uri (uri_escaped, NULL, &error)) {
            fprintf(stderr, "open_uri: error: %s\n", error->message);
            GtkWidget *dialog = gtk_message_dialog_new (NULL,
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        GTK_MESSAGE_ERROR,
                                                        GTK_BUTTONS_OK_CANCEL,
                                                        "Error while opening file:");
            gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                      "%s", error->message);
            gtk_dialog_run (GTK_DIALOG (dialog));
            gtk_widget_destroy (dialog);
            g_error_free (error);
        }
        g_free (uri_escaped);
    }
}

void
launch_node (BTreeNode *node)
{
    char path[PATH_MAX] = "";
    bool res = btree_node_get_path_full (node, path, sizeof (path));
    if (res) {
        open_uri (path);
    }
}

void
launch_node_path (BTreeNode *node)
{
    char path[PATH_MAX] = "";
    bool res = btree_node_get_path (node, path, sizeof (path));
    if (res) {
        open_uri (path);
    }
}


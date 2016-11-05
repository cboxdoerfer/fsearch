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
#include <gio/gio.h>
#include "utils.h"
#include "ui_utils.h"

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


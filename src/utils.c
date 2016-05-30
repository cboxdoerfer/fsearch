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
#include "database_node.h"

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
open_uri (const char *uri)
{
    GError *error = NULL;
    if (g_app_info_launch_default_for_uri (uri, NULL, &error)) {
        return TRUE;
    }
    fprintf(stderr, "open_uri: error: %s\n", error->message);
    g_error_free (error);
    return FALSE;
}

void
launch_node (GNode *node)
{
    char path[PATH_MAX] = "";
    bool res = db_node_get_path_full (node, path, sizeof (path));
    if (res) {
        char uri[PATH_MAX] = "";
        snprintf (uri, sizeof (uri), "file://%s", path);
        open_uri (uri);
    }
}

void
launch_node_path (GNode *node)
{
    char path[PATH_MAX] = "";
    bool res = db_node_get_path (node, path, sizeof (path));
    if (res) {
        char uri[PATH_MAX] = "";
        snprintf (uri, sizeof (uri), "file://%s", path);
        open_uri (uri);
    }
}


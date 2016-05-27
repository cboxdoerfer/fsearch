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

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <linux/limits.h>
#include "database_node.h"

GNode *
db_node_new (const char *name, off_t size, time_t mtime, bool is_dir, uint32_t pos)
{
    DatabaseNodeData *data = g_new0 (DatabaseNodeData, 1);
    g_assert (data != NULL);

    data->name = g_strdup (name);
    data->mtime = mtime;
    data->size = size;
    data->is_dir = is_dir;
    data->pos = pos;

    return g_node_new (data);
}

void
db_node_free_data (GNode *node)
{
    if (!node) {
        return;
    }

    DatabaseNodeData *data = node->data;
    if (!data) {
        return;
    }

    if (data->name) {
        g_free (data->name);
        data->name = NULL;
    }
    g_free (data);
    data = NULL;
}

void
db_node_free (GNode *node)
{
    if (node) {
        db_node_free_data (node);
        g_node_destroy (node);
        node = NULL;
    }
}

static gboolean
db_node_free_cb (GNode *node, gpointer data)
{
    if (!node) {
        return FALSE;
    }
    db_node_free_data (node);

    return FALSE;
}

void
db_node_free_tree (GNode *root)
{
    g_assert (root != NULL);
    // free DatabaseNodeData
    g_node_traverse (root,
                     G_IN_ORDER,
                     G_TRAVERSE_ALL,
                     -1,
                     (GNodeTraverseFunc)db_node_free_cb,
                     NULL);
    // free tree
    g_node_destroy (root);
    root = NULL;
}

void
db_node_append (GNode *parent, GNode *child)
{
    g_node_append (parent, child);
}

void
db_node_set_pos (GNode *node, uint32_t pos)
{
    DatabaseNodeData *data = node->data;
    data->pos = pos;
}

const char *
db_node_get_name (GNode *node)
{
    DatabaseNodeData *data = node->data;
    return data->name;
}

off_t
db_node_get_size (GNode *node)
{
    DatabaseNodeData *data = node->data;
    return data->size;
}

uint32_t
db_node_get_num_children (GNode *node)
{
    return g_node_n_children (node);
}

uint32_t
db_node_get_pos (GNode *node)
{
    DatabaseNodeData *data = node->data;
    return data->pos;
}

time_t
db_node_get_mtime (GNode *node)
{
    DatabaseNodeData *data = node->data;
    return data->mtime;
}

bool
db_node_is_dir (GNode *node)
{
    DatabaseNodeData *data = node->data;
    return data->is_dir;
}

const char *
db_node_get_root_path (GNode *node)
{
    if (!node) {
        // empty node
        return NULL;
    }
    GNode *root = g_node_get_root (node);
    if (root) {
        DatabaseNodeData *data = root->data;
        if (data) {
            return data->name;
        }
    }
    return NULL;
}

static inline int
string_copy (char *dest, const char *src, int dest_len)
{
    int i = 0;
    for (i = 0; i < dest_len && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    if (i < dest_len) {
        dest[i] = '\0';
    }
    else {
        dest[dest_len - 1] = '\0';
    }
    return i;
}

static bool
get_path (GNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    if (G_NODE_IS_ROOT (node)) {
        DatabaseNodeData *data = node->data;
        g_strlcpy (path, data->name, path_len);
        return true;
    }
    const gint depth = g_node_depth (node);
    GNode *temp = node;
    if (depth > 1) {
        char *parents[depth + 1];
        parents[depth] = NULL;

        for (int32_t i = depth - 1; i >= 0 && temp; i--) {
            DatabaseNodeData *data = temp->data;
            parents[i] = data->name;
            temp = temp->parent;
        }

        char *ptr = path;
        int bytes_left = path_len;
        int bytes_written = string_copy (ptr, parents[0], bytes_left);
        bytes_left -= bytes_written;
        ptr += bytes_written;

        uint32_t counter = 1;
        char *item = parents[counter];
        while (item && bytes_left > 0) {
            *ptr++ = '/';
            bytes_left--;
            bytes_written = string_copy (ptr, item, bytes_left);
            bytes_left -= bytes_written;
            ptr += bytes_written;
            counter++;
            item = parents[counter];
        }
        return true;
    }
    return false;
}

bool
db_node_get_path (GNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    return get_path (node->parent, path, path_len);
}

bool
db_node_get_path_full (GNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    return get_path (node, path, path_len);
}

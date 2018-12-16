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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "btree.h"
#include "string_utils.h"

BTreeNode *
btree_node_new (const char *name,
                time_t mtime,
                off_t size,
                uint32_t pos,
                bool is_dir,
                const char *tags)
{
    BTreeNode *new = calloc (1, sizeof (BTreeNode));
    assert (new);

    new->parent = NULL;
    new->children = NULL;
    new->next = NULL;

    // data
    new->name = strdup (name);
    new->mtime = mtime;
    new->size = size;
    new->pos = pos;
    new->is_dir = is_dir;
    new->tags = tags == NULL ? NULL : strdup(tags);

    return new;
}

static void
btree_node_data_free (BTreeNode *node)
{
    if (!node) {
        return;
    }
    if (node->name) {
        free (node->name);
        node->name = NULL;
    }
    if (node->tags) {
        free (node->tags);
        node->tags = NULL;
    }
    free (node);
    node = NULL;
}

static void
btree_nodes_free (BTreeNode *node)
{
    while (node) {
        if (node->children) {
            btree_nodes_free (node->children);
        }
        BTreeNode *next = node->next;
        btree_node_data_free (node);
        node = next;
    }
}

void
btree_node_unlink (BTreeNode *node)
{
    assert (node);
    if (!node->parent) {
        return;
    }
    BTreeNode *parent = node->parent;
    if (parent->children == node) {
        parent->children = node->next;
    }
    else {
        BTreeNode *sibling = parent->children;
        while (sibling->next != node) {
            sibling = sibling->next;
        }
        sibling->next = node->next;
    }

    node->parent = NULL;
    node->next = NULL;
}

void
btree_node_free (BTreeNode *node)
{
    if (!node) {
        return;
    }
    if (node->parent) {
        btree_node_unlink (node);
    }
    if (node->children) {
        btree_nodes_free (node->children);
    }
    btree_node_data_free (node);
}

BTreeNode *
btree_node_append (BTreeNode *parent, BTreeNode *node)
{
    assert (parent);
    assert (node);
    node->parent = parent;
    node->next = NULL;

    if (!parent->children) {
        parent->children = node;
        return node;
    }
    BTreeNode *child = parent->children;
    while (child->next) {
        child = child->next;
    }
    child->next = node;
    return node;
}

BTreeNode *
btree_node_prepend (BTreeNode *parent, BTreeNode *node)
{
    assert (parent);
    assert (node);
    node->parent = parent;
    node->next = parent->children;
    parent->children = node;
    return node;
}

void
btree_node_remove (BTreeNode *node)
{
    btree_node_free (node);
}

BTreeNode *
btree_node_get_root (BTreeNode *node)
{
    assert (node);
    BTreeNode *root = node;
    while (root->parent) {
        root = root->parent;
    }
    return root;
}

bool
btree_node_is_root (BTreeNode *node)
{
    return node->parent ? false : true;
}

uint32_t
btree_node_depth (BTreeNode *node)
{
    uint32_t depth = 0;
    BTreeNode *temp = node;
    while (temp) {
        depth++;
        temp = temp->parent;
    }
    return depth;
}

uint32_t
btree_node_n_children (BTreeNode *node)
{
    assert (node);
    if (!node->children) {
        return 0;
    }
    uint32_t num_children = 0;
    BTreeNode *child = node->children;
    while (child) {
        child = child->next;
        num_children++;
    }
    return num_children;
}

bool
btree_node_has_children (BTreeNode *node)
{
    assert (node);
    return node->children ? true : false;
}

void
btree_node_children_foreach (BTreeNode *node,
                             void (*func)(BTreeNode *, void *),
                             void *data)
{
    if (!node) {
        return;
    }
    BTreeNode *child = node->children;
    while (child) {
        func (child, data);
        child = child->next;
    }
}

void
btree_node_count_nodes (BTreeNode *node, uint32_t *num_nodes)
{
    (*num_nodes)++;
    if (node->children) {
        BTreeNode *child = node->children;
        while (child) {
            btree_node_count_nodes (child, num_nodes);
            child = child->next;
        }
    }
}

uint32_t
btree_node_n_nodes (BTreeNode *node)
{
    if (!node) {
        return 0;
    }
    uint32_t num_nodes = 0;
    btree_node_count_nodes (node, &num_nodes);
    return num_nodes;
}

void
btree_node_traverse_cb (BTreeNode *node,
                        bool (*func)(BTreeNode *, void *),
                        void *data)
{
    func (node, data);
    if (node->children) {
        BTreeNode *child = node->children;
        while (child) {
            btree_node_traverse_cb (child, func, data);
            child = child->next;
        }
    }
}


void
btree_node_traverse (BTreeNode *node,
                     bool (*func)(BTreeNode *, void *),
                     void *data)
{
    if (!node) {
        return;
    }
    btree_node_traverse_cb (node, func, data);
}

static bool
btree_node_build_path (BTreeNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    if (btree_node_is_root (node)) {
        if (strlen (node->name) == 0) {
            strncpy (path, "/", path_len);
        }
        else {
            strncpy (path, node->name, path_len);
        }
        return true;
    }

    const int32_t depth = btree_node_depth (node);
    char *parents[depth + 1];
    parents[depth] = NULL;

    BTreeNode *temp = node;
    for (int32_t i = depth - 1; i >= 0 && temp; i--) {
        parents[i] = temp->name;
        temp = temp->parent;
    }

    char *ptr = path;
    char *end = &path[path_len - 1];

    uint32_t counter = 0;
    ptr = fsearch_string_copy (ptr, end, parents[counter++]);

    char *item = parents[counter++];
    while (item && ptr != end) {
        ptr = fsearch_string_copy (ptr, end, "/");
        ptr = fsearch_string_copy (ptr, end, item);
        item = parents[counter++];
    }
    return true;
}

bool
btree_node_get_path (BTreeNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    return btree_node_build_path (node->parent, path, path_len);
}

bool
btree_node_get_path_full (BTreeNode *node, char *path, size_t path_len)
{
    if (!node) {
        // empty node
        return false;
    }
    return btree_node_build_path (node, path, path_len);
}

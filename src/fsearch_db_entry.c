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

#define _GNU_SOURCE
#include "fsearch_db_entry.h"
#include "fsearch_string_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DatabaseEntry *
db_entry_new(const char *name, time_t mtime, off_t size, uint32_t pos, bool is_dir) {
    DatabaseEntry *new = calloc(1, sizeof(DatabaseEntry));
    assert(new);

    new->parent = NULL;
    new->children = NULL;
    new->next = NULL;

    // data
    new->name = strdup(name);
    new->mtime = mtime;
    new->size = size;
    new->pos = pos;
    new->is_dir = is_dir;

    return new;
}

static void
btree_node_data_free(DatabaseEntry *node) {
    if (!node) {
        return;
    }
    if (node->name) {
        free(node->name);
        node->name = NULL;
    }
    free(node);
    node = NULL;
}

static void
btree_nodes_free(DatabaseEntry *node) {
    while (node) {
        if (node->children) {
            btree_nodes_free(node->children);
        }
        DatabaseEntry *next = node->next;
        btree_node_data_free(node);
        node = next;
    }
}

void
btree_node_unlink(DatabaseEntry *node) {
    assert(node);
    if (!node->parent) {
        return;
    }
    DatabaseEntry *parent = node->parent;
    if (parent->children == node) {
        parent->children = node->next;
    }
    else {
        DatabaseEntry *sibling = parent->children;
        while (sibling->next != node) {
            sibling = sibling->next;
        }
        sibling->next = node->next;
    }

    node->parent = NULL;
    node->next = NULL;
}

void
btree_node_clear(DatabaseEntry *node) {
    if (node && node->name) {
        free(node->name);
        node->name = NULL;
    }
}

void
db_entry_free(DatabaseEntry *node) {
    if (!node) {
        return;
    }
    if (node->parent) {
        btree_node_unlink(node);
    }
    if (node->children) {
        btree_nodes_free(node->children);
    }
    btree_node_data_free(node);
}

DatabaseEntry *
btree_node_append(DatabaseEntry *parent, DatabaseEntry *node) {
    assert(parent);
    assert(node);
    node->parent = parent;
    node->next = NULL;

    if (!parent->children) {
        parent->children = node;
        return node;
    }
    DatabaseEntry *child = parent->children;
    while (child->next) {
        child = child->next;
    }
    child->next = node;
    return node;
}

DatabaseEntry *
btree_node_prepend(DatabaseEntry *parent, DatabaseEntry *node) {
    assert(parent);
    assert(node);
    node->parent = parent;
    node->next = parent->children;
    parent->children = node;
    return node;
}

DatabaseEntry *
btree_node_get_root(DatabaseEntry *node) {
    assert(node);
    DatabaseEntry *root = node;
    while (root->parent) {
        root = root->parent;
    }
    return root;
}

bool
btree_node_is_root(DatabaseEntry *node) {
    return node->parent ? false : true;
}

uint32_t
btree_node_depth(DatabaseEntry *node) {
    uint32_t depth = 0;
    DatabaseEntry *temp = node;
    while (temp) {
        depth++;
        temp = temp->parent;
    }
    return depth;
}

uint32_t
btree_node_n_children(DatabaseEntry *node) {
    assert(node);
    if (!node->children) {
        return 0;
    }
    uint32_t num_children = 0;
    DatabaseEntry *child = node->children;
    while (child) {
        child = child->next;
        num_children++;
    }
    return num_children;
}

bool
btree_node_has_children(DatabaseEntry *node) {
    assert(node);
    return node->children ? true : false;
}

void
btree_node_children_foreach(DatabaseEntry *node, void (*func)(DatabaseEntry *, void *), void *data) {
    if (!node) {
        return;
    }
    DatabaseEntry *child = node->children;
    while (child) {
        func(child, data);
        child = child->next;
    }
}

void
btree_node_count_nodes(DatabaseEntry *node, uint32_t *num_nodes) {
    (*num_nodes)++;
    if (node->children) {
        DatabaseEntry *child = node->children;
        while (child) {
            btree_node_count_nodes(child, num_nodes);
            child = child->next;
        }
    }
}

uint32_t
btree_node_n_nodes(DatabaseEntry *node) {
    if (!node) {
        return 0;
    }
    uint32_t num_nodes = 0;
    btree_node_count_nodes(node, &num_nodes);
    return num_nodes;
}

void
btree_node_traverse_cb(DatabaseEntry *node, bool (*func)(DatabaseEntry *, void *), void *data) {
    func(node, data);
    if (node->children) {
        DatabaseEntry *child = node->children;
        while (child) {
            btree_node_traverse_cb(child, func, data);
            child = child->next;
        }
    }
}

void
btree_node_traverse(DatabaseEntry *node, bool (*func)(DatabaseEntry *, void *), void *data) {
    if (!node) {
        return;
    }
    btree_node_traverse_cb(node, func, data);
}

// static bool
// btree_node_build_path(DatabaseEntry *node, char *path, size_t path_len) {
//     if (!node) {
//         // empty node
//         return false;
//     }
//     if (btree_node_is_root(node)) {
//         if (strlen(node->name) == 0) {
//             strncpy(path, "/", path_len);
//         }
//         else {
//             strncpy(path, node->name, path_len);
//         }
//         return true;
//     }
//
//     const int32_t depth = btree_node_depth(node);
//     char *parents[depth + 1];
//     parents[depth] = NULL;
//
//     DatabaseEntry *temp = node;
//     for (int32_t i = depth - 1; i >= 0 && temp; i--) {
//         parents[i] = temp->name;
//         temp = temp->parent;
//     }
//
//     char *ptr = path;
//     char *end = &path[path_len - 1];
//
//     uint32_t counter = 0;
//     ptr = fs_str_copy(ptr, end, parents[counter++]);
//
//     char *item = parents[counter++];
//     while (item && ptr != end) {
//         ptr = fs_str_copy(ptr, end, "/");
//         ptr = fs_str_copy(ptr, end, item);
//         item = parents[counter++];
//     }
//     return true;
// }
//
// bool
// db_entry_init_path(DatabaseEntry *node, char *path, size_t path_len) {
//     if (!node) {
//         // empty node
//         return false;
//     }
//     return btree_node_build_path(node->parent, path, path_len);
// }
//
// static void
// build_path_recursively(DatabaseEntry *node, GString *str) {
//     if (node->parent) {
//         build_path_recursively(node->parent, str);
//     }
//     g_string_append_c(str, '/');
//     g_string_append(str, node->name);
// }
//
// void
// db_entry_append_path(DatabaseEntry *node, GString *str) {
//     build_path_recursively(node, str);
// }
//
// char *
// db_entry_get_path(DatabaseEntry *node) {
//     GString *path = g_string_new(NULL);
//     build_path_recursively(node, path);
//     return g_string_free(path, FALSE);
// }
//
// bool
// db_entry_init_parent_path(DatabaseEntry *node, char *path, size_t path_len) {
//     if (!node) {
//         // empty node
//         return false;
//     }
//     return btree_node_build_path(node, path, path_len);
// }

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

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct _DatabaseEntry DatabaseEntry;

struct _DatabaseEntry {
    DatabaseEntry *next;
    DatabaseEntry *parent;
    DatabaseEntry *children;

    // data
    char *name;

    time_t mtime;
    off_t size;
    uint32_t pos;
    bool is_dir;
};

DatabaseEntry *
db_entry_new(const char *name, time_t mtime, off_t size, uint32_t pos, bool is_dir);

void
db_entry_free(DatabaseEntry *node);

void
btree_node_clear(DatabaseEntry *node);

void
btree_node_unlink(DatabaseEntry *node);

DatabaseEntry *
btree_node_append(DatabaseEntry *parent, DatabaseEntry *node);

DatabaseEntry *
btree_node_prepend(DatabaseEntry *parent, DatabaseEntry *node);

DatabaseEntry *
btree_node_get_root(DatabaseEntry *node);

bool
btree_node_is_root(DatabaseEntry *node);

uint32_t
btree_node_n_nodes(DatabaseEntry *node);

uint32_t
btree_node_depth(DatabaseEntry *node);

uint32_t
btree_node_n_children(DatabaseEntry *node);

bool
btree_node_has_children(DatabaseEntry *node);

void
btree_node_children_foreach(DatabaseEntry *node, void (*func)(DatabaseEntry *, void *), void *data);

void
btree_node_traverse(DatabaseEntry *node, bool (*func)(DatabaseEntry *, void *), void *data);

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

#pragma once

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

typedef struct _DatabaseNodeData DatabaseNodeData;

struct _DatabaseNodeData
{
    gchar *name;

    time_t mtime;
    off_t size;
    uint32_t pos;
    bool is_dir;
};

GNode *
db_node_new (const char *name, off_t size, time_t mtime, bool is_dir, uint32_t pos);

void
db_node_free (GNode *node);

void
db_node_free_data (GNode *node);

void
db_node_free_tree (GNode *root);

void
db_node_append (GNode *parent, GNode *child);

void
db_node_set_pos (GNode *node, uint32_t pos);

const char *
db_node_get_name (GNode *node);

off_t
db_node_get_size (GNode *node);

time_t
db_node_get_mtime (GNode *node);

uint32_t
db_node_get_pos (GNode *node);

uint32_t
db_node_get_num_children (GNode *node);

bool
db_node_is_dir (GNode *node);

bool
db_node_get_path (GNode *node, char *path, size_t path_len);

bool
db_node_get_path_full (GNode *node, char *path, size_t path_len);

const char *
db_node_get_root_path (GNode *node);

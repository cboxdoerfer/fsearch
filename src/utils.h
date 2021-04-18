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

#include "btree.h"
#include "fsearch_list_view.h"
#include <gtk/gtk.h>
#include <stdbool.h>

void
init_data_dir_path(char *path, size_t len);

bool
create_dir(const char *path);

bool
file_trash(const char *path);

bool
file_remove(const char *path);

bool
launch_node(BTreeNode *node);

bool
launch_node_path(BTreeNode *node, const char *cmd);

gchar *
get_mimetype(const gchar *path);

gchar *
get_file_type(BTreeNode *node, const gchar *path);

GIcon *
get_gicon_for_path(const char *path);

cairo_surface_t *
get_icon_surface(GdkWindow *win, const char *path, int icon_size, int scale_factor);

int
get_icon_size_for_height(int height);

char *
get_size_formatted(BTreeNode *node, bool show_base_2_units);

int
compare_name(BTreeNode **a, BTreeNode **b);

int
compare_pos(BTreeNode **a_node, BTreeNode **b_node);

int
compare_size(BTreeNode **a, BTreeNode **b);

int
compare_path(BTreeNode **a, BTreeNode **b);

int
compare_changed(BTreeNode **a, BTreeNode **b);

int
compare_type(BTreeNode **a, BTreeNode **b);


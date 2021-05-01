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

#include "fsearch_database.h"
#include "fsearch_db_entry.h"
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
launch_entry(FsearchDatabaseEntry *entry);

bool
launch_entry_for_path(FsearchDatabaseEntry *entry, const char *cmd);

gchar *
get_file_type(const gchar *name, gboolean is_dir);

gchar *
get_file_type_non_localized(const char *name, gboolean is_dir);

GIcon *
get_gicon_for_path(const char *path);

cairo_surface_t *
get_icon_surface(GdkWindow *win, const char *path, int icon_size, int scale_factor);

int
get_icon_size_for_height(int height);

char *
get_size_formatted(off_t size, bool show_base_2_units);

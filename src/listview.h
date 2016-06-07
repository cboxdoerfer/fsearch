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

#include <gtk/gtk.h>
#include <stdint.h>
#include "config.h"

GtkTreeView *
listview_new (void);

void
listview_add_column (GtkTreeView *view, uint32_t col_type, int32_t size, int32_t pos);

void
listview_add_default_columns (GtkTreeView *view);

void
listview_remove_column_at_pos (GtkTreeView *view, int32_t pos);

void
listview_remove_column (GtkTreeView *view, uint32_t col_type);

uint32_t
listview_column_get_pos (GtkTreeView *view, uint32_t col_type);

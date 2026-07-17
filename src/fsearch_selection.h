/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include "fsearch_array.h"

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

void
fsearch_selection_free(GHashTable *selection);

GHashTable *
fsearch_selection_new(void);

void
fsearch_selection_select_toggle(GHashTable *selection, gpointer item);

void
fsearch_selection_select(GHashTable *selection, gpointer item);

void
fsearch_selection_unselect(GHashTable *selection, gpointer item);

bool
fsearch_selection_is_selected(GHashTable *selection, gpointer item);

void
fsearch_selection_select_all(GHashTable *selection, DynamicArray *items);

void
fsearch_selection_unselect_all(GHashTable *selection);

void
fsearch_selection_invert(GHashTable *selection, DynamicArray *items);

uint32_t
fsearch_selection_get_num_selected(GHashTable *selection);

void
fsearch_selection_for_each(GHashTable *selection, GHFunc func, gpointer user_data);
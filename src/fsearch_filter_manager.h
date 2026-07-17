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
*/

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <stdbool.h>

#include "fsearch_filter.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_FILTER_MANAGER (fsearch_filter_manager_get_type())

typedef struct FsearchFilterManager FsearchFilterManager;

GType
fsearch_filter_manager_get_type(void);

FsearchFilterManager *
fsearch_filter_manager_ref(FsearchFilterManager *manager);

void
fsearch_filter_manager_unref(FsearchFilterManager *manager);

FsearchFilterManager *
fsearch_filter_manager_new(void);

FsearchFilterManager *
fsearch_filter_manager_new_with_defaults(void);

FsearchFilter *
fsearch_filter_manager_get_filter_for_name(FsearchFilterManager *manager, const char *name);

FsearchFilterManager *
fsearch_filter_manager_copy(FsearchFilterManager *manager);

guint
fsearch_filter_manager_get_num_filters(FsearchFilterManager *manager);

FsearchFilter *
fsearch_filter_manager_get_filter(FsearchFilterManager *manager, guint idx);

void
fsearch_filter_manager_append_filter(FsearchFilterManager *manager, FsearchFilter *filter);

void
fsearch_filter_manager_reorder(FsearchFilterManager *manager, gint *new_order, size_t new_order_len);

void
fsearch_filter_manager_remove(FsearchFilterManager *manager, FsearchFilter *filter);

void
fsearch_filter_manager_edit(FsearchFilterManager *manager,
                            FsearchFilter *filter,
                            const char *name,
                            const char *macro,
                            const char *query,
                            FsearchQueryFlags flags);

bool
fsearch_filter_manager_cmp(FsearchFilterManager *manager_1, FsearchFilterManager *manager_2);

G_END_DECLS
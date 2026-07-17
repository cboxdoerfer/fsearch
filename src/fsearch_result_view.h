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

#include "fsearch_database.h"
#include "fsearch_list_view.h"

typedef struct {
    FsearchDatabase *db;
    FsearchListView *list_view;

    GHashTable *item_info_cache;
    GHashTable *pixbuf_cache;
    GHashTable *app_gicon_cache;

    GHashTable *icon_cache;
    GHashTable *icon_loads;

    // remember the row height from the last draw call
    // when it changes we need to reset the icon cache
    int32_t row_height;

    guint view_id;
    FsearchDatabaseIndexProperty sort_order;
    GtkSortType sort_type;
} FsearchResultView;

FsearchResultView *
fsearch_result_view_new(guint view_id);

void
fsearch_result_view_free(FsearchResultView *result_view);

void
fsearch_result_view_row_cache_reset(FsearchResultView *result_view);

char *
fsearch_result_view_query_tooltip(FsearchResultView *view,
                                  uint32_t row,
                                  FsearchListViewColumn *col,
                                  PangoLayout *layout,
                                  uint32_t row_height);

void
fsearch_result_view_draw_row(FsearchResultView *view,
                             cairo_t *cr,
                             GdkWindow *bin_window,
                             PangoLayout *layout,
                             GtkStyleContext *context,
                             GList *columns,
                             cairo_rectangle_int_t *rect,
                             uint32_t row,
                             gboolean row_selected,
                             gboolean row_focused,
                             gboolean row_hovered,
                             gboolean right_to_left_text);
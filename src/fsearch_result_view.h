#pragma once

#include "fsearch_database_view.h"
#include "fsearch_list_view.h"

char *
fsearch_result_view_query_tooltip(FsearchDatabaseView *view,
                                  uint32_t row,
                                  FsearchListViewColumn *col,
                                  PangoLayout *layout,
                                  uint32_t row_height);

void
fsearch_result_view_draw_row(FsearchDatabaseView *view,
                             cairo_t *cr,
                             GdkWindow *bin_window,
                             PangoLayout *layout,
                             GtkStyleContext *context,
                             GList *columns,
                             cairo_rectangle_int_t *rect,
                             uint32_t row,
                             gboolean row_selected,
                             gboolean row_focused,
                             gboolean right_to_left_text);

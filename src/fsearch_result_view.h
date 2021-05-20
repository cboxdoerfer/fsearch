#pragma once

#include "fsearch_database_entry.h"
#include "fsearch_list_view.h"
#include "fsearch_query.h"

char *
fsearch_result_view_query_tooltip(FsearchDatabaseEntry *entry,
                                  FsearchListViewColumn *col,
                                  PangoLayout *layout,
                                  uint32_t row_height);

void
fsearch_result_view_draw_row(cairo_t *cr,
                             GdkWindow *bin_window,
                             PangoLayout *layout,
                             GtkStyleContext *context,
                             GList *columns,
                             cairo_rectangle_int_t *rect,
                             FsearchDatabaseEntry *entry,
                             FsearchQuery *query,
                             gboolean row_selected,
                             gboolean row_focused,
                             gboolean right_to_left_text);

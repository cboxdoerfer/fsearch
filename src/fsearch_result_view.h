#pragma once

#include "fsearch_database_view.h"
#include "fsearch_list_view.h"

typedef struct {
    FsearchDatabaseView *database_view;
    FsearchListView *list_view;

    GHashTable *row_cache;
    GHashTable *pixbuf_cache;
    GHashTable *app_gicon_cache;

    // remember the row height from the last draw call
    // when it changes we need to reset the icon cache
    int32_t row_height;

    FsearchDatabaseIndexType sort_order;
    GtkSortType sort_type;
} FsearchResultView;

FsearchResultView *
fsearch_result_view_new(void);

void
fsearch_result_view_free(FsearchResultView *result_view);

void
fsearch_result_view_row_cache_reset(FsearchResultView *result_view);

char *
fsearch_result_view_query_tooltip(FsearchDatabaseView *view,
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

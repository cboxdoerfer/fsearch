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

    // remember the row height and scale factor from the last draw call
    // when it changes we need to reset the icon cache
    int32_t row_height;
    int32_t scale_factor;

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
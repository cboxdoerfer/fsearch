#pragma once

#include <gtk/gtk.h>
#include <stdint.h>

#define ROW_PADDING_Y 4
#define ROW_PADDING_X 3

G_BEGIN_DECLS

#define FSEARCH_TYPE_LIST_VIEW (fsearch_list_view_get_type())

G_DECLARE_FINAL_TYPE(FsearchListView, fsearch_list_view, FSEARCH, LIST_VIEW, GtkContainer)

G_END_DECLS

typedef struct {
    GtkWidget *button;
    GtkWidget *arrow;
    GtkWidget *emblem;
    FsearchListView *view;
    int type;
    char *name;
    gint width;
    gint effective_width;
    gboolean expand;
    gboolean visible;
    PangoAlignment alignment;
    PangoEllipsizeMode ellipsize_mode;

    GdkWindow *window;

    volatile gint ref_count;
} FsearchListViewColumn;

typedef char *(*FsearchListViewQueryTooltipFunc)(PangoLayout *layout,
                                                 uint32_t row_height,
                                                 uint32_t row,
                                                 FsearchListViewColumn *col,
                                                 gpointer user_data);

typedef void (*FsearchListViewDrawRowFunc)(cairo_t *cr,
                                           GdkWindow *bin_window,
                                           PangoLayout *layout,
                                           GtkStyleContext *context,
                                           GList *columns,
                                           cairo_rectangle_int_t *rect,
                                           uint32_t row,
                                           gboolean row_selected,
                                           gboolean row_focused,
                                           gboolean row_hovered,
                                           gboolean right_to_left_text,
                                           gpointer user_data);

typedef void (*FsearchListViewSortFunc)(int type, gpointer user_data);

// selection handlers
typedef gboolean (*FsearchListViewIsSelectedFunc)(int row_idx, gpointer user_data);
typedef void (*FsearchListViewSelectFunc)(int row_idx, gpointer user_data);
typedef void (*FsearchListViewSelectToggleFunc)(int row_idx, gpointer user_data);
typedef void (*FsearchListViewSelectRangeFunc)(int start_idx, int end_idx, gpointer user_data);
typedef void (*FsearchListViewUnselectAllFunc)(gpointer user_data);
typedef guint (*FsearchListViewNumSelectedFunc)(gpointer user_data);

FsearchListViewColumn *
fsearch_list_view_column_ref(FsearchListViewColumn *col);

void
fsearch_list_view_column_unref(FsearchListViewColumn *col);

FsearchListViewColumn *
fsearch_list_view_column_new(int type,
                             char *name,
                             PangoAlignment alignment,
                             PangoEllipsizeMode ellipsize_mode,
                             gboolean visibile,
                             gboolean expand,
                             uint32_t width);

FsearchListView *
fsearch_list_view_new();

void
fsearch_list_view_set_selection_handlers(FsearchListView *view,
                                         FsearchListViewIsSelectedFunc is_selected_func,
                                         FsearchListViewSelectFunc select_func,
                                         FsearchListViewSelectToggleFunc select_toggle_func,
                                         FsearchListViewSelectRangeFunc select_range_func,
                                         FsearchListViewUnselectAllFunc unselect_func,
                                         FsearchListViewNumSelectedFunc num_selected_func,
                                         gpointer user_data);

void
fsearch_list_view_column_set_visible(FsearchListView *view, FsearchListViewColumn *col, gboolean visible);

void
fsearch_list_view_column_set_tooltip(FsearchListViewColumn *col, const char *tooltip);

void
fsearch_list_view_column_set_emblem(FsearchListViewColumn *col, const char *emblem_name, gboolean visible);

void
fsearch_list_view_remove_column(FsearchListView *view, FsearchListViewColumn *col);

void
fsearch_list_view_append_column(FsearchListView *view, FsearchListViewColumn *col);

FsearchListViewColumn *
fsearch_list_view_get_first_column_for_type(FsearchListView *view, int type);

void
fsearch_list_view_set_config(FsearchListView *view, uint32_t num_rows, int sort_order, GtkSortType sort_type);

gint
fsearch_list_view_get_cursor(FsearchListView *view);

void
fsearch_list_view_set_cursor(FsearchListView *view, int row_idx);

void
fsearch_list_view_set_single_click_activate(FsearchListView *view, gboolean value);

void
fsearch_list_view_set_sort_order(FsearchListView *view, int sort_order);

int
fsearch_list_view_get_sort_order(FsearchListView *view);

void
fsearch_list_view_set_sort_type(FsearchListView *view, GtkSortType sort_type);

GtkSortType
fsearch_list_view_get_sort_type(FsearchListView *view);

void
fsearch_list_view_set_sort_func(FsearchListView *view, FsearchListViewSortFunc func, gpointer sort_func_data);

void
fsearch_list_view_set_query_tooltip_func(FsearchListView *view,
                                         FsearchListViewQueryTooltipFunc func,
                                         gpointer func_data);

void
fsearch_list_view_set_draw_row_func(FsearchListView *view, FsearchListViewDrawRowFunc func, gpointer func_data);

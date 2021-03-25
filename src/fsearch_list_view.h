#pragma once

#include <gtk/gtk.h>

#define ROW_PADDING_Y 4
#define ROW_PADDING_X 3

typedef enum {
    FSEARCH_LIST_VIEW_COLUMN_NAME,
    FSEARCH_LIST_VIEW_COLUMN_PATH,
    FSEARCH_LIST_VIEW_COLUMN_SIZE,
    FSEARCH_LIST_VIEW_COLUMN_CHANGED,
    FSEARCH_LIST_VIEW_COLUMN_TYPE,
    NUM_FSEARCH_LIST_VIEW_COLUMNS,
} FsearchListViewColumnType;

typedef void (*FsearchListViewDrawRowFunc)(cairo_t *cr,
                                           GdkWindow *bin_window,
                                           PangoLayout *layout,
                                           GtkStyleContext *context,
                                           GList *columns,
                                           cairo_rectangle_int_t *rect,
                                           GtkSortType sort_type,
                                           uint32_t row,
                                           gboolean row_selected,
                                           gboolean row_focused,
                                           gpointer user_data);

typedef void *(*FsearchListViewRowDataFunc)(int row_idx, GtkSortType sort_type, gpointer user_data);

typedef void (*FsearchListViewSortFunc)(FsearchListViewColumnType type, gpointer user_data);

G_BEGIN_DECLS

#define FSEARCH_TYPE_LIST_VIEW (fsearch_list_view_get_type())

G_DECLARE_FINAL_TYPE(FsearchListView, fsearch_list_view, FSEARCH, LIST_VIEW, GtkContainer)

G_END_DECLS

typedef struct {
    GtkWidget *button;
    GtkWidget *arrow;
    FsearchListView *view;
    FsearchListViewColumnType type;
    char *name;
    gint width;
    gint effective_width;
    gboolean expand;
    PangoAlignment alignment;
    PangoEllipsizeMode ellipsize_mode;

    GdkWindow *window;
} FsearchListViewColumn;

FsearchListViewColumn *
fsearch_list_view_column_new(FsearchListViewColumnType type,
                             char *name,
                             PangoAlignment alignment,
                             PangoEllipsizeMode ellipsize_mode,
                             gboolean expand,
                             uint32_t width);

FsearchListView *
fsearch_list_view_new();

void
fsearch_list_view_remove_column(FsearchListView *view, FsearchListViewColumn *col);

void
fsearch_list_view_append_column(FsearchListView *view, FsearchListViewColumn *col);

FsearchListViewColumn *
fsearch_list_view_get_first_column_for_type(FsearchListView *view, FsearchListViewColumnType type);

void
fsearch_list_view_set_num_rows(FsearchListView *view, uint32_t num_rows);

void
fsearch_list_view_selection_clear(FsearchListView *view);

void
fsearch_list_view_select_all(FsearchListView *view);

void
fsearch_list_view_select_range(FsearchListView *view, int start_idx, int end_idx);

void
fsearch_list_view_selection_invert(FsearchListView *view);

gboolean
fsearch_list_view_is_selected(FsearchListView *view, void *data);

uint32_t
fsearch_list_view_get_num_selected(FsearchListView *view);

void
fsearch_list_view_selection_for_each(FsearchListView *view, GHFunc func, gpointer user_data);

gint
fsearch_list_view_get_cursor(FsearchListView *view);

void
fsearch_list_view_set_cursor(FsearchListView *view, int row_idx);

void
fsearch_list_view_set_sort_type(FsearchListView *view, GtkSortType sort_type);

GtkSortType
fsearch_list_view_get_sort_type(FsearchListView *view);

void
fsearch_list_view_set_sort_func(FsearchListView *view, FsearchListViewSortFunc func, gpointer sort_func_data);

void
fsearch_list_view_set_row_data_func(FsearchListView *view,
                                    FsearchListViewRowDataFunc func,
                                    gpointer row_data_func_data);

void
fsearch_list_view_set_draw_row_func(FsearchListView *view,
                                    FsearchListViewDrawRowFunc func,
                                    gpointer draw_row_func_data);


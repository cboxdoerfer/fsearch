
#include "fsearch_list_view.h"
#include "pango/pango-attributes.h"
#include "pango/pango-layout.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_HEIGHT_FALLBACK 20
#define ROW_HEIGHT_DEFAULT 30
#define COLUMN_RESIZE_AREA_WIDTH 6

typedef enum {
    RUBBERBAND_SELECT_INACTIVE,
    RUBBERBAND_SELECT_WAITING,
    RUBBERBAND_SELECT_ACTIVE,
    NUM_RUBBERBAND_SELECT,
} FsearchListviewRubberbandState;

struct _FsearchListView {
    GtkContainer parent_instance;

    GList *columns;
    GList *columns_reversed;

    GdkWindow *bin_window;
    GdkWindow *header_window;

    GtkAdjustment *hadjustment;
    GtkAdjustment *vadjustment;

    guint hscroll_policy : 1;
    guint vscroll_policy : 1;

    GtkGesture *multi_press_gesture;
    GtkGesture *bin_drag_gesture;
    GtkGesture *header_drag_gesture;

    gboolean bin_drag_mode;
    gboolean col_resize_mode;

    FsearchListviewRubberbandState rubberband_state;

    gint drag_column_pos;
    gint x_drag_started;

    gint x_bin_drag_started;
    gint y_bin_drag_started;
    gint x_bin_drag_offset;
    gint y_bin_drag_offset;

    guint scroll_timeout;

    gint rubberband_start_idx;
    gint rubberband_end_idx;
    gboolean rubberband_extend;
    gboolean rubberband_modify;

    gboolean single_click_activate;

    // The cursor index should only be highlighted while the view is navigated with the keyboard
    gboolean highlight_cursor_idx;
    // cursor_idx is the row index which was last focused by the mouse (through a click) or keyboard
    gint cursor_idx;
    // hovered_idx is the row index which is currently being hovered by the mouse cursor
    gint hovered_idx;

    gint extend_started_idx;

    gint num_rows;
    gint row_height;

    gint header_height;

    gint min_list_width;
    gint list_height;

    GtkSortType sort_type;
    gint sort_order;

    FsearchListViewSortFunc sort_func;
    gpointer sort_func_data;

    FsearchListViewDrawRowFunc draw_row_func;
    gpointer draw_row_func_data;

    FsearchListViewQueryTooltipFunc query_tooltip_func;
    gpointer query_tooltip_func_data;

    gboolean has_selection_handlers;

    FsearchListViewIsSelectedFunc is_selected_func;
    FsearchListViewSelectFunc select_func;
    FsearchListViewSelectRangeFunc select_range_func;
    FsearchListViewSelectRangeFunc toggle_range_func;
    FsearchListViewSelectToggleFunc select_toggle_func;
    FsearchListViewUnselectAllFunc unselect_func;
    FsearchListViewNumSelectedFunc num_selected_func;
    gpointer selection_user_data;
};

enum {
    VIRTUAL_ROW_ABOVE_VIEW = -1,
    VIRTUAL_ROW_BELOW_VIEW = -2,
    UNSET_ROW = -3,
};

enum { FSEARCH_LIST_VIEW_SIGNAL_ROW_ACTIVATED, FSEARCH_LIST_VIEW_SIGNAL_POPUP, NUM_FSEARCH_LIST_VIEW_SIGNALS };

static guint signals[NUM_FSEARCH_LIST_VIEW_SIGNALS];

/* Properties */
enum {
    PROP_0,
    LAST_PROP,
    /* overridden */
    PROP_HADJUSTMENT = LAST_PROP,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY,
};

static gboolean
is_row_idx_valid(FsearchListView *view, int row_idx) {
    if (row_idx < 0) {
        return FALSE;
    }
    if (row_idx >= view->num_rows) {
        return FALSE;
    }
    return TRUE;
}

static int
get_last_row_idx(FsearchListView *view) {
    int last_row = view->num_rows - 1;
    if (last_row < 0) {
        return UNSET_ROW;
    }
    else {
        return last_row;
    }
}

static inline int
get_row_idx_for_sort_type(FsearchListView *view, int row_idx) {
    if (!is_row_idx_valid(view, row_idx)) {
        return row_idx;
    }

    if (view->sort_type == GTK_SORT_ASCENDING) {
        return row_idx;
    }
    else {
        return get_last_row_idx(view) - row_idx;
    }
}

static int
get_hscroll_pos(FsearchListView *view) {
    return (int)gtk_adjustment_get_value(view->hadjustment);
}

static int
get_vscroll_pos(FsearchListView *view) {
    return (int)gtk_adjustment_get_value(view->vadjustment);
}

static gboolean
fsearch_list_view_is_selected(FsearchListView *view, int row);

static void
fsearch_list_view_scrollable_init(GtkScrollableInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FsearchListView,
                        fsearch_list_view,
                        GTK_TYPE_CONTAINER,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, fsearch_list_view_scrollable_init))

static gboolean
fsearch_list_view_is_text_dir_rtl(FsearchListView *view) {
    return gtk_widget_get_direction(GTK_WIDGET(view)) == GTK_TEXT_DIR_RTL ? TRUE : FALSE;
}

static GList *
fsearch_list_view_get_columns_for_text_direction(FsearchListView *view) {
    const gboolean right_to_left_text = fsearch_list_view_is_text_dir_rtl(view);
    return right_to_left_text ? view->columns_reversed : view->columns;
}

static int
fsearch_list_view_get_columns_effective_width(FsearchListView *view) {
    int width = 0;
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        width += column->effective_width;
    }
    return width;
}

static int
fsearch_list_view_get_columns_width(FsearchListView *view) {
    int width = 0;
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        width += column->width;
    }
    return width;
}

static gboolean
is_row_idx_fully_in_view(FsearchListView *view, int row_idx) {
    const int y_view_start = floor(get_vscroll_pos(view));
    const int y_view_end = y_view_start + gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    const int y_row = row_idx * view->row_height;

    if (y_row > y_view_start && y_row + view->row_height < y_view_end) {
        return TRUE;
    }
    return FALSE;
}

static gboolean
get_row_rect_in_view(FsearchListView *view, int row_idx, cairo_rectangle_int_t *rec) {
    if (!is_row_idx_valid(view, row_idx)) {
        return FALSE;
    }

    const int y_view_start = floor(get_vscroll_pos(view));
    const int y_view_end = y_view_start + gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    const int y_row = row_idx * view->row_height;

    if (y_view_start - view->row_height < y_row && y_row < y_view_end) {
        rec->x = 0;
        rec->y = y_row - y_view_start;
        rec->width = gdk_window_get_width(view->bin_window);
        rec->height = view->row_height;
        return TRUE;
    }
    return FALSE;
}

static gboolean
redraw_row(FsearchListView *view, int row_idx) {
    cairo_rectangle_int_t rec = {};
    if (get_row_rect_in_view(view, row_idx, &rec)) {
        gtk_widget_queue_draw_area(GTK_WIDGET(view), rec.x, rec.y + view->header_height, rec.width, rec.height);
        return TRUE;
    }
    return FALSE;
}

static gint
fsearch_list_view_num_rows_for_view_height(FsearchListView *view) {
    return floor((gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height) / (double)view->row_height);
}

static gint
fsearch_list_view_get_row_idx_for_y_canvas(FsearchListView *view, int y_canvas) {
    if (y_canvas < 0) {
        // we're above the first row
        return VIRTUAL_ROW_ABOVE_VIEW;
    }
    int row_idx = floor((double)y_canvas / (double)view->row_height);

    if (row_idx >= view->num_rows) {
        // we're below the last row
        row_idx = VIRTUAL_ROW_BELOW_VIEW;
    }

    return row_idx;
}

static void
fsearch_list_view_convert_view_to_canvas_coords(FsearchListView *view, int x_view, int y_view, int *x_canvas, int *y_canvas) {

    if (x_canvas) {
        *x_canvas = get_hscroll_pos(view) + x_view;
    }
    if (y_canvas) {
        *y_canvas = get_vscroll_pos(view) + y_view - view->header_height;
    }
}

static gint
get_font_height_for_widget(GtkWidget *widget) {
    PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);
    g_return_val_if_fail(layout, TEXT_HEIGHT_FALLBACK);

    gint text_height;
    pango_layout_get_pixel_size(layout, NULL, &text_height);
    g_clear_object(&layout);

    return text_height;
}

static gint
fsearch_list_view_get_row_idx_for_y_view(FsearchListView *view, int y_view) {
    int y_canvas = 0;
    fsearch_list_view_convert_view_to_canvas_coords(view, 0, y_view, NULL, &y_canvas);
    return fsearch_list_view_get_row_idx_for_y_canvas(view, y_canvas);
}

static FsearchListViewColumn *
fsearch_list_view_get_col_for_x_canvas(FsearchListView *view, int x_canvas) {
    int width = 0;

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        width += MAX(0, gdk_window_get_width(view->bin_window) - fsearch_list_view_get_columns_effective_width(view));
    }

    if (width > x_canvas) {
        return NULL;
    }

    for (GList *c = columns; c != NULL; c = c->next) {
        FsearchListViewColumn *col = c->data;
        if (!col->visible) {
            continue;
        }
        width += col->effective_width;
        if (x_canvas < width) {
            return col;
        }
    }
    return NULL;
}

static FsearchListViewColumn *
fsearch_list_view_get_col_for_x_view(FsearchListView *view, int x_view) {
    int x_canvas = 0;
    fsearch_list_view_convert_view_to_canvas_coords(view, x_view, 0, &x_canvas, NULL);
    return fsearch_list_view_get_col_for_x_canvas(view, x_canvas);
}

static void
fsearch_list_view_get_rubberband_points(FsearchListView *view, double *x1, double *y1, double *x2, double *y2) {
    gdouble x_drag_start = 0;
    gdouble y_drag_start = 0;
    gtk_gesture_drag_get_start_point(GTK_GESTURE_DRAG(view->bin_drag_gesture), &x_drag_start, &y_drag_start);

    const gdouble x_drag_start_diff = view->x_bin_drag_started - x_drag_start - get_hscroll_pos(view);
    const gdouble y_drag_start_diff = view->y_bin_drag_started - y_drag_start - get_vscroll_pos(view)
                                    + view->header_height;

    *x1 = view->x_bin_drag_started;
    *y1 = view->y_bin_drag_started;

    *x2 = view->x_bin_drag_started + view->x_bin_drag_offset - x_drag_start_diff;
    *y2 = view->y_bin_drag_started + view->y_bin_drag_offset - y_drag_start_diff;
}

static void
fsearch_list_view_draw_column_header(GtkWidget *widget, GtkStyleContext *context, cairo_t *cr) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    if (!gtk_cairo_should_draw_window(cr, view->header_window)) {
        return;
    }

    gtk_style_context_save(context);
    gtk_style_context_remove_class(context, GTK_STYLE_CLASS_CELL);
    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);
    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        gtk_container_propagate_draw(GTK_CONTAINER(view), column->button, cr);
    }
    gtk_style_context_restore(context);

    const int view_width = gtk_widget_get_allocated_width(widget);
    const int columns_width = fsearch_list_view_get_columns_effective_width(view);
    if (columns_width < view_width) {
        // draw filler at the end of the column headers
        FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(view, 0);
        if (col) {
            // use the style context of the first column button
            GtkStyleContext *button_style_context = gtk_widget_get_style_context(col->button);
            GtkStateFlags flags = gtk_style_context_get_state(button_style_context);
            flags &= ~GTK_STATE_FLAG_ACTIVE;
            flags &= ~GTK_STATE_FLAG_PRELIGHT;
            flags &= ~GTK_STATE_FLAG_SELECTED;
            flags &= ~GTK_STATE_FLAG_FOCUSED;
            flags |= GTK_STATE_FLAG_INSENSITIVE;

            gtk_style_context_save(button_style_context);
            gtk_style_context_set_state(button_style_context, flags);

            const gboolean is_rtl = fsearch_list_view_is_text_dir_rtl(view);
            const int filler_x = is_rtl ? 0 : columns_width - 2;
            const int filler_width = view_width - columns_width + 2;
            gtk_render_background(button_style_context, cr, filler_x, 0, filler_width, view->header_height);
            gtk_render_frame(button_style_context, cr, filler_x, 0, filler_width, view->header_height);
            gtk_style_context_restore(button_style_context);
        }
    }
}

static void
fsearch_list_view_draw_list(GtkWidget *widget, GtkStyleContext *context, cairo_t *cr) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    if (!gtk_cairo_should_draw_window(cr, view->bin_window)) {
        return;
    }

    GtkAllocation alloc;

    gtk_widget_get_allocation(widget, &alloc);

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);

    cairo_rectangle_int_t view_rect;
    view_rect.x = 0;
    view_rect.y = view->header_height;
    view_rect.width = gtk_widget_get_allocated_width(widget);
    view_rect.height = gtk_widget_get_allocated_height(widget) - view->header_height;

    const int columns_width = fsearch_list_view_get_columns_effective_width(view);

    const int x_scroll_offset = -get_hscroll_pos(view);
    const int y_scroll_offset = -get_vscroll_pos(view);
    const int bin_window_width = gdk_window_get_width(view->bin_window);

    int x_offset = x_scroll_offset;
    const int x_rtl_offset = bin_window_width - columns_width;
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        x_offset += x_rtl_offset;
    }

    const int y_offset = y_scroll_offset % view->row_height + view->header_height;
    const int first_visible_row = floor(-y_scroll_offset / (double)view->row_height);
    const int num_rows_in_view = (int)ceil(view_rect.height / (double)view->row_height) + 1;

    cairo_save(cr);
    gdk_cairo_rectangle(cr, &view_rect);
    cairo_clip(cr);

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);

    for (int i = 0; i < num_rows_in_view; i++) {
        if (first_visible_row + i >= view->num_rows) {
            break;
        }
        cairo_rectangle_int_t row_rect;

        row_rect.x = x_offset;
        row_rect.y = y_offset + i * view->row_height;
        row_rect.width = MIN(columns_width, bin_window_width);
        row_rect.height = view->row_height;

        if (view->draw_row_func) {
            int row_idx = first_visible_row + i;
            cairo_save(cr);
            gdk_cairo_rectangle(cr, &row_rect);
            cairo_clip(cr);
            view->draw_row_func(cr,
                                view->bin_window,
                                layout,
                                context,
                                columns,
                                &row_rect,
                                get_row_idx_for_sort_type(view, row_idx),
                                fsearch_list_view_is_selected(view, row_idx),
                                view->cursor_idx == row_idx ? TRUE : FALSE,
                                view->hovered_idx == row_idx ? TRUE : FALSE,
                                fsearch_list_view_is_text_dir_rtl(view),
                                view->draw_row_func_data);
            cairo_restore(cr);
        }
    }

    if (view->highlight_cursor_idx && gtk_widget_has_focus(widget) && first_visible_row <= view->cursor_idx
        && view->cursor_idx <= first_visible_row + num_rows_in_view) {
        GtkStateFlags flags = gtk_style_context_get_state(context);
        flags |= GTK_STATE_FLAG_FOCUSED;
        if (fsearch_list_view_is_selected(view, view->cursor_idx)) {
            flags |= GTK_STATE_FLAG_SELECTED;
        }

        gtk_style_context_save(context);
        gtk_style_context_set_state(context, flags);
        gtk_render_focus(context,
                         cr,
                         x_offset,
                         y_offset + (view->cursor_idx - first_visible_row) * view->row_height,
                         columns_width,
                         view->row_height);
        gtk_style_context_restore(context);
    }

    if (view->num_rows > 0) {
        gtk_style_context_save(context);
        gtk_style_context_add_class(context, GTK_STYLE_CLASS_SEPARATOR);

        uint32_t line_x = x_offset;
        for (GList *col = columns; col != NULL; col = col->next) {
            FsearchListViewColumn *column = col->data;

            if (!col->next) {
                break;
            }
            if (!column->visible) {
                continue;
            }
            line_x += column->effective_width;
            gtk_render_line(context, cr, line_x, view_rect.y, line_x, view_rect.y + view_rect.height);
        }
        gtk_style_context_restore(context);
    }

    if (view->bin_drag_mode && view->rubberband_state == RUBBERBAND_SELECT_ACTIVE) {
        cairo_save(cr);
        gtk_style_context_save(context);
        gtk_style_context_remove_class(context, GTK_STYLE_CLASS_VIEW);
        gtk_style_context_add_class(context, GTK_STYLE_CLASS_RUBBERBAND);

        gdouble x_drag_start = 0;
        gdouble y_drag_start = 0;
        gtk_gesture_drag_get_start_point(GTK_GESTURE_DRAG(view->bin_drag_gesture), &x_drag_start, &y_drag_start);

        const gdouble x_drag_start_diff = view->x_bin_drag_started - x_drag_start + x_scroll_offset;
        const gdouble y_drag_start_diff = view->y_bin_drag_started - y_drag_start + y_scroll_offset + view->header_height;

        const double x1 = x_offset + view_rect.x + view->x_bin_drag_started;
        const double y1 = y_scroll_offset + view_rect.y + view->y_bin_drag_started;

        const double x2 = x_offset + view_rect.x + view->x_bin_drag_started + view->x_bin_drag_offset - x_drag_start_diff;
        const double y2 = y_scroll_offset + view_rect.y + view->y_bin_drag_started + view->y_bin_drag_offset
                        - y_drag_start_diff;

        GdkRectangle rect = {};
        rect.width = ABS(x1 - x2);
        rect.height = ABS(y1 - y2);
        rect.x = MIN(x1, x2);
        rect.y = MIN(y1, y2);

        gdk_cairo_rectangle(cr, &rect);
        cairo_clip(cr);

        gtk_render_background(context, cr, rect.x, rect.y, rect.width, rect.height);
        gtk_render_frame(context, cr, rect.x, rect.y, rect.width, rect.height);

        gtk_style_context_restore(context);
        cairo_restore(cr);
    }

    cairo_restore(cr);
    g_clear_object(&layout);
}

static gboolean
fsearch_list_view_draw(GtkWidget *widget, cairo_t *cr) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    GdkRectangle clip_rec = {};
    if (!gdk_cairo_get_clip_rectangle(cr, &clip_rec)) {
        return GDK_EVENT_PROPAGATE;
    }

    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);

    if (gtk_cairo_should_draw_window(cr, gtk_widget_get_window(widget))) {
        gtk_render_background(context, cr, 0, 0, width, height);

        if (clip_rec.y + clip_rec.height > view->header_height) {
            fsearch_list_view_draw_list(widget, context, cr);
        }

        if (clip_rec.y < view->header_height) {
            fsearch_list_view_draw_column_header(widget, context, cr);
        }
    }

    return GDK_EVENT_PROPAGATE;
}

static int32_t
fit_row_idx_in_view(FsearchListView *view, int32_t row_idx) {
    if (row_idx == VIRTUAL_ROW_ABOVE_VIEW) {
        return 0;
    }
    else if (row_idx == VIRTUAL_ROW_BELOW_VIEW) {
        return get_last_row_idx(view);
    }
    else {
        return CLAMP(row_idx, 0, get_last_row_idx(view));
    }
}

static void
fsearch_list_view_scroll_row_into_view(FsearchListView *view, int row_idx) {
    row_idx = fit_row_idx_in_view(view, row_idx);

    if (is_row_idx_fully_in_view(view, row_idx)) {
        return;
    }

    int view_height = gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    int y_row = view->row_height * row_idx;
    int y_view_start = (int)floor(get_vscroll_pos(view)) + view->header_height;

    if (y_view_start >= y_row - view->row_height) {
        gtk_adjustment_set_value(view->vadjustment, y_row);
    }
    else {
        gtk_adjustment_set_value(view->vadjustment, y_row - view_height + view->row_height);
    }
}

static void
fsearch_list_view_selection_changed(FsearchListView *view) {
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

static guint
fsearch_list_view_selection_num_selected(FsearchListView *view) {
    return view->has_selection_handlers ? view->num_selected_func(view->selection_user_data) : 0;
}

static void
fsearch_list_view_selection_clear_silent(FsearchListView *view) {
    if (view->has_selection_handlers) {
        view->unselect_func(view->selection_user_data);
    }
}

static void
fsearch_list_view_selection_add(FsearchListView *view, int row) {
    if (view->has_selection_handlers) {
        view->select_func(get_row_idx_for_sort_type(view, row), view->selection_user_data);
        redraw_row(view, row);
    }
}

static void
fsearch_list_view_selection_toggle_silent(FsearchListView *view, int row) {
    if (view->has_selection_handlers) {
        view->select_toggle_func(get_row_idx_for_sort_type(view, row), view->selection_user_data);
    }
}

static gboolean
fsearch_list_view_is_selected(FsearchListView *view, int row) {
    if (view->has_selection_handlers) {
        return view->is_selected_func(get_row_idx_for_sort_type(view, row), view->selection_user_data);
    }
    return FALSE;
}

static void
fsearch_list_view_selection_clear(FsearchListView *view) {
    if (view->has_selection_handlers) {
        view->unselect_func(view->selection_user_data);
        fsearch_list_view_selection_changed(view);
    }
}

static void
fsearch_list_view_select_range_silent(FsearchListView *view, int32_t start_idx, int32_t end_idx, gboolean toggle) {
    if (!view->has_selection_handlers) {
        return;
    }
    if (start_idx == UNSET_ROW || end_idx == UNSET_ROW) {
        return;
    }

    // If both start and end index point above or below the view there's nothing to select
    if (start_idx == VIRTUAL_ROW_ABOVE_VIEW && end_idx == VIRTUAL_ROW_ABOVE_VIEW) {
        return;
    }
    if (start_idx == VIRTUAL_ROW_BELOW_VIEW && end_idx == VIRTUAL_ROW_BELOW_VIEW) {
        return;
    }

    // Translate VIRTUAL_ROW_ABOVE_VIEW to the first idx and VIRTUAL_ROW_BELOW_VIEW to the last idx
    start_idx = fit_row_idx_in_view(view, start_idx);
    end_idx = fit_row_idx_in_view(view, end_idx);

    start_idx = get_row_idx_for_sort_type(view, (gint)start_idx);
    end_idx = get_row_idx_for_sort_type(view, (gint)end_idx);
    const int32_t temp_idx = start_idx;

    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = temp_idx;
    }

    end_idx = MIN(get_last_row_idx(view), end_idx);

    if (toggle) {
        view->toggle_range_func((gint)start_idx, (gint)end_idx, view->selection_user_data);
    }
    else {
        view->select_range_func((gint)start_idx, (gint)end_idx, view->selection_user_data);
    }
}

static void
fsearch_list_view_get_selection_modifiers(FsearchListView *view, gboolean *modify, gboolean *extend) {
    GtkWidget *widget = GTK_WIDGET(view);

    *modify = FALSE;
    *extend = FALSE;

    GdkModifierType state = 0;
    GdkModifierType mask;
    if (gtk_get_current_event_state(&state)) {
        mask = gtk_widget_get_modifier_mask(widget, GDK_MODIFIER_INTENT_MODIFY_SELECTION);
        if ((state & mask) == mask) {
            *modify = TRUE;
        }
        mask = gtk_widget_get_modifier_mask(widget, GDK_MODIFIER_INTENT_EXTEND_SELECTION);
        if ((state & mask) == mask) {
            *extend = TRUE;
        }
    }
}

static void
on_fsearch_list_view_multi_press_gesture_pressed(GtkGestureMultiPress *gesture,
                                                 gint n_press,
                                                 gdouble x,
                                                 gdouble y,
                                                 FsearchListView *view) {

    guint button_pressed = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    // gtk_widget_grab_focus(GTK_WIDGET(view));
    if (button_pressed > 3) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    // gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (gtk_widget_get_can_focus(GTK_WIDGET(view)) && !gtk_widget_has_focus(GTK_WIDGET(view))) {
        gtk_widget_grab_focus(GTK_WIDGET(view));
    }

    if (y < view->header_height) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    if (view->rubberband_state == RUBBERBAND_SELECT_ACTIVE) {
        return;
    }

    gboolean modify_selection;
    gboolean extend_selection;

    fsearch_list_view_get_selection_modifiers(view, &modify_selection, &extend_selection);

    int row_idx = fsearch_list_view_get_row_idx_for_y_view(view, y);

    // In modify selection mode (i.e. while Ctrl is pressed) the selection must not be cleared
    if (!is_row_idx_valid(view, row_idx) && !modify_selection) {
        fsearch_list_view_selection_clear(view);
        return;
    }

    gboolean extended_selection = FALSE;

    if (button_pressed == GDK_BUTTON_PRIMARY) {
        if (n_press == 1) {
            if (extend_selection) {
                extended_selection = TRUE;
                if (view->cursor_idx == UNSET_ROW) {
                    // The cursor index hasn't been set so far. So we start and end the extended selection
                    // at the clicked row
                    view->cursor_idx = row_idx;
                }
                if (view->extend_started_idx == UNSET_ROW) {
                    view->extend_started_idx = view->cursor_idx;
                }
                fsearch_list_view_selection_clear_silent(view);
                // Select from the last cursor index to the clicked row
                fsearch_list_view_select_range_silent(view, view->cursor_idx, row_idx, FALSE);
                // Set the cursor to the clicked row
                view->cursor_idx = row_idx;
            }
            else if (modify_selection) {
                view->cursor_idx = row_idx;
                fsearch_list_view_selection_toggle_silent(view, row_idx);
            }
            else {
                view->cursor_idx = row_idx;
                fsearch_list_view_selection_clear_silent(view);
                fsearch_list_view_selection_toggle_silent(view, row_idx);
                if (view->single_click_activate) {
                    FsearchListViewColumn *col = fsearch_list_view_get_col_for_x_view(view, x);
                    if (col) {
                        g_signal_emit(view,
                                      signals[FSEARCH_LIST_VIEW_SIGNAL_ROW_ACTIVATED],
                                      0,
                                      col->type,
                                      get_row_idx_for_sort_type(view, row_idx));
                    }
                }
            }
            fsearch_list_view_selection_changed(view);
            fsearch_list_view_scroll_row_into_view(view, view->cursor_idx);
        }

        if (n_press == 2 && !view->single_click_activate) {
            FsearchListViewColumn *col = fsearch_list_view_get_col_for_x_view(view, x);
            if (col) {
                g_signal_emit(view,
                              signals[FSEARCH_LIST_VIEW_SIGNAL_ROW_ACTIVATED],
                              0,
                              col->type,
                              get_row_idx_for_sort_type(view, row_idx));
            }
        }
    }

    if (button_pressed == GDK_BUTTON_SECONDARY && n_press == 1) {
        view->cursor_idx = row_idx;
        if (!fsearch_list_view_is_selected(view, row_idx)) {
            fsearch_list_view_selection_clear_silent(view);
            fsearch_list_view_selection_toggle_silent(view, row_idx);
            fsearch_list_view_selection_changed(view);
        }
        g_signal_emit(view, signals[FSEARCH_LIST_VIEW_SIGNAL_POPUP], 0);
    }

    view->highlight_cursor_idx = FALSE;

    gtk_widget_queue_draw(GTK_WIDGET(view));

    if (!extended_selection) {
        view->extend_started_idx = UNSET_ROW;
    }
}

static void
on_fsearch_list_view_multi_press_gesture_released(GtkGestureMultiPress *gesture,
                                                  gint n_press,
                                                  gdouble x,
                                                  gdouble y,
                                                  FsearchListView *view) {
    guint button_pressed = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button_pressed > 3) {
        return;
    }

    int row_idx = fsearch_list_view_get_row_idx_for_y_view(view, y);
    if (row_idx < 0) {
        return;
    }
}

static void
on_fsearch_list_view_bin_drag_gesture_end(GtkGestureDrag *gesture,
                                          gdouble offset_x,
                                          gdouble offset_y,
                                          FsearchListView *view) {
    //  GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));
    if (view->bin_drag_mode) {
        view->bin_drag_mode = FALSE;
        view->rubberband_state = RUBBERBAND_SELECT_INACTIVE;
        view->x_bin_drag_started = -1;
        view->y_bin_drag_started = -1;
        view->x_bin_drag_offset = -1;
        view->y_bin_drag_offset = -1;
        view->rubberband_start_idx = UNSET_ROW;
        view->rubberband_end_idx = UNSET_ROW;
        view->rubberband_extend = FALSE;
        view->rubberband_modify = FALSE;
        gtk_widget_queue_draw(GTK_WIDGET(view));
    }
}

static int
cmp_row_idx(int i1, int i2) {
    g_assert(i1 != UNSET_ROW);
    g_assert(i2 != UNSET_ROW);

    if (i1 == i2) {
        return 0;
    }

    if (i1 == VIRTUAL_ROW_BELOW_VIEW || i2 == VIRTUAL_ROW_ABOVE_VIEW) {
        return 1;
    }
    if (i2 == VIRTUAL_ROW_BELOW_VIEW || i1 == VIRTUAL_ROW_ABOVE_VIEW) {
        return -1;
    }
    return i1 - i2;
}

static void
rubberband_toggle_range(FsearchListView *view, int start_idx, int end_idx, int prev_end_idx) {
    int cmp_prev_new_res = cmp_row_idx(prev_end_idx, end_idx);
    int cmp_start_end_res = cmp_row_idx(start_idx, end_idx);
    int cmp_start_prev_end_res = cmp_row_idx(start_idx, prev_end_idx);
    if (cmp_prev_new_res == 0) {
        g_assert_not_reached();
    }
    else if (cmp_prev_new_res > 0) {
        // end < prev_end
        if (cmp_start_prev_end_res >= 0) {
            // end < prev_end <= start
            // selection grows upwards
            fsearch_list_view_select_range_silent(view,
                                                  end_idx,
                                                  prev_end_idx == VIRTUAL_ROW_BELOW_VIEW ? get_last_row_idx(view)
                                                                                         : prev_end_idx - 1,
                                                  TRUE);
        }
        else {
            // start < prev_end
            if (cmp_start_end_res > 0) {
                // end < start < prev_end
                // toggle everything from end_idx to prev_end_idx, except start_idx
                fsearch_list_view_select_range_silent(view, end_idx, prev_end_idx, TRUE);
                fsearch_list_view_selection_toggle_silent(view, start_idx);
            }
            else {
                // start <= end < prev_end
                // toggle everything after end_idx til prev_end
                if (end_idx + 1 <= get_last_row_idx(view)) {
                    fsearch_list_view_select_range_silent(view,
                                                          end_idx + 1,
                                                          prev_end_idx == VIRTUAL_ROW_BELOW_VIEW ? get_last_row_idx(view)
                                                                                                 : prev_end_idx,
                                                          TRUE);
                }
            }
        }
    }
    else {
        // prev_end < end
        if (cmp_start_prev_end_res > 0) {
            // prev_end < start
            if (cmp_start_end_res >= 0) {
                // prev_end < end <= start
                // toggle everything from prev_end to the row before end
                fsearch_list_view_select_range_silent(view,
                                                      prev_end_idx,
                                                      end_idx == VIRTUAL_ROW_BELOW_VIEW ? get_last_row_idx(view)
                                                                                        : end_idx - 1,
                                                      TRUE);
            }
            else {
                // prev_end < start < end
                // toggle everything from prev_end to end, except start_idx
                fsearch_list_view_select_range_silent(view, prev_end_idx, end_idx, TRUE);
                fsearch_list_view_selection_toggle_silent(view, start_idx);
            }
        }
        else {
            // start <= prev_end < end
            // toggle everything after prev_end til end
            if (prev_end_idx + 1 <= get_last_row_idx(view)) {
                fsearch_list_view_select_range_silent(view,
                                                      prev_end_idx + 1,
                                                      end_idx == VIRTUAL_ROW_BELOW_VIEW ? get_last_row_idx(view) : end_idx,
                                                      TRUE);
            }
        }
    }
}

static void
update_rubberband_selection(FsearchListView *view) {
    if (!view->bin_drag_mode) {
        return;
    }
    gdouble offset_x;
    gdouble offset_y;
    gtk_gesture_drag_get_offset(GTK_GESTURE_DRAG(view->bin_drag_gesture), &offset_x, &offset_y);
    view->highlight_cursor_idx = FALSE;
    view->rubberband_state = RUBBERBAND_SELECT_ACTIVE;
    view->x_bin_drag_offset = offset_x;
    view->y_bin_drag_offset = offset_y;

    double x1, y1, x2, y2;
    fsearch_list_view_get_rubberband_points(view, &x1, &y1, &x2, &y2);
    int start_idx = fsearch_list_view_get_row_idx_for_y_canvas(view, y1);
    int end_idx = fsearch_list_view_get_row_idx_for_y_canvas(view, y2);
    int prev_start_idx = view->rubberband_start_idx;
    int prev_end_idx = view->rubberband_end_idx;

    view->rubberband_start_idx = start_idx;
    view->rubberband_end_idx = end_idx;

    // Only update selection when our end_idx changed to last time
    if (end_idx == prev_end_idx) {
        return;
    }
    // The start index should always stay the same while the rubber band is moved
    // only in the very first call of each rubber band selection are they different
    if (start_idx != prev_start_idx) {
        return;
    }

    view->cursor_idx = MAX(fit_row_idx_in_view(view, start_idx), fit_row_idx_in_view(view, end_idx));
    view->extend_started_idx = MIN(fit_row_idx_in_view(view, start_idx), fit_row_idx_in_view(view, end_idx));
    if (view->rubberband_modify) {
        // rubber band selection while Ctrl key is pressed
        rubberband_toggle_range(view, start_idx, end_idx, prev_end_idx);
    }
    else {
        // Normal ranged selection
        fsearch_list_view_selection_clear_silent(view);
        fsearch_list_view_select_range_silent(view, start_idx, end_idx, FALSE);
    }
    fsearch_list_view_selection_changed(view);
    return;
}

static gboolean
vertical_autoscroll(gpointer data) {
    FsearchListView *view = data;
    if (!gtk_gesture_is_recognized(view->bin_drag_gesture)) {
        goto out;
    }
    double y_drag_start = 0;
    if (!gtk_gesture_drag_get_start_point(GTK_GESTURE_DRAG(view->bin_drag_gesture), NULL, &y_drag_start)) {
        goto out;
    }
    double y_drag_offset = 0;
    if (!gtk_gesture_drag_get_offset(GTK_GESTURE_DRAG(view->bin_drag_gesture), NULL, &y_drag_offset)) {
        goto out;
    }

    const double y_drag_point = y_drag_start + y_drag_offset;
    const double view_height = gtk_widget_get_allocated_height(GTK_WIDGET(view));

    double scroll_offset = 0;
    if (y_drag_point < view->header_height) {
        // the cursor is above the view -> scroll up
        scroll_offset = y_drag_point - view->header_height;
    }
    else if (y_drag_point > view_height) {
        // the cursor is below the view -> scroll down
        scroll_offset = y_drag_point - view_height;
    }

    if (scroll_offset == 0) {
        goto out;
    }

    // Make sure the rubberband selection gets updated while scrolling
    view->hovered_idx = UNSET_ROW;

    gtk_adjustment_set_value(view->vadjustment, MAX(get_vscroll_pos(view) + scroll_offset, 0.0));
    return G_SOURCE_CONTINUE;

out:
    view->scroll_timeout = 0;
    return G_SOURCE_REMOVE;
}

static void
add_vertical_autoscroll_timeout(FsearchListView *view) {
    if (view->scroll_timeout == 0) {
        view->scroll_timeout = g_timeout_add(33, vertical_autoscroll, view);
    }
}

static void
on_fsearch_list_view_bin_drag_gesture_update(GtkGestureDrag *gesture,
                                             gdouble offset_x,
                                             gdouble offset_y,
                                             FsearchListView *view) {
    GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));

    // if (gtk_gesture_get_sequence_state(GTK_GESTURE(gesture), sequence) != GTK_EVENT_SEQUENCE_CLAIMED) {
    //     return;
    // }

    update_rubberband_selection(view);
    add_vertical_autoscroll_timeout(view);
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

static void
on_fsearch_list_view_bin_drag_gesture_begin(GtkGestureDrag *gesture,
                                            gdouble start_x,
                                            gdouble start_y,
                                            FsearchListView *view) {
    if (start_y > view->header_height && !view->single_click_activate) {
        if (!gtk_widget_has_focus(GTK_WIDGET(view))) {
            gtk_widget_grab_focus(GTK_WIDGET(view));
        }

        view->x_bin_drag_started = start_x + get_hscroll_pos(view);
        view->y_bin_drag_started = start_y + get_vscroll_pos(view) - view->header_height;
        view->bin_drag_mode = TRUE;
        view->rubberband_state = RUBBERBAND_SELECT_WAITING;
        fsearch_list_view_get_selection_modifiers(view, &view->rubberband_modify, &view->rubberband_extend);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

static void
on_fsearch_list_view_header_drag_gesture_end(GtkGestureDrag *gesture,
                                             gdouble offset_x,
                                             gdouble offset_y,
                                             FsearchListView *view) {
    // GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));
    if (view->col_resize_mode) {
        view->col_resize_mode = FALSE;
        view->drag_column_pos = -1;
    }
}

static void
on_fsearch_list_view_header_drag_gesture_update(GtkGestureDrag *gesture,
                                                gdouble offset_x,
                                                gdouble offset_y,
                                                FsearchListView *view) {
    GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));

    if (gtk_gesture_get_sequence_state(GTK_GESTURE(gesture), sequence) != GTK_EVENT_SEQUENCE_CLAIMED) {
        return;
    }

    gdouble start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    gdouble x = start_x;
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        x -= offset_x;
    }
    else {
        x += offset_x;
    }

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);
    if (view->col_resize_mode) {
        GList *c = g_list_nth(columns, view->drag_column_pos);
        if (!c) {
            return;
        }

        FsearchListViewColumn *col = c->data;
        col->width = x - view->x_drag_started;
        col->width = MAX(30, col->width);
        gtk_widget_queue_resize(GTK_WIDGET(view));
    }
}

static void
on_fsearch_list_view_header_drag_gesture_begin(GtkGestureDrag *gesture,
                                               gdouble start_x,
                                               gdouble start_y,
                                               FsearchListView *view) {
    GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));
    const GdkEvent *event = gtk_gesture_get_last_event(GTK_GESTURE(gesture), sequence);
    GdkWindow *window = event->any.window;

    gint col_pos = 0;
    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);

    for (GList *col = columns; col; col = col->next, col_pos++) {
        FsearchListViewColumn *column = col->data;
        if (window != column->window) {
            continue;
        }
        if (!column->visible) {
            continue;
        }

        view->col_resize_mode = TRUE;

        view->drag_column_pos = col_pos;
        view->x_drag_started = start_x - column->effective_width;

        if (!gtk_widget_has_focus(GTK_WIDGET(view))) {
            gtk_widget_grab_focus(GTK_WIDGET(view));
        }

        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        return;
    }
}

static gboolean
fsearch_list_view_key_press_event(GtkWidget *widget, GdkEventKey *event) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    if (view->rubberband_state == RUBBERBAND_SELECT_ACTIVE) {
        // Don't support key press events while rubber band selection is active
        return GDK_EVENT_STOP;
    }

    gboolean modify_selection;
    gboolean extend_selection;

    fsearch_list_view_get_selection_modifiers(view, &modify_selection, &extend_selection);

    guint keyval;
    gdk_event_get_keyval((GdkEvent *)event, &keyval);

    int d_idx = 0;
    switch (keyval) {
    case GDK_KEY_Up:
        d_idx = -1;
        break;
    case GDK_KEY_Down:
        d_idx = 1;
        break;
    case GDK_KEY_Page_Up:
        d_idx = -fsearch_list_view_num_rows_for_view_height(view);
        break;
    case GDK_KEY_Page_Down:
        d_idx = fsearch_list_view_num_rows_for_view_height(view);
        break;
    case GDK_KEY_Home:
        d_idx = -view->cursor_idx;
        break;
    case GDK_KEY_End:
        d_idx = get_last_row_idx(view) - view->cursor_idx;
        break;
    case GDK_KEY_Menu:
        // TODO: Popup menu at the last selected item, instead of the mouse pointer position (scroll to it if necessary)
        g_signal_emit(view, signals[FSEARCH_LIST_VIEW_SIGNAL_POPUP], 0);
        return TRUE;
    case GDK_KEY_F10:
        if (extend_selection) {
            // Shift + F10 -> open context menu
            g_signal_emit(view, signals[FSEARCH_LIST_VIEW_SIGNAL_POPUP], 0);
            return GDK_EVENT_STOP;
        }
    default:
        return GDK_EVENT_PROPAGATE;
    }

    if (d_idx != 0) {
        int prev_focused_idx = view->cursor_idx;
        if (view->cursor_idx >= 0) {
            prev_focused_idx = view->cursor_idx;
        }
        else {
            prev_focused_idx = 0;
        }

        view->highlight_cursor_idx = TRUE;
        view->cursor_idx = CLAMP(prev_focused_idx + d_idx, 0, get_last_row_idx(view));

        const guint num_selected = fsearch_list_view_selection_num_selected(view);

        if (extend_selection) {
            if (view->extend_started_idx == UNSET_ROW) {
                view->extend_started_idx = prev_focused_idx;
            }
            if (num_selected > 0) {
                fsearch_list_view_selection_clear_silent(view);
            }
            fsearch_list_view_select_range_silent(view, view->extend_started_idx, view->cursor_idx, FALSE);
        }
        else if (!modify_selection) {
            view->extend_started_idx = UNSET_ROW;
            if (num_selected > 0) {
                fsearch_list_view_selection_clear_silent(view);
            }
            fsearch_list_view_selection_toggle_silent(view, view->cursor_idx);
        }

        fsearch_list_view_selection_changed(view);
        fsearch_list_view_scroll_row_into_view(view, view->cursor_idx);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gint
fsearch_list_view_focus_out_event(GtkWidget *widget, GdkEventFocus *event) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    redraw_row(view, view->cursor_idx);
    return GTK_WIDGET_CLASS(fsearch_list_view_parent_class)->focus_out_event(widget, event);
}

static gboolean
fsearch_list_view_query_tooltip(GtkWidget *widget, int x, int y, gboolean keyboard_mode, GtkTooltip *tooltip) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    if (!view->query_tooltip_func) {
        return FALSE;
    }
    int row_idx = fsearch_list_view_get_row_idx_for_y_view(view, y);
    FsearchListViewColumn *col = fsearch_list_view_get_col_for_x_view(view, x);
    if (row_idx < 0 || !col) {
        return FALSE;
    }

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);
    if (!layout) {
        return FALSE;
    }

    g_autofree char *tooltip_text = view->query_tooltip_func(layout,
                                                             view->row_height,
                                                             get_row_idx_for_sort_type(view, row_idx),
                                                             col,
                                                             view->query_tooltip_func_data);
    g_clear_object(&layout);

    if (tooltip_text) {
        gtk_tooltip_set_text(tooltip, tooltip_text);
        return TRUE;
    }
    return FALSE;
}

static gboolean
fsearch_list_view_get_border(GtkScrollable *scrollable, GtkBorder *border) {
    FsearchListView *view = FSEARCH_LIST_VIEW(scrollable);
    border->top = view->header_height;
    return TRUE;
}

static void
fsearch_list_view_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchListView *view = FSEARCH_LIST_VIEW(object);

    switch (prop_id) {
    case PROP_HADJUSTMENT:
        g_value_set_object(value, view->hadjustment);
        break;
    case PROP_VADJUSTMENT:
        g_value_set_object(value, view->vadjustment);
        break;
    case PROP_HSCROLL_POLICY:
        g_value_set_enum(value, view->hscroll_policy);
        break;
    case PROP_VSCROLL_POLICY:
        g_value_set_enum(value, view->vscroll_policy);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
update_hovered_idx_for_current_cursor_position(FsearchListView *view) {
#if GTK_CHECK_VERSION(3, 20, 0)
    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
    if (!seat) {
        return;
    }
    GdkDevice *pointer_device = gdk_seat_get_pointer(seat);
#else
    GdkDeviceManager *device_manager = gdk_display_get_device_manager(gdk_display_get_default());
    if (!device_manager) {
        return;
    }
    GdkDevice *pointer_device = gdk_device_manager_get_client_pointer(device_manager);
#endif
    if (!pointer_device) {
        return;
    }
    gint x_pointer = 0;
    gint y_pointer = 0;
    const int view_width = gtk_widget_get_allocated_width(GTK_WIDGET(view));
    const int x_view_in_bin_window = get_hscroll_pos(view);
    gdk_window_get_device_position(view->bin_window, pointer_device, &x_pointer, &y_pointer, NULL);
    if (x_pointer >= x_view_in_bin_window && x_pointer <= view_width) {
        view->hovered_idx = fsearch_list_view_get_row_idx_for_y_canvas(view, y_pointer);
    }
}

static void
on_fsearch_list_view_adjustment_changed(GtkAdjustment *adjustment, FsearchListView *view) {
    if (gtk_widget_get_realized(GTK_WIDGET(view))) {
        gdk_window_move(view->bin_window, -get_hscroll_pos(view), -get_vscroll_pos(view) + view->header_height);
        gdk_window_move(view->header_window, -get_hscroll_pos(view), 0);

        // We don't receive motion events while scrolling, because the mouse cursor usually doesn't move,
        // but we still need to update the hovered index and rubberband, since the content below the cursor moves.
        update_hovered_idx_for_current_cursor_position(view);
        update_rubberband_selection(view);
    }
}

static void
fsearch_list_view_set_adjustment_value(GtkAdjustment *adjustment, double allocated_size, double size) {
    const gdouble prev_value = gtk_adjustment_get_value(adjustment);
    const gdouble new_upper = MAX(allocated_size, size);

    g_object_set(adjustment,
                 "lower",
                 0.0,
                 "upper",
                 new_upper,
                 "page-size",
                 (gdouble)allocated_size,
                 "step-increment",
                 allocated_size * 0.1,
                 "page-increment",
                 allocated_size * 0.9,
                 NULL);

    const gdouble new_value = CLAMP(prev_value, 0, new_upper - allocated_size);
    if (new_value != prev_value) {
        gtk_adjustment_set_value(adjustment, new_value);
    }
}

static void
fsearch_list_view_set_hadjustment_value(FsearchListView *view) {
    const gint width = gtk_widget_get_allocated_width(GTK_WIDGET(view));
    fsearch_list_view_set_adjustment_value(view->hadjustment, width, view->min_list_width);
}

static void
fsearch_list_view_set_vadjustment_value(FsearchListView *view) {
    const gint height = gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    fsearch_list_view_set_adjustment_value(view->vadjustment, height, view->list_height);
}

static void
fsearch_list_view_set_hadjustment(FsearchListView *view, GtkAdjustment *adjustment) {
    if (adjustment && view->hadjustment == adjustment) {
        return;
    }

    if (view->hadjustment != NULL) {
        g_signal_handlers_disconnect_by_func(view->hadjustment, on_fsearch_list_view_adjustment_changed, view);
        g_clear_object(&view->hadjustment);
    }

    if (adjustment == NULL) {
        adjustment = gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }

    g_signal_connect(adjustment, "value-changed", G_CALLBACK(on_fsearch_list_view_adjustment_changed), view);
    view->hadjustment = g_object_ref_sink(adjustment);

    fsearch_list_view_set_hadjustment_value(view);

    g_object_notify(G_OBJECT(view), "hadjustment");
}

static void
fsearch_list_view_set_vadjustment(FsearchListView *view, GtkAdjustment *adjustment) {
    if (adjustment && view->vadjustment == adjustment) {
        return;
    }

    if (view->vadjustment != NULL) {
        g_signal_handlers_disconnect_by_func(view->vadjustment, on_fsearch_list_view_adjustment_changed, view);
        g_clear_object(&view->vadjustment);
    }

    if (adjustment == NULL) {
        adjustment = gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }

    g_signal_connect(adjustment, "value-changed", G_CALLBACK(on_fsearch_list_view_adjustment_changed), view);
    view->vadjustment = g_object_ref_sink(adjustment);

    fsearch_list_view_set_vadjustment_value(view);

    g_object_notify(G_OBJECT(view), "vadjustment");
}

static void
fsearch_list_view_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchListView *view = FSEARCH_LIST_VIEW(object);

    switch (prop_id) {
    case PROP_HADJUSTMENT:
        fsearch_list_view_set_hadjustment(view, g_value_get_object(value));

        break;
    case PROP_VADJUSTMENT:
        fsearch_list_view_set_vadjustment(view, g_value_get_object(value));
        break;
    case PROP_HSCROLL_POLICY:
        if (view->hscroll_policy != g_value_get_enum(value)) {
            view->hscroll_policy = g_value_get_enum(value);
            gtk_widget_queue_resize(GTK_WIDGET(view));
            g_object_notify_by_pspec(object, pspec);
        }
        break;
    case PROP_VSCROLL_POLICY:
        if (view->vscroll_policy != g_value_get_enum(value)) {
            view->vscroll_policy = g_value_get_enum(value);
            gtk_widget_queue_resize(GTK_WIDGET(view));
            g_object_notify_by_pspec(object, pspec);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static int
fsearch_list_view_num_expanding_columns(FsearchListView *view) {
    int num_expanding = 0;
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        if (column->expand) {
            num_expanding++;
        }
    }
    return 0;
}

static void
fsearch_list_view_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    gtk_widget_set_allocation(widget, allocation);

    int columns_width = fsearch_list_view_get_columns_effective_width(view);
    int view_width = gtk_widget_get_allocated_width(widget);
    int num_expanding = fsearch_list_view_num_expanding_columns(view);

    int x_extra_space = 0;
    if (num_expanding > 0 && view_width > columns_width) {
        x_extra_space = floor((view_width - columns_width) / (double)num_expanding);
    }

    const gboolean text_dir_rtl = fsearch_list_view_is_text_dir_rtl(view);
    const int x_offset = MAX(text_dir_rtl ? view_width - columns_width : 0, 0);
    int x = x_offset;

    view->min_list_width = 0;

    int header_font_height = 0;

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);

    // the header height is determined by the font size of the columns
    // they're probably the same size, so only looking at the first one would suffice,
    // but just to be sure we'll look for the maximum
    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        header_font_height = MAX(header_font_height, get_font_height_for_widget(column->button));
    }

    view->header_height = header_font_height + 2 * ROW_PADDING_Y;

    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        GdkRectangle header_button_rect;
        header_button_rect.x = x;
        header_button_rect.y = 0;
        header_button_rect.width = column->width;
        header_button_rect.height = view->header_height;

        if (column->expand && !view->col_resize_mode) {
            header_button_rect.width += x_extra_space;
        }
        x += header_button_rect.width;

        column->effective_width = header_button_rect.width;
        view->min_list_width += column->effective_width;

        gtk_widget_size_allocate(column->button, &header_button_rect);
        if (gtk_widget_get_realized(column->button)) {
            int x_win = x - COLUMN_RESIZE_AREA_WIDTH / 2;
            if (fsearch_list_view_is_text_dir_rtl(view)) {
                x_win -= header_button_rect.width;
            }
            gdk_window_move_resize(column->window,
                                   x_win,
                                   header_button_rect.y,
                                   COLUMN_RESIZE_AREA_WIDTH,
                                   header_button_rect.height);
        }
    }

    if (gtk_widget_get_realized(widget)) {
        int font_height = get_font_height_for_widget(GTK_WIDGET(view));
        view->row_height = font_height + 2 * ROW_PADDING_Y;
        view->list_height = view->row_height * view->num_rows;
        gdk_window_move_resize(gtk_widget_get_window(widget),
                               allocation->x,
                               allocation->y,
                               allocation->width,
                               allocation->height);
        gdk_window_move_resize(view->bin_window,
                               -get_hscroll_pos(view),
                               view->header_height - get_vscroll_pos(view),
                               MAX(view->min_list_width, allocation->width),
                               MAX(view->list_height, allocation->height - view->header_height));
        gdk_window_move_resize(view->header_window,
                               -get_hscroll_pos(view),
                               0,
                               MAX(view->min_list_width, allocation->width),
                               view->header_height);
    }

    fsearch_list_view_set_hadjustment_value(view);
    fsearch_list_view_set_vadjustment_value(view);
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        const gdouble hadj_new_upper = gtk_adjustment_get_upper(view->hadjustment);
        gtk_adjustment_set_value(view->hadjustment, hadj_new_upper);
    }
}

static void
fsearch_list_view_get_preferred_height(GtkWidget *widget, gint *minimum, gint *natural) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    *minimum = *natural = view->num_rows * view->row_height;
}

static void
fsearch_list_view_get_preferred_width(GtkWidget *widget, gint *minimum, gint *natural) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    *minimum = *natural = view->min_list_width;
}

static void
fsearch_list_view_container_for_all(GtkContainer *container,
                                    gboolean include_internals,
                                    GtkCallback callback,
                                    gpointer callback_data) {
    FsearchListView *view = FSEARCH_LIST_VIEW(container);
    if (!include_internals) {
        return;
    }
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        (*callback)(column->button, callback_data);
    }
}

static void
fsearch_list_view_container_remove(GtkContainer *container, GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(container);

    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (column->button == widget) {
            view->columns_reversed = g_list_remove(view->columns_reversed, column);
            gtk_widget_unparent(widget);
            return;
        }
    }
}

static void
fsearch_list_view_map(GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    gtk_widget_set_mapped(widget, TRUE);

    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!gtk_widget_get_visible(column->button) || gtk_widget_get_mapped(column->button)) {
            continue;
        }
        gtk_widget_map(column->button);
        gdk_window_raise(column->window);
        gdk_window_show(column->window);
    }

    gdk_window_show(view->bin_window);
    gdk_window_show(view->header_window);
    gdk_window_show(gtk_widget_get_window(widget));
}

static void
fsearch_list_view_grab_focus(GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    GTK_WIDGET_CLASS(fsearch_list_view_parent_class)->grab_focus(widget);
}

static gboolean
fsearch_list_view_leave_notify_event(GtkWidget *widget, GdkEventCrossing *event) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    if (gtk_widget_get_realized(widget)) {
        gdk_window_set_cursor(view->bin_window, NULL);
        redraw_row(view, view->hovered_idx);
    }
    view->hovered_idx = UNSET_ROW;

    return GDK_EVENT_PROPAGATE;
}

static gboolean
fsearch_list_view_motion_notify_event(GtkWidget *widget, GdkEventMotion *event) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    const gint prev_hovered_idx = view->hovered_idx;

    if (event->window != view->bin_window) {
        view->hovered_idx = UNSET_ROW;
    }
    else {
        view->hovered_idx = fsearch_list_view_get_row_idx_for_y_canvas(view, (int)(event->y));

        if (view->single_click_activate && view->hovered_idx >= 0) {
            g_autoptr(GdkCursor) cursor = gdk_cursor_new_for_display(gdk_window_get_display(event->window), GDK_HAND2);
            gdk_window_set_cursor(event->window, cursor);
        }
        else {
            gdk_window_set_cursor(event->window, NULL);
        }
    }

    if (prev_hovered_idx != view->hovered_idx) {
        redraw_row(view, prev_hovered_idx);
        redraw_row(view, view->hovered_idx);
    }

    return GTK_WIDGET_CLASS(fsearch_list_view_parent_class)->motion_notify_event(widget, event);
}

static void
fsearch_list_view_unrealize_column(FsearchListView *view, FsearchListViewColumn *column) {
    gtk_widget_unregister_window(GTK_WIDGET(view), column->window);
    g_clear_pointer(&column->window, gdk_window_destroy);
}

static void
fsearch_list_view_unrealize_columns(FsearchListView *view) {
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;

        fsearch_list_view_unrealize_column(view, column);
    }
}

static void
fsearch_list_view_unrealize(GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    fsearch_list_view_unrealize_columns(view);

    gtk_widget_unregister_window(widget, view->bin_window);
    g_clear_pointer(&view->bin_window, gdk_window_destroy);

    gtk_widget_unregister_window(widget, view->header_window);
    g_clear_pointer(&view->header_window, gdk_window_destroy);

    gtk_gesture_set_window(view->multi_press_gesture, NULL);

    GTK_WIDGET_CLASS(fsearch_list_view_parent_class)->unrealize(widget);
}

static void
fsearch_list_view_realize_column(FsearchListView *view, FsearchListViewColumn *col) {
    g_return_if_fail(gtk_widget_get_realized(GTK_WIDGET(view)));
    g_return_if_fail(col->button != NULL);

    gtk_widget_set_parent_window(col->button, view->header_window);

    GdkWindowAttr attrs;
    attrs.window_type = GDK_WINDOW_CHILD;
    attrs.wclass = GDK_INPUT_ONLY;
    attrs.visual = gtk_widget_get_visual(GTK_WIDGET(view));
    attrs.event_mask = gtk_widget_get_events(GTK_WIDGET(view))
                     | (GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);
    guint attrs_mask = GDK_WA_CURSOR | GDK_WA_X | GDK_WA_Y;
    GdkDisplay *display = gdk_window_get_display(view->header_window);
    attrs.cursor = gdk_cursor_new_from_name(display, "col-resize");
    attrs.y = 0;
    attrs.width = COLUMN_RESIZE_AREA_WIDTH;
    attrs.height = view->header_height;

    GtkAllocation allocation;
    gtk_widget_get_allocation(col->button, &allocation);
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        attrs.x = (-COLUMN_RESIZE_AREA_WIDTH / 2);
    }
    else {
        attrs.x = allocation.width - COLUMN_RESIZE_AREA_WIDTH / 2;
    }

    col->window = gdk_window_new(view->header_window, &attrs, attrs_mask);
    gtk_widget_register_window(GTK_WIDGET(view), col->window);

    g_clear_object(&attrs.cursor);
}

static void
fsearch_list_view_realize(GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    GtkAllocation allocation;
    GdkWindowAttr attrs;
    guint attrs_mask;

    gtk_widget_set_realized(widget, TRUE);

    gtk_widget_get_allocation(widget, &allocation);

    attrs.window_type = GDK_WINDOW_CHILD;
    attrs.x = allocation.x;
    attrs.y = allocation.y;
    attrs.width = allocation.width;
    attrs.height = allocation.height;
    attrs.wclass = GDK_INPUT_OUTPUT;
    attrs.visual = gtk_widget_get_visual(widget);
    attrs.event_mask = GDK_VISIBILITY_NOTIFY_MASK;

    attrs_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

    GdkWindow *window = gdk_window_new(gtk_widget_get_parent_window(widget), &attrs, attrs_mask);
    gtk_widget_set_window(widget, window);
    gtk_widget_register_window(widget, window);

    gtk_widget_get_allocation(widget, &allocation);

    attrs.x = 0;
    attrs.y = view->header_height;
    attrs.width = MAX(view->min_list_width, allocation.width);
    attrs.height = MAX(view->list_height, allocation.height);
    attrs.event_mask = (GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK
                        | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                        | gtk_widget_get_events(widget));

    view->bin_window = gdk_window_new(window, &attrs, attrs_mask);
    gtk_widget_register_window(widget, view->bin_window);

    attrs.x = 0;
    attrs.y = 0;
    attrs.width = MAX(view->min_list_width, allocation.width);
    attrs.height = view->header_height;
    attrs.event_mask = (GDK_EXPOSURE_MASK | GDK_SCROLL_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK
                        | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK
                        | gtk_widget_get_events(widget));

    view->header_window = gdk_window_new(window, &attrs, attrs_mask);
    gtk_widget_register_window(widget, view->header_window);

    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        fsearch_list_view_realize_column(view, column);
    }

    gtk_gesture_set_window(view->multi_press_gesture, view->bin_window);
}

static void
fsearch_list_view_destroy(GtkWidget *widget) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);

    g_list_free(g_steal_pointer(&view->columns_reversed));
    g_list_free_full(g_steal_pointer(&view->columns), (GDestroyNotify)fsearch_list_view_column_unref);
    g_clear_object(&view->multi_press_gesture);
    g_clear_object(&view->bin_drag_gesture);
    g_clear_object(&view->header_drag_gesture);

    GTK_WIDGET_CLASS(fsearch_list_view_parent_class)->destroy(widget);
}

static void
fsearch_list_view_class_init(FsearchListViewClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);

    object_class->set_property = fsearch_list_view_set_property;
    object_class->get_property = fsearch_list_view_get_property;

    g_object_class_override_property(object_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(object_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(object_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(object_class, PROP_VSCROLL_POLICY, "vscroll-policy");

    widget_class->destroy = fsearch_list_view_destroy;
    widget_class->draw = fsearch_list_view_draw;
    widget_class->realize = fsearch_list_view_realize;
    widget_class->unrealize = fsearch_list_view_unrealize;
    widget_class->map = fsearch_list_view_map;
    widget_class->size_allocate = fsearch_list_view_size_allocate;
    widget_class->get_preferred_width = fsearch_list_view_get_preferred_width;
    widget_class->get_preferred_height = fsearch_list_view_get_preferred_height;
    widget_class->key_press_event = fsearch_list_view_key_press_event;
    widget_class->query_tooltip = fsearch_list_view_query_tooltip;
    widget_class->grab_focus = fsearch_list_view_grab_focus;
    widget_class->focus_out_event = fsearch_list_view_focus_out_event;
    widget_class->motion_notify_event = fsearch_list_view_motion_notify_event;
    widget_class->leave_notify_event = fsearch_list_view_leave_notify_event;

    container_class->forall = fsearch_list_view_container_for_all;
    container_class->remove = fsearch_list_view_container_remove;

    signals[FSEARCH_LIST_VIEW_SIGNAL_POPUP] =
        g_signal_new("row-popup", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

    signals[FSEARCH_LIST_VIEW_SIGNAL_ROW_ACTIVATED] = g_signal_new("row-activated",
                                                                   G_TYPE_FROM_CLASS(klass),
                                                                   G_SIGNAL_RUN_LAST,
                                                                   0,
                                                                   NULL,
                                                                   NULL,
                                                                   NULL,
                                                                   G_TYPE_NONE,
                                                                   2,
                                                                   G_TYPE_INT,
                                                                   G_TYPE_INT);

#if GTK_CHECK_VERSION(3, 20, 0)
    gtk_widget_class_set_css_name(widget_class, "treeview");
#endif
}

static void
fsearch_list_view_init(FsearchListView *view) {
    view->bin_window = NULL;

    view->hadjustment = NULL;
    view->vadjustment = NULL;

    view->num_rows = 0;
    view->row_height = ROW_HEIGHT_DEFAULT;

    view->header_height = ROW_HEIGHT_DEFAULT;

    view->cursor_idx = UNSET_ROW;
    view->highlight_cursor_idx = FALSE;

    view->extend_started_idx = UNSET_ROW;

    view->scroll_timeout = 0;

    view->min_list_width = 0;
    view->list_height = view->num_rows * view->row_height;

    gtk_widget_set_sensitive(GTK_WIDGET(view), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(view), TRUE);

    view->multi_press_gesture = gtk_gesture_multi_press_new(GTK_WIDGET(view));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(view->multi_press_gesture), 0);
    g_signal_connect(view->multi_press_gesture,
                     "pressed",
                     G_CALLBACK(on_fsearch_list_view_multi_press_gesture_pressed),
                     view);
    g_signal_connect(view->multi_press_gesture,
                     "released",
                     G_CALLBACK(on_fsearch_list_view_multi_press_gesture_released),
                     view);

    view->bin_drag_gesture = gtk_gesture_drag_new(GTK_WIDGET(view));
    g_signal_connect(view->bin_drag_gesture, "drag-begin", G_CALLBACK(on_fsearch_list_view_bin_drag_gesture_begin), view);
    g_signal_connect(view->bin_drag_gesture, "drag-update", G_CALLBACK(on_fsearch_list_view_bin_drag_gesture_update), view);
    g_signal_connect(view->bin_drag_gesture, "drag-end", G_CALLBACK(on_fsearch_list_view_bin_drag_gesture_end), view);

    view->header_drag_gesture = gtk_gesture_drag_new(GTK_WIDGET(view));
    g_signal_connect(view->header_drag_gesture,
                     "drag-begin",
                     G_CALLBACK(on_fsearch_list_view_header_drag_gesture_begin),
                     view);
    g_signal_connect(view->header_drag_gesture,
                     "drag-update",
                     G_CALLBACK(on_fsearch_list_view_header_drag_gesture_update),
                     view);
    g_signal_connect(view->header_drag_gesture, "drag-end", G_CALLBACK(on_fsearch_list_view_header_drag_gesture_end), view);
    GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(view));
    gtk_style_context_add_class(style, GTK_STYLE_CLASS_VIEW);
    gtk_style_context_add_class(style, GTK_STYLE_CLASS_LINKED);
    // gtk_style_context_add_class(style, GTK_STYLE_CLASS_CELL);
}

FsearchListView *
fsearch_list_view_new(GtkAdjustment *hadjustment, GtkAdjustment *vadjustment) {
    return g_object_new(FSEARCH_TYPE_LIST_VIEW, "hadjustment", hadjustment, "vadjustment", vadjustment, NULL);
}

static void
fsearch_list_view_scrollable_init(GtkScrollableInterface *iface) {
    iface->get_border = fsearch_list_view_get_border;
}

static void
fsearch_list_view_column_free(FsearchListViewColumn *col) {
    if (!col) {
        return;
    }

    g_clear_pointer(&col->name, g_free);
    g_clear_object(&col->button);
    g_clear_pointer(&col, free);
}

void
fsearch_list_view_remove_column(FsearchListView *view, FsearchListViewColumn *col) {
    if (!view || !col) {
        return;
    }
    if (view != col->view) {
        return;
    }

    view->columns_reversed = g_list_remove(view->columns_reversed, col);
    view->columns = g_list_remove(view->columns, col);
    if (col->visible) {
        view->min_list_width -= col->width;
    }

    if (gtk_widget_get_realized(GTK_WIDGET(view))) {
        fsearch_list_view_unrealize_column(view, col);
        gtk_widget_queue_resize(GTK_WIDGET(view));
    }

    fsearch_list_view_column_unref(g_steal_pointer(&col));
}

void
fsearch_list_view_column_set_tooltip(FsearchListViewColumn *col, const char *tooltip) {
    if (!col) {
        return;
    }
    gtk_widget_set_tooltip_markup(col->button, tooltip);
}

void
fsearch_list_view_column_set_emblem(FsearchListViewColumn *col, const char *emblem_name, gboolean visible) {
    if (!col) {
        return;
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(col->emblem), emblem_name, GTK_ICON_SIZE_BUTTON);
    if (visible) {
        gtk_widget_show(col->emblem);
    }
    else {
        gtk_widget_hide(col->emblem);
    }
}

void
fsearch_list_view_column_set_visible(FsearchListView *view, FsearchListViewColumn *col, gboolean visible) {
    if (!view || !col) {
        return;
    }
    if (view != col->view) {
        return;
    }
    if (col->visible == visible) {
        return;
    }
    if (col->visible) {
        gtk_widget_hide(col->button);
        view->min_list_width -= col->width;
    }
    else {
        gtk_widget_show(col->button);
        view->min_list_width += col->width;
    }

    if (gtk_widget_get_realized(GTK_WIDGET(view))) {
        gdk_window_raise(col->window);
    }

    col->visible = visible;
    gtk_widget_queue_resize(GTK_WIDGET(view));
}

static void
fsearch_list_view_reset_sort_indicator(FsearchListView *view) {
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        gtk_widget_hide(column->arrow);
        gtk_widget_set_sensitive(column->button, TRUE);

        if (gtk_widget_get_realized(GTK_WIDGET(view))) {
            gdk_window_raise(column->window);
        }
    }
}

static void
fsearch_list_view_update_sort_indicator(FsearchListView *view) {
    FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(view, view->sort_order);
    if (!col) {
        return;
    }

    fsearch_list_view_reset_sort_indicator(view);

    gtk_image_set_from_icon_name(GTK_IMAGE(col->arrow),
                                 view->sort_type == GTK_SORT_DESCENDING ? "pan-up-symbolic" : "pan-down-symbolic",
                                 GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(col->arrow);
}

static void
on_fsearch_list_view_header_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchListViewColumn *col = user_data;
    GtkSortType current_sort_type = col->view->sort_type;
    int current_sort_order = col->view->sort_order;

    if (col->view->sort_func) {
        if (current_sort_order == col->type) {
            col->view->sort_func(col->type, !current_sort_type, col->view->sort_func_data);
        }
        else {
            col->view->sort_func(col->type, GTK_SORT_ASCENDING, col->view->sort_func_data);
        }
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(col->arrow), "content-loading-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(col->arrow);
    gtk_widget_set_sensitive(col->button, FALSE);
}

static gboolean
on_fsearch_list_view_header_button_pressed(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    if (gdk_event_triggers_context_menu(event)) {
        g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/menus.ui");
        GMenuModel *menu_model = G_MENU_MODEL(gtk_builder_get_object(builder, "fsearch_listview_column_popup_menu"));

        FsearchListViewColumn *col = user_data;
        GtkWidget *list = GTK_WIDGET(col->view);

        GtkWidget *menu_widget = gtk_menu_new_from_model(G_MENU_MODEL(menu_model));
        gtk_menu_attach_to_widget(GTK_MENU(menu_widget), list, NULL);
#if !GTK_CHECK_VERSION(3, 22, 0)
        guint button;
        guint32 time = gdk_event_get_time(event);
        gdk_event_get_button(event, &button);
        gtk_menu_popup(GTK_MENU(menu_widget), NULL, NULL, NULL, NULL, button, time);
#else
        gtk_menu_popup_at_pointer(GTK_MENU(menu_widget), NULL);
#endif
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

FsearchListViewColumn *
fsearch_list_view_get_first_column_for_type(FsearchListView *view, int type) {
    if (!view) {
        return NULL;
    }

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);
    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (column->type == type) {
            return column;
        }
    }
    return NULL;
}

void
fsearch_list_view_append_column(FsearchListView *view, FsearchListViewColumn *col) {
    if (!view || !col) {
        return;
    }
    col->view = view;

    view->columns = g_list_append(view->columns, col);
    view->columns_reversed = g_list_prepend(view->columns_reversed, col);
    if (col->visible) {
        view->min_list_width += col->width;
    }

    g_signal_connect(G_OBJECT(col->button), "clicked", G_CALLBACK(on_fsearch_list_view_header_button_clicked), col);
    g_signal_connect(G_OBJECT(col->button),
                     "button-press-event",
                     G_CALLBACK(on_fsearch_list_view_header_button_pressed),
                     col);

    gtk_widget_set_parent(col->button, GTK_WIDGET(view));
    gtk_widget_set_parent_window(col->button, view->header_window);
    if (gtk_widget_get_realized(GTK_WIDGET(view))) {
        fsearch_list_view_realize_column(view, col);
    }

    gtk_widget_queue_resize(col->button);
    gtk_widget_queue_resize(GTK_WIDGET(view));
}

void
fsearch_list_view_set_config(FsearchListView *view, uint32_t num_rows, int sort_order, GtkSortType sort_type) {
    if (!view) {
        return;
    }
    view->cursor_idx = UNSET_ROW;
    view->highlight_cursor_idx = FALSE;

    view->extend_started_idx = UNSET_ROW;
    view->num_rows = num_rows;
    view->list_height = num_rows * view->row_height;
    gtk_adjustment_set_value(view->vadjustment, 0);

    view->sort_order = sort_order;
    view->sort_type = sort_type;
    fsearch_list_view_update_sort_indicator(view);

    gtk_widget_queue_resize(GTK_WIDGET(view));
}

void
fsearch_list_view_set_query_tooltip_func(FsearchListView *view, FsearchListViewQueryTooltipFunc func, gpointer func_data) {
    if (!view) {
        return;
    }
    view->query_tooltip_func = func;
    view->query_tooltip_func_data = func_data;
}

void
fsearch_list_view_set_draw_row_func(FsearchListView *view,
                                    FsearchListViewDrawRowFunc draw_row_func,
                                    gpointer draw_row_func_data) {
    if (!view) {
        return;
    }
    view->draw_row_func = draw_row_func;
    view->draw_row_func_data = draw_row_func_data;
}

void
fsearch_list_view_set_sort_func(FsearchListView *view, FsearchListViewSortFunc sort_func, gpointer sort_func_data) {
    if (!view) {
        return;
    }
    view->sort_func = sort_func;
    view->sort_func_data = sort_func_data;
}

void
fsearch_list_view_set_selection_handlers(FsearchListView *view,
                                         FsearchListViewIsSelectedFunc is_selected_func,
                                         FsearchListViewSelectFunc select_func,
                                         FsearchListViewSelectToggleFunc select_toggle_func,
                                         FsearchListViewSelectRangeFunc select_range_func,
                                         FsearchListViewSelectRangeFunc toggle_range_func,
                                         FsearchListViewUnselectAllFunc unselect_func,
                                         FsearchListViewNumSelectedFunc num_selected_func,
                                         gpointer user_data) {
    if (!view) {
        return;
    }
    view->is_selected_func = is_selected_func;
    view->select_func = select_func;
    view->select_toggle_func = select_toggle_func;
    view->select_range_func = select_range_func;
    view->toggle_range_func = toggle_range_func;
    view->unselect_func = unselect_func;
    view->num_selected_func = num_selected_func;
    view->selection_user_data = user_data;

    if (is_selected_func && select_func && select_toggle_func && unselect_func) {
        view->has_selection_handlers = TRUE;
    }
    else {
        view->has_selection_handlers = FALSE;
    }
}

gint
fsearch_list_view_get_cursor(FsearchListView *view) {
    if (!view) {
        return 0;
    }
    return view->cursor_idx;
}

void
fsearch_list_view_set_cursor(FsearchListView *view, int row_idx) {
    if (!view) {
        return;
    }
    view->highlight_cursor_idx = TRUE;
    view->cursor_idx = CLAMP(row_idx, 0, get_last_row_idx(view));
    fsearch_list_view_selection_add(view, view->cursor_idx);
    fsearch_list_view_scroll_row_into_view(view, row_idx);
}

int
fsearch_list_view_get_sort_order(FsearchListView *view) {
    return view->sort_order;
}

GtkSortType
fsearch_list_view_get_sort_type(FsearchListView *view) {
    return view->sort_type;
}

void
fsearch_list_view_set_single_click_activate(FsearchListView *view, gboolean value) {
    if (!view) {
        return;
    }
    view->single_click_activate = value;
}

FsearchListViewColumn *
fsearch_list_view_column_ref(FsearchListViewColumn *col) {
    if (!col || g_atomic_int_get(&col->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&col->ref_count);
    return col;
}

void
fsearch_list_view_column_unref(FsearchListViewColumn *col) {
    if (!col || g_atomic_int_get(&col->ref_count) <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&col->ref_count)) {
        g_clear_pointer(&col, fsearch_list_view_column_free);
    }
}

FsearchListViewColumn *
fsearch_list_view_column_new(int type,
                             char *name,
                             PangoAlignment alignment,
                             PangoEllipsizeMode ellipsize_mode,
                             gboolean visible,
                             gboolean expand,
                             uint32_t width) {
    FsearchListViewColumn *col = calloc(1, sizeof(FsearchListViewColumn));
    g_assert(col);

    col->button = gtk_button_new();
    gtk_widget_show(col->button);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    col->emblem = gtk_image_new_from_icon_name("emblem-important", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_opacity(col->emblem, 0.3);
    col->arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);

    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), col->emblem, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), col->arrow, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(col->button), hbox);

    gtk_widget_show(hbox);
    gtk_widget_show(label);

    col->type = type;
    col->name = g_strdup(name ? name : "");
    col->alignment = alignment;
    col->ellipsize_mode = ellipsize_mode;
    col->width = width;
    col->expand = expand;
    col->visible = visible;
    col->ref_count = 1;

    return col;
}

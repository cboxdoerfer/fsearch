
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

    gint rubberband_start_idx;
    gint rubberband_end_idx;

    GHashTable *selection;
    uint32_t num_selected;

    gboolean single_click_activate;

    gint focused_idx;

    gint last_clicked_idx;
    gint extend_started_idx;

    gint num_rows;
    gint row_height;

    gint header_height;

    gint min_list_width;
    gint list_height;

    GtkSortType sort_type;
    FsearchListViewColumnType sort_order;

    FsearchListViewSortFunc sort_func;
    gpointer sort_func_data;

    FsearchListViewDrawRowFunc draw_row_func;
    gpointer draw_row_func_data;

    FsearchListViewQueryTooltipFunc query_tooltip_func;
    gpointer query_tooltip_func_data;

    FsearchListViewRowDataFunc row_data_func;
    gpointer row_data_func_data;
};

enum { FSEARCH_LIST_VIEW_SELECTION_CHANGED, FSEARCH_LIST_VIEW_ROW_ACTIVATED, FSEARCH_LIST_VIEW_POPUP, NUM_SIGNALS };

static guint signals[NUM_SIGNALS];

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
fsearch_list_view_is_row_in_view(FsearchListView *view, int row_idx) {
    if (row_idx < 0) {
        return FALSE;
    }

    int y_view_start = floor(gtk_adjustment_get_value(view->vadjustment));
    int y_view_end = y_view_start + gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    int y_row = row_idx * view->row_height;

    if (y_view_start <= y_row && y_row <= y_view_end - view->row_height) {
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
    int row_idx = floor((double)y_canvas / (double)view->row_height);

    if (row_idx >= view->num_rows) {
        row_idx = -1;
    }

    return row_idx;
}

static void
fsearch_list_view_convert_view_to_canvas_coords(FsearchListView *view,
                                                int x_view,
                                                int y_view,
                                                int *x_canvas,
                                                int *y_canvas) {

    cairo_rectangle_int_t canvas_rect;
    canvas_rect.x = gtk_adjustment_get_value(view->hadjustment);
    canvas_rect.y = gtk_adjustment_get_value(view->vadjustment);
    canvas_rect.width = gdk_window_get_width(view->bin_window);
    canvas_rect.height = gdk_window_get_height(view->bin_window);

    if (x_canvas) {
        *x_canvas = canvas_rect.x + x_view;
    }
    if (y_canvas) {
        *y_canvas = canvas_rect.y + y_view - view->header_height;
    }
}

static gint
fsearch_list_view_get_font_height(FsearchListView *view) {
    GtkWidget *widget = GTK_WIDGET(view);

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);
    if (!layout) {
        return TEXT_HEIGHT_FALLBACK;
    }

    gint text_height;
    pango_layout_get_pixel_size(layout, NULL, &text_height);
    g_object_unref(layout);

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

static gboolean
fsearch_list_view_is_selected_for_idx(FsearchListView *view, gint row_idx) {
    if (!view->row_data_func) {
        return FALSE;
    }
    void *data = view->row_data_func(row_idx, view->sort_type, view->row_data_func_data);
    return fsearch_list_view_is_selected(view, data);
}

static void
fsearch_list_view_get_rubberband_points(FsearchListView *view, double *x1, double *y1, double *x2, double *y2) {
    gdouble x_drag_start = 0;
    gdouble y_drag_start = 0;
    gtk_gesture_drag_get_start_point(GTK_GESTURE_DRAG(view->bin_drag_gesture), &x_drag_start, &y_drag_start);

    const gdouble x_drag_start_diff =
        view->x_bin_drag_started - x_drag_start - gtk_adjustment_get_value(view->hadjustment);
    const gdouble y_drag_start_diff =
        view->y_bin_drag_started - y_drag_start - gtk_adjustment_get_value(view->vadjustment) + view->header_height;

    *x1 = view->x_bin_drag_started;
    *y1 = view->y_bin_drag_started;

    *x2 = view->x_bin_drag_started + view->x_bin_drag_offset - x_drag_start_diff;
    *y2 = view->y_bin_drag_started + view->y_bin_drag_offset - y_drag_start_diff;
}

static gboolean
fsearch_list_view_draw(GtkWidget *widget, cairo_t *cr) {
    FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    if (gtk_cairo_should_draw_window(cr, gtk_widget_get_window(widget))) {
        gtk_render_background(context, cr, 0, 0, width, height);
    }

    if (!gtk_cairo_should_draw_window(cr, view->bin_window)) {
        return FALSE;
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
    cairo_rectangle_int_t canvas_rect;
    canvas_rect.x = -gtk_adjustment_get_value(view->hadjustment);
    canvas_rect.y = -gtk_adjustment_get_value(view->vadjustment);
    canvas_rect.width = gdk_window_get_width(view->bin_window);
    canvas_rect.height = gdk_window_get_height(view->bin_window);

    const int x_rtl_offset = canvas_rect.width - columns_width;
    if (fsearch_list_view_is_text_dir_rtl(view)) {
        canvas_rect.x += x_rtl_offset;
    }

    const double y_offset = canvas_rect.y % view->row_height + view->header_height;
    const int first_visible_row = floor(-canvas_rect.y / (double)view->row_height);
    const int num_rows_in_view = view_rect.height / view->row_height + 1;

    cairo_save(cr);
    gdk_cairo_rectangle(cr, &view_rect);
    cairo_clip(cr);

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);

    for (int i = 0; i < num_rows_in_view; i++) {
        if (first_visible_row + i >= view->num_rows) {
            break;
        }
        cairo_rectangle_int_t row_rect;

        row_rect.x = canvas_rect.x;
        row_rect.y = y_offset + i * view->row_height;
        row_rect.width = MIN(columns_width, canvas_rect.width);
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
                                view->sort_type,
                                row_idx,
                                fsearch_list_view_is_selected_for_idx(view, row_idx),
                                view->last_clicked_idx == row_idx ? TRUE : FALSE,
                                fsearch_list_view_is_text_dir_rtl(view),
                                view->draw_row_func_data);
            cairo_restore(cr);
        }
    }

    if (gtk_widget_has_focus(widget) && first_visible_row <= view->focused_idx
        && view->focused_idx <= first_visible_row + num_rows_in_view) {
        GtkStateFlags flags = gtk_style_context_get_state(context);
        flags |= GTK_STATE_FLAG_FOCUSED;

        gtk_style_context_save(context);
        gtk_style_context_set_state(context, flags);
        gtk_render_focus(context,
                         cr,
                         canvas_rect.x,
                         y_offset + (view->focused_idx - first_visible_row) * view->row_height,
                         columns_width,
                         view->row_height);
        gtk_style_context_restore(context);
    }

    if (view->num_rows > 0) {
        gtk_style_context_save(context);
        gtk_style_context_add_class(context, GTK_STYLE_CLASS_SEPARATOR);

        uint32_t line_x = canvas_rect.x;
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

        const gdouble x_drag_start_diff =
            view->x_bin_drag_started - x_drag_start - gtk_adjustment_get_value(view->hadjustment);
        const gdouble y_drag_start_diff =
            view->y_bin_drag_started - y_drag_start - gtk_adjustment_get_value(view->vadjustment) + view->header_height;

        const double x1 = canvas_rect.x + view_rect.x + view->x_bin_drag_started;
        const double y1 = canvas_rect.y + view_rect.y + view->y_bin_drag_started;

        const double x2 =
            canvas_rect.x + view_rect.x + view->x_bin_drag_started + view->x_bin_drag_offset - x_drag_start_diff;
        const double y2 =
            canvas_rect.y + view_rect.y + view->y_bin_drag_started + view->y_bin_drag_offset - y_drag_start_diff;

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

    if (gtk_cairo_should_draw_window(cr, view->header_window)) {
        gtk_style_context_save(context);
        gtk_style_context_remove_class(context, GTK_STYLE_CLASS_CELL);
        for (GList *col = columns; col != NULL; col = col->next) {
            FsearchListViewColumn *column = col->data;
            if (!column->visible) {
                continue;
            }
            gtk_container_propagate_draw(GTK_CONTAINER(view), column->button, cr);
        }
        gtk_style_context_restore(context);
    }

    g_object_unref(layout);

    return FALSE;
}

static void
fsearch_list_view_scroll_row_into_view(FsearchListView *view, int row_idx) {
    row_idx = CLAMP(row_idx, 0, view->num_rows - 1);

    if (fsearch_list_view_is_row_in_view(view, row_idx)) {
        gtk_widget_queue_draw(GTK_WIDGET(view));
        return;
    }

    int view_height = gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    int y_row = view->row_height * row_idx;
    int y_view_start = floor(gtk_adjustment_get_value(view->vadjustment)) + view->header_height;

    if (y_view_start >= y_row) {
        gtk_adjustment_set_value(view->vadjustment, y_row);
    }
    else {
        gtk_adjustment_set_value(view->vadjustment, y_row - view_height + view->row_height);
    }
}

static void
fsearch_list_view_selection_changed(FsearchListView *view) {
    gtk_widget_queue_draw(GTK_WIDGET(view));
    g_signal_emit(view, signals[FSEARCH_LIST_VIEW_SELECTION_CHANGED], 0);
}

static void
fsearch_list_view_selection_invert_silent(FsearchListView *view) {
    GHashTable *new_selection = g_hash_table_new(g_direct_hash, g_direct_equal);

    int num_selected = 0;

    for (int i = 0; i < view->num_rows; i++) {
        void *data = view->row_data_func(i, view->sort_type, view->row_data_func_data);
        if (!data) {
            continue;
        }
        if (g_hash_table_contains(view->selection, data)) {
            continue;
        }
        g_hash_table_add(new_selection, data);
        num_selected++;
    }
    g_hash_table_destroy(view->selection);
    view->selection = new_selection;
    view->num_selected = num_selected;
}

static void
fsearch_list_view_select_all_silent(FsearchListView *view) {
    if (!view->row_data_func) {
        return;
    }
    for (int i = 0; i < view->num_rows; i++) {
        void *data = view->row_data_func(i, view->sort_type, view->row_data_func_data);
        if (data) {
            g_hash_table_add(view->selection, data);
            view->num_selected++;
        }
    }
}

static void
fsearch_list_view_selection_clear_silent(FsearchListView *view) {
    g_hash_table_steal_all(view->selection);
    view->num_selected = 0;
}

static void
fsearch_list_view_selection_add_silent(FsearchListView *view, void *data) {
    if (g_hash_table_contains(view->selection, data)) {
        return;
    }
    g_hash_table_add(view->selection, data);
    view->num_selected++;
}

static void
fsearch_list_view_selection_add(FsearchListView *view, void *data) {
    fsearch_list_view_selection_add_silent(view, data);
    fsearch_list_view_selection_changed(view);
}

static void
fsearch_list_view_selection_toggle_silent(FsearchListView *view, void *data) {
    if (g_hash_table_steal(view->selection, data)) {
        view->num_selected--;
        return;
    }
    else {
        g_hash_table_add(view->selection, data);
        view->num_selected++;
    }
}

static void
fsearch_list_view_select_range_silent(FsearchListView *view, guint start_idx, guint end_idx) {
    if (!view->row_data_func) {
        return;
    }
    if (start_idx < 0 || end_idx < 0) {
        return;
    }

    int temp_idx = start_idx;

    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = temp_idx;
    }

    end_idx = MIN(view->num_rows - 1, end_idx);

    for (int i = start_idx; i <= end_idx; i++) {
        void *data = view->row_data_func(i, view->sort_type, view->row_data_func_data);
        if (data) {
            fsearch_list_view_selection_add_silent(view, data);
        }
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
fsearch_list_view_multi_press_gesture_pressed(GtkGestureMultiPress *gesture,
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

    if (!view->row_data_func) {
        return;
    }
    if (view->rubberband_state == RUBBERBAND_SELECT_ACTIVE) {
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

    int row_idx = fsearch_list_view_get_row_idx_for_y_view(view, y);
    if (row_idx < 0) {
        fsearch_list_view_selection_clear(view);
        return;
    }

    void *row_data = view->row_data_func(row_idx, view->sort_type, view->row_data_func_data);
    if (!row_data) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    if (button_pressed == GDK_BUTTON_PRIMARY) {
        gboolean modify_selection;
        gboolean extend_selection;

        fsearch_list_view_get_selection_modifiers(view, &modify_selection, &extend_selection);

        if (n_press == 1) {
            if (extend_selection) {
                if (view->last_clicked_idx < 0) {
                    view->last_clicked_idx = row_idx;
                }
                fsearch_list_view_selection_clear_silent(view);
                fsearch_list_view_select_range_silent(view, view->last_clicked_idx, row_idx);
            }
            else if (modify_selection) {
                view->last_clicked_idx = row_idx;
                fsearch_list_view_selection_toggle_silent(view, row_data);
            }
            else {
                view->last_clicked_idx = row_idx;
                fsearch_list_view_selection_clear_silent(view);
                fsearch_list_view_selection_toggle_silent(view, row_data);
                if (view->single_click_activate) {
                    FsearchListViewColumn *col = fsearch_list_view_get_col_for_x_view(view, x);
                    if (col) {
                        g_signal_emit(view,
                                      signals[FSEARCH_LIST_VIEW_ROW_ACTIVATED],
                                      0,
                                      col->type,
                                      row_idx,
                                      view->sort_type);
                    }
                }
            }
            fsearch_list_view_selection_changed(view);
        }

        if (n_press == 2 && !view->single_click_activate) {
            FsearchListViewColumn *col = fsearch_list_view_get_col_for_x_view(view, x);
            if (col) {
                g_signal_emit(view, signals[FSEARCH_LIST_VIEW_ROW_ACTIVATED], 0, col->type, row_idx, view->sort_type);
            }
        }
    }

    if (button_pressed == GDK_BUTTON_SECONDARY && n_press == 1) {
        view->last_clicked_idx = row_idx;
        if (!fsearch_list_view_is_selected(view, row_data)) {
            fsearch_list_view_selection_clear_silent(view);
            fsearch_list_view_selection_toggle_silent(view, row_data);
            fsearch_list_view_selection_changed(view);
        }
        g_signal_emit(view, signals[FSEARCH_LIST_VIEW_POPUP], 0, row_idx, view->sort_type);
    }

    view->focused_idx = -1;
    gtk_widget_queue_draw(GTK_WIDGET(view));

    if (view->extend_started_idx >= 0) {
        view->extend_started_idx = -1;
    }
}

static void
fsearch_list_view_multi_press_gesture_released(GtkGestureMultiPress *gesture,
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
fsearch_list_view_bin_drag_gesture_end(GtkGestureDrag *gesture,
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
        view->rubberband_start_idx = -1;
        view->rubberband_end_idx = -1;
        gtk_widget_queue_draw(GTK_WIDGET(view));
    }
}

static void
fsearch_list_view_bin_drag_gesture_update(GtkGestureDrag *gesture,
                                          gdouble offset_x,
                                          gdouble offset_y,
                                          FsearchListView *view) {
    GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));

    // if (gtk_gesture_get_sequence_state(GTK_GESTURE(gesture), sequence) != GTK_EVENT_SEQUENCE_CLAIMED) {
    //     return;
    // }

    view->rubberband_state = RUBBERBAND_SELECT_ACTIVE;
    view->x_bin_drag_offset = offset_x;
    view->y_bin_drag_offset = offset_y;

    double x1, y1, x2, y2;
    fsearch_list_view_get_rubberband_points(view, &x1, &y1, &x2, &y2);
    int row_idx_1 = MAX(0, fsearch_list_view_get_row_idx_for_y_canvas(view, y1));
    int row_idx_2 = MAX(0, fsearch_list_view_get_row_idx_for_y_canvas(view, y2));

    if (row_idx_1 > row_idx_2) {
        int tmp_idx = row_idx_1;
        row_idx_1 = row_idx_2;
        row_idx_2 = tmp_idx;
    }

    if (row_idx_1 != view->rubberband_start_idx || row_idx_2 != view->rubberband_end_idx) {
        view->rubberband_start_idx = row_idx_1;
        view->rubberband_end_idx = row_idx_2;
        fsearch_list_view_selection_clear_silent(view);
        fsearch_list_view_select_range_silent(view, row_idx_1, row_idx_2);
        fsearch_list_view_selection_changed(view);
        return;
    }
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

static void
fsearch_list_view_bin_drag_gesture_begin(GtkGestureDrag *gesture,
                                         gdouble start_x,
                                         gdouble start_y,
                                         FsearchListView *view) {
    if (start_y > view->header_height) {
        if (!gtk_widget_has_focus(GTK_WIDGET(view))) {
            gtk_widget_grab_focus(GTK_WIDGET(view));
        }

        view->x_bin_drag_started = start_x + gtk_adjustment_get_value(view->hadjustment);
        view->y_bin_drag_started = start_y + gtk_adjustment_get_value(view->vadjustment) - view->header_height;
        view->bin_drag_mode = TRUE;
        view->rubberband_state = RUBBERBAND_SELECT_WAITING;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

static void
fsearch_list_view_header_drag_gesture_end(GtkGestureDrag *gesture,
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
fsearch_list_view_header_drag_gesture_update(GtkGestureDrag *gesture,
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
fsearch_list_view_header_drag_gesture_begin(GtkGestureDrag *gesture,
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
        d_idx = -view->focused_idx;
        break;
    case GDK_KEY_End:
        d_idx = view->num_rows - view->focused_idx - 1;
        break;
    default:
        return FALSE;
    }

    if (d_idx != 0) {
        int old_focused_idx = view->focused_idx;
        if (view->focused_idx >= 0) {
            old_focused_idx = view->focused_idx;
        }
        else if (view->last_clicked_idx >= 0) {
            old_focused_idx = view->last_clicked_idx;
        }
        else {
            old_focused_idx = 0;
        }
        view->last_clicked_idx = -1;
        view->focused_idx = CLAMP(old_focused_idx + d_idx, 0, view->num_rows - 1);

        void *row_data = view->row_data_func(view->focused_idx, view->sort_type, view->row_data_func_data);

        if (extend_selection) {
            if (view->extend_started_idx < 0) {
                view->extend_started_idx = old_focused_idx;
            }
            fsearch_list_view_selection_clear_silent(view);
            fsearch_list_view_select_range_silent(view, view->extend_started_idx, view->focused_idx);
        }
        else if (!modify_selection) {
            view->extend_started_idx = -1;
            fsearch_list_view_selection_clear_silent(view);
            fsearch_list_view_selection_toggle_silent(view, row_data);
        }

        fsearch_list_view_selection_changed(view);
        fsearch_list_view_scroll_row_into_view(view, view->focused_idx);
        return TRUE;
    }
    return FALSE;
}

static gint
fsearch_list_view_focus_out_event(GtkWidget *widget, GdkEventFocus *event) {
    // FsearchListView *view = FSEARCH_LIST_VIEW(widget);
    // view->focused_idx = -1;
    gtk_widget_queue_draw(widget);
    return TRUE;
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

    gboolean ret_val = FALSE;
    char *tooltip_text = view->query_tooltip_func(layout,
                                                  view->sort_type,
                                                  view->row_height,
                                                  row_idx,
                                                  col,
                                                  view->query_tooltip_func_data);
    if (tooltip_text) {
        gtk_tooltip_set_text(tooltip, tooltip_text);
        g_free(tooltip_text);
        tooltip_text = NULL;
        ret_val = TRUE;
    }
    g_object_unref(layout);
    return ret_val;
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
fsearch_list_view_adjustment_changed(GtkAdjustment *adjustment, FsearchListView *view) {
    if (gtk_widget_get_realized(GTK_WIDGET(view))) {
        gdk_window_move(view->bin_window,
                        -gtk_adjustment_get_value(view->hadjustment),
                        -gtk_adjustment_get_value(view->vadjustment) + view->header_height);
        gdk_window_move(view->header_window, -gtk_adjustment_get_value(view->hadjustment), 0);
    }
}

static void
fsearch_list_view_set_adjustment_value(GtkAdjustment *adjustment, double allocated_size, double size) {
    gdouble old_value = gtk_adjustment_get_value(adjustment);
    gdouble new_upper = MAX(allocated_size, size);

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

    gdouble new_value = CLAMP(old_value, 0, new_upper - allocated_size);
    if (new_value != old_value) {
        gtk_adjustment_set_value(adjustment, new_value);
    }
}

static void
fsearch_list_view_set_hadjustment_value(FsearchListView *view) {
    gint width = gtk_widget_get_allocated_width(GTK_WIDGET(view));
    fsearch_list_view_set_adjustment_value(view->hadjustment, width, view->min_list_width);
}

static void
fsearch_list_view_set_vadjustment_value(FsearchListView *view) {
    gint height = gtk_widget_get_allocated_height(GTK_WIDGET(view)) - view->header_height;
    fsearch_list_view_set_adjustment_value(view->vadjustment, height, view->list_height);
}

static void
fsearch_list_view_set_hadjustment(FsearchListView *view, GtkAdjustment *adjustment) {
    if (adjustment && view->hadjustment == adjustment) {
        return;
    }

    if (view->hadjustment != NULL) {
        g_signal_handlers_disconnect_by_func(view->hadjustment, fsearch_list_view_adjustment_changed, view);
        g_object_unref(view->hadjustment);
    }

    if (adjustment == NULL) {
        adjustment = gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }

    g_signal_connect(adjustment, "value-changed", G_CALLBACK(fsearch_list_view_adjustment_changed), view);
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
        g_signal_handlers_disconnect_by_func(view->vadjustment, fsearch_list_view_adjustment_changed, view);
        g_object_unref(view->vadjustment);
    }

    if (adjustment == NULL) {
        adjustment = gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }

    g_signal_connect(adjustment, "value-changed", G_CALLBACK(fsearch_list_view_adjustment_changed), view);
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

    GList *columns = fsearch_list_view_get_columns_for_text_direction(view);
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
        int font_height = fsearch_list_view_get_font_height(view);
        view->row_height = font_height + 2 * ROW_PADDING_Y;
        view->list_height = view->row_height * view->num_rows;
        gdk_window_move_resize(gtk_widget_get_window(widget),
                               allocation->x,
                               allocation->y,
                               allocation->width,
                               allocation->height);
        gdk_window_move_resize(view->bin_window,
                               -gtk_adjustment_get_value(view->hadjustment),
                               view->header_height,
                               MAX(view->min_list_width, allocation->width),
                               MAX(view->list_height, allocation->height - view->header_height));
        gdk_window_move_resize(view->header_window,
                               -gtk_adjustment_get_value(view->hadjustment),
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

static void
fsearch_list_view_unrealize_column(FsearchListView *view, FsearchListViewColumn *column) {
    gtk_widget_unregister_window(GTK_WIDGET(view), column->window);
    gdk_window_destroy(column->window);
    column->window = NULL;
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
    gdk_window_destroy(view->bin_window);
    view->bin_window = NULL;

    gtk_widget_unregister_window(widget, view->header_window);
    gdk_window_destroy(view->header_window);
    view->header_window = NULL;

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

    g_object_unref(attrs.cursor);
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
    attrs.event_mask =
        (GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK
         | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | gtk_widget_get_events(widget));

    view->bin_window = gdk_window_new(window, &attrs, attrs_mask);
    gtk_widget_register_window(widget, view->bin_window);

    attrs.x = 0;
    attrs.y = 0;
    attrs.width = MAX(view->min_list_width, allocation.width);
    attrs.height = view->header_height;
    attrs.event_mask =
        (GDK_EXPOSURE_MASK | GDK_SCROLL_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK
         | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | gtk_widget_get_events(widget));

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

    if (view->multi_press_gesture) {
        g_object_unref(view->multi_press_gesture);
        view->multi_press_gesture = NULL;
    }
    if (view->bin_drag_gesture) {
        g_object_unref(view->bin_drag_gesture);
        view->bin_drag_gesture = NULL;
    }
    if (view->header_drag_gesture) {
        g_object_unref(view->header_drag_gesture);
        view->header_drag_gesture = NULL;
    }
    if (view->selection) {
        g_hash_table_destroy(view->selection);
        view->selection = NULL;
    }
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

    container_class->forall = fsearch_list_view_container_for_all;
    container_class->remove = fsearch_list_view_container_remove;

    signals[FSEARCH_LIST_VIEW_SELECTION_CHANGED] = g_signal_new("selection-changed",
                                                                G_TYPE_FROM_CLASS(klass),
                                                                G_SIGNAL_RUN_LAST,
                                                                0,
                                                                NULL,
                                                                NULL,
                                                                NULL,
                                                                G_TYPE_NONE,
                                                                0);

    signals[FSEARCH_LIST_VIEW_POPUP] = g_signal_new("row-popup",
                                                    G_TYPE_FROM_CLASS(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    2,
                                                    G_TYPE_INT,
                                                    GTK_TYPE_SORT_TYPE);

    signals[FSEARCH_LIST_VIEW_ROW_ACTIVATED] = g_signal_new("row-activated",
                                                            G_TYPE_FROM_CLASS(klass),
                                                            G_SIGNAL_RUN_LAST,
                                                            0,
                                                            NULL,
                                                            NULL,
                                                            NULL,
                                                            G_TYPE_NONE,
                                                            3,
                                                            G_TYPE_INT,
                                                            G_TYPE_INT,
                                                            GTK_TYPE_SORT_TYPE);

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

    view->focused_idx = -1;
    view->last_clicked_idx = -1;
    view->extend_started_idx = -1;

    view->min_list_width = 0;
    view->list_height = view->num_rows * view->row_height;

    view->selection = g_hash_table_new(g_direct_hash, g_direct_equal);

    gtk_widget_set_sensitive(GTK_WIDGET(view), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(view), TRUE);

    view->multi_press_gesture = gtk_gesture_multi_press_new(GTK_WIDGET(view));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(view->multi_press_gesture), 0);
    g_signal_connect(view->multi_press_gesture,
                     "pressed",
                     G_CALLBACK(fsearch_list_view_multi_press_gesture_pressed),
                     view);
    g_signal_connect(view->multi_press_gesture,
                     "released",
                     G_CALLBACK(fsearch_list_view_multi_press_gesture_released),
                     view);

    view->bin_drag_gesture = gtk_gesture_drag_new(GTK_WIDGET(view));
    g_signal_connect(view->bin_drag_gesture, "drag-begin", G_CALLBACK(fsearch_list_view_bin_drag_gesture_begin), view);
    g_signal_connect(view->bin_drag_gesture,
                     "drag-update",
                     G_CALLBACK(fsearch_list_view_bin_drag_gesture_update),
                     view);
    g_signal_connect(view->bin_drag_gesture, "drag-end", G_CALLBACK(fsearch_list_view_bin_drag_gesture_end), view);

    view->header_drag_gesture = gtk_gesture_drag_new(GTK_WIDGET(view));
    g_signal_connect(view->header_drag_gesture,
                     "drag-begin",
                     G_CALLBACK(fsearch_list_view_header_drag_gesture_begin),
                     view);
    g_signal_connect(view->header_drag_gesture,
                     "drag-update",
                     G_CALLBACK(fsearch_list_view_header_drag_gesture_update),
                     view);
    g_signal_connect(view->header_drag_gesture,
                     "drag-end",
                     G_CALLBACK(fsearch_list_view_header_drag_gesture_end),
                     view);
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

    if (col->button) {
        g_object_unref(col->button);
    }

    if (col->name) {
        free(col->name);
        col->name = NULL;
    }

    free(col);
    col = NULL;
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
    col->visible = visible;
    gtk_widget_queue_resize(GTK_WIDGET(view));
}

static void
fsearch_list_view_reset_sort_indicator(FsearchListView *view) {
    for (GList *col = view->columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        gtk_widget_hide(column->arrow);
    }
}

static void
on_fsearch_list_view_header_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchListViewColumn *col = user_data;
    GtkSortType current_sort_type = col->view->sort_type;
    FsearchListViewColumnType current_sort_order = col->view->sort_order;

    fsearch_list_view_reset_sort_indicator(col->view);

    if (current_sort_order == col->type) {
        if (current_sort_type == GTK_SORT_ASCENDING) {
            gtk_image_set_from_icon_name(GTK_IMAGE(col->arrow), "pan-up-symbolic", GTK_ICON_SIZE_BUTTON);
        }
        else if (current_sort_type == GTK_SORT_DESCENDING) {
            gtk_image_set_from_icon_name(GTK_IMAGE(col->arrow), "pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
        }
        fsearch_list_view_set_sort_type(col->view, !current_sort_type);
        gtk_widget_show(col->arrow);
    }
    else {
        if (col->view->sort_func) {
            col->view->sort_func(col->type, col->view->sort_func_data);
            col->view->sort_order = col->type;
            gtk_widget_show(col->arrow);
            fsearch_list_view_set_sort_type(col->view, GTK_SORT_ASCENDING);
            gtk_image_set_from_icon_name(GTK_IMAGE(col->arrow), "pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
        }
    }
}

static gboolean
on_fsearch_list_view_header_button_pressed(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    if (gdk_event_triggers_context_menu(event)) {
        GtkBuilder *builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/menus.ui");
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
        g_object_unref(builder);
        return TRUE;
    }
    return FALSE;
}

FsearchListViewColumn *
fsearch_list_view_get_first_column_for_type(FsearchListView *view, FsearchListViewColumnType type) {
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
fsearch_list_view_set_num_rows(FsearchListView *view,
                               uint32_t num_rows,
                               FsearchListViewColumnType sort_order,
                               GtkSortType sort_type) {
    if (!view) {
        return;
    }
    view->focused_idx = -1;
    view->last_clicked_idx = -1;
    view->extend_started_idx = -1;
    view->num_rows = num_rows;
    view->list_height = num_rows * view->row_height;
    fsearch_list_view_selection_clear(view);
    fsearch_list_view_reset_sort_indicator(view);

    gtk_adjustment_set_value(view->vadjustment, 0);

    view->sort_order = sort_order;
    view->sort_type = sort_type;

    gtk_widget_queue_resize(GTK_WIDGET(view));
}

void
fsearch_list_view_set_query_tooltip_func(FsearchListView *view,
                                         FsearchListViewQueryTooltipFunc func,
                                         gpointer func_data) {
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
fsearch_list_view_set_row_data_func(FsearchListView *view,
                                    FsearchListViewRowDataFunc row_data_func,
                                    gpointer row_data_func_data) {
    if (!view) {
        return;
    }
    view->row_data_func = row_data_func;
    view->row_data_func_data = row_data_func_data;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void
fsearch_list_view_set_sort_func(FsearchListView *view, FsearchListViewSortFunc sort_func, gpointer sort_func_data) {
    if (!view) {
        return;
    }
    view->sort_func = sort_func;
    view->sort_func_data = sort_func_data;
}

gboolean
fsearch_list_view_is_selected(FsearchListView *view, void *data) {
    return g_hash_table_contains(view->selection, data);
}

uint32_t
fsearch_list_view_get_num_selected(FsearchListView *view) {
    return view->num_selected;
}

void
fsearch_list_view_selection_for_each(FsearchListView *view, GHFunc func, gpointer user_data) {
    if (!view) {
        return;
    }
    g_hash_table_foreach(view->selection, func, user_data);
}

void
fsearch_list_view_selection_invert(FsearchListView *view) {

    fsearch_list_view_selection_invert_silent(view);
    fsearch_list_view_selection_changed(view);
}

void
fsearch_list_view_select_range(FsearchListView *view, int start_idx, int end_idx) {
    fsearch_list_view_select_range_silent(view, start_idx, end_idx);
    fsearch_list_view_selection_changed(view);
}

void
fsearch_list_view_select_all(FsearchListView *view) {
    fsearch_list_view_select_all_silent(view);
    fsearch_list_view_selection_changed(view);
}

void
fsearch_list_view_selection_clear(FsearchListView *view) {
    if (!view || !view->selection) {
        return;
    }
    fsearch_list_view_selection_clear_silent(view);
    fsearch_list_view_selection_changed(view);
}

gint
fsearch_list_view_get_cursor(FsearchListView *view) {
    if (!view) {
        return 0;
    }
    return view->focused_idx;
}

void
fsearch_list_view_set_cursor(FsearchListView *view, int row_idx) {
    if (!view) {
        return;
    }
    view->focused_idx = CLAMP(row_idx, 0, view->num_rows);
    if (view->row_data_func) {
        void *data = view->row_data_func(view->focused_idx, view->sort_type, view->row_data_func_data);
        if (data) {
            fsearch_list_view_selection_add(view, data);
        }
    }
    fsearch_list_view_scroll_row_into_view(view, row_idx);
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

void
fsearch_list_view_set_sort_order(FsearchListView *view, FsearchListViewColumnType sort_order) {
    if (!view) {
        return;
    }
    view->sort_order = sort_order;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

FsearchListViewColumnType
fsearch_list_view_get_sort_order(FsearchListView *view) {
    return view->sort_order;
}

void
fsearch_list_view_set_sort_type(FsearchListView *view, GtkSortType sort_type) {
    if (!view) {
        return;
    }
    view->sort_type = sort_type;
    gtk_widget_queue_draw(GTK_WIDGET(view));
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
fsearch_list_view_column_new(FsearchListViewColumnType type,
                             char *name,
                             PangoAlignment alignment,
                             PangoEllipsizeMode ellipsize_mode,
                             gboolean visible,
                             gboolean expand,
                             uint32_t width) {
    FsearchListViewColumn *col = calloc(1, sizeof(FsearchListViewColumn));
    g_assert(col != NULL);

    col->button = gtk_button_new();
    gtk_widget_show(col->button);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    col->arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);

    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), col->arrow, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(col->button), hbox);

    gtk_widget_show(hbox);
    gtk_widget_show(label);

    col->type = type;
    col->name = name ? g_strdup(name) : NULL;
    col->alignment = alignment;
    col->ellipsize_mode = ellipsize_mode;
    col->width = width;
    col->expand = expand;
    col->visible = visible;

    return col;
}

#define G_LOG_DOMAIN "fsearch-result-view"

#include "fsearch_result_view.h"

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_file_utils.h"
#include "fsearch_query.h"

#include <assert.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>

static int32_t
get_icon_size_for_height(int32_t height) {
    if (height < 24) {
        return 16;
    }
    if (height < 32) {
        return 24;
    }
    if (height < 48) {
        return 32;
    }
    return 48;
}

static cairo_surface_t *
get_icon_surface(GdkWindow *win,
                 const char *name,
                 FsearchDatabaseEntryType type,
                 int32_t icon_size,
                 int32_t scale_factor) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    if (!icon_theme) {
        return NULL;
    }

    cairo_surface_t *icon_surface = NULL;
    GIcon *icon = fsearch_file_utils_guess_icon(name, type == DATABASE_ENTRY_TYPE_FOLDER);
    const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
    if (!names) {
        g_object_unref(icon);
        return NULL;
    }

    GtkIconInfo *icon_info = gtk_icon_theme_choose_icon_for_scale(icon_theme,
                                                                  (const char **)names,
                                                                  icon_size,
                                                                  scale_factor,
                                                                  GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!icon_info) {
        return NULL;
    }

    GdkPixbuf *pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
    if (pixbuf) {
        icon_surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, win);
        g_object_unref(pixbuf);
    }
    g_object_unref(icon);
    g_object_unref(icon_info);

    return icon_surface;
}

typedef struct {
    char *display_name;
    PangoAttrList *name_attr;
    PangoAttrList *path_attr;

    cairo_surface_t *icon_surface;

    GString *path;
    GString *full_path;
    char *size;
    char *type;
    char time[100];
} DrawRowContext;

static void
draw_row_ctx_init(FsearchDatabaseView *view,
                  uint32_t row,
                  GdkWindow *bin_window,
                  int32_t icon_size,
                  DrawRowContext *ctx) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    db_view_lock(view);
    GString *name = db_view_entry_get_name_for_idx(view, row);
    if (!name) {
        goto out;
    }
    ctx->display_name = g_filename_display_name(name->str);

    ctx->path = db_view_entry_get_path_for_idx(view, row);

    FsearchQuery *query = db_view_get_query(view);
    if (query) {
        ctx->name_attr = fsearch_query_highlight_match(query, name->str);
        if ((query->has_separator && query->flags.auto_search_in_path) || query->flags.search_in_path) {
            ctx->path_attr = fsearch_query_highlight_match(query, ctx->path->str);
        }
        fsearch_query_unref(query);
        query = NULL;
    }

    ctx->full_path = db_view_entry_get_path_full_for_idx(view, row);

    FsearchDatabaseEntryType type = db_view_entry_get_type_for_idx(view, row);
    ctx->type = fsearch_file_utils_get_file_type(name->str, type == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);

    ctx->icon_surface =
        config->show_listview_icons
            ? get_icon_surface(bin_window, name->str, type, icon_size, gdk_window_get_scale_factor(bin_window))
            : NULL;

    off_t size = db_view_entry_get_size_for_idx(view, row);
    ctx->size = fsearch_file_utils_get_size_formatted(size, config->show_base_2_units);

    const time_t mtime = db_view_entry_get_mtime_for_idx(view, row);
    strftime(ctx->time,
             100,
             "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime(&mtime));

out:
    if (name) {
        g_string_free(name, TRUE);
        name = NULL;
    }
    db_view_unlock(view);
}

static void
draw_row_ctx_free(DrawRowContext *ctx) {
    if (ctx->display_name) {
        g_free(ctx->display_name);
        ctx->display_name = NULL;
    }
    if (ctx->type) {
        g_free(ctx->type);
        ctx->type = NULL;
    }
    if (ctx->size) {
        g_free(ctx->size);
        ctx->size = NULL;
    }
    if (ctx->path_attr) {
        pango_attr_list_unref(ctx->path_attr);
        ctx->path_attr = NULL;
    }
    if (ctx->name_attr) {
        pango_attr_list_unref(ctx->name_attr);
        ctx->name_attr = NULL;
    }
    if (ctx->path) {
        g_string_free(ctx->path, TRUE);
        ctx->path = NULL;
    }
    if (ctx->full_path) {
        g_string_free(ctx->full_path, TRUE);
        ctx->full_path = NULL;
    }
    if (ctx->icon_surface) {
        cairo_surface_destroy(ctx->icon_surface);
        ctx->icon_surface = NULL;
    }
}

char *
fsearch_result_view_query_tooltip(FsearchDatabaseView *view,
                                  uint32_t row,
                                  FsearchListViewColumn *col,
                                  PangoLayout *layout,
                                  uint32_t row_height) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    db_view_lock(view);
    GString *name = db_view_entry_get_name_for_idx(view, row);
    if (!name) {
        db_view_unlock(view);
        return NULL;
    }

    int32_t width = col->effective_width - 2 * ROW_PADDING_X;
    char *text = NULL;

    switch (col->type) {
    case DATABASE_INDEX_TYPE_NAME:
        if (config->show_listview_icons) {
            int32_t icon_size = get_icon_size_for_height((int32_t)row_height - ROW_PADDING_X);
            width -= 2 * ROW_PADDING_X + icon_size;
        }
        text = g_filename_display_name(name->str);
        break;
    case DATABASE_INDEX_TYPE_PATH: {
        GString *path = db_view_entry_get_path_for_idx(view, row);
        text = g_filename_display_name(path->str);
        g_string_free(path, TRUE);
        path = NULL;
        break;
    }
    case DATABASE_INDEX_TYPE_FILETYPE: {
        text = fsearch_file_utils_get_file_type(
            name->str,
            db_view_entry_get_type_for_idx(view, row) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
        break;
    }
    case DATABASE_INDEX_TYPE_SIZE:
        text =
            fsearch_file_utils_get_size_formatted(db_view_entry_get_size_for_idx(view, row), config->show_base_2_units);
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME: {
        const time_t mtime = db_view_entry_get_mtime_for_idx(view, row);
        char mtime_formatted[100] = "";
        strftime(mtime_formatted,
                 sizeof(mtime_formatted),
                 "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
                 localtime(&mtime));
        text = g_strdup(mtime_formatted);
        break;
    }
    default:
        g_warning("[query_tooltip] unknown index type");
    }

    db_view_unlock(view);

    g_string_free(name, TRUE);
    name = NULL;

    if (!text) {
        return NULL;
    }

    pango_layout_set_text(layout, text, -1);

    int32_t layout_width = 0;
    pango_layout_get_pixel_size(layout, &layout_width, NULL);
    width -= layout_width;

    if (width < 0) {
        return text;
    }

    g_free(text);
    text = NULL;

    return NULL;
}

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
                             gboolean right_to_left_text) {
    if (!columns) {
        return;
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    const int32_t icon_size = get_icon_size_for_height(rect->height - ROW_PADDING_X);

    DrawRowContext ctx = {};
    draw_row_ctx_init(view, row, bin_window, icon_size, &ctx);

    GtkStateFlags flags = gtk_style_context_get_state(context);
    if (row_selected) {
        flags |= GTK_STATE_FLAG_SELECTED;
    }
    if (row_focused) {
        flags |= GTK_STATE_FLAG_FOCUSED;
    }

    gtk_style_context_save(context);
    gtk_style_context_set_state(context, flags);

    // Render row background
    gtk_render_background(context, cr, rect->x, rect->y, rect->width, rect->height);

    // Render row foreground
    int32_t x = rect->x;
    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        cairo_save(cr);
        cairo_rectangle(cr, x, rect->y, column->effective_width, rect->height);
        cairo_clip(cr);
        int32_t dx = 0;
        int32_t dw = 0;
        pango_layout_set_attributes(layout, NULL);
        switch (column->type) {
        case DATABASE_INDEX_TYPE_NAME: {
            if (config->show_listview_icons && ctx.icon_surface) {
                int32_t x_icon = x;
                if (right_to_left_text) {
                    x_icon += column->effective_width - icon_size - ROW_PADDING_X;
                }
                else {
                    x_icon += ROW_PADDING_X;
                    dx += icon_size + 2 * ROW_PADDING_X;
                }
                dw += icon_size + 2 * ROW_PADDING_X;
                gtk_render_icon_surface(context,
                                        cr,
                                        ctx.icon_surface,
                                        x_icon,
                                        rect->y + floor((rect->height - icon_size) / 2.0));
            }
            pango_layout_set_attributes(layout, ctx.name_attr);
            pango_layout_set_text(layout, ctx.display_name, -1);
        } break;
        case DATABASE_INDEX_TYPE_PATH:
            pango_layout_set_attributes(layout, ctx.path_attr);
            pango_layout_set_text(layout, ctx.path->str, (int32_t)ctx.path->len);
            break;
        case DATABASE_INDEX_TYPE_SIZE:
            pango_layout_set_text(layout, ctx.size, -1);
            break;
        case DATABASE_INDEX_TYPE_FILETYPE:
            pango_layout_set_text(layout, ctx.type, -1);
            break;
        case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
            pango_layout_set_text(layout, ctx.time, -1);
            break;
        default:
            pango_layout_set_text(layout, "Unknown column", -1);
        }

        pango_layout_set_width(layout, (column->effective_width - 2 * ROW_PADDING_X - dw) * PANGO_SCALE);
        pango_layout_set_alignment(layout, column->alignment);
        pango_layout_set_ellipsize(layout, column->ellipsize_mode);
        gtk_render_layout(context, cr, x + ROW_PADDING_X + dx, rect->y + ROW_PADDING_Y, layout);
        x += column->effective_width;
        cairo_restore(cr);
    }
    gtk_style_context_restore(context);

    draw_row_ctx_free(&ctx);
}

FsearchResultView *
fsearch_result_view_new(void) {
    FsearchResultView *result_view = calloc(1, sizeof(FsearchResultView));
    assert(result_view != NULL);
    return result_view;
}

void
fsearch_result_view_free(FsearchResultView *result_view) {
    if (!result_view) {
        return;
    }
    free(result_view);
    result_view = NULL;
}

#define G_LOG_DOMAIN "fsearch-result-view"

#include "fsearch_result_view.h"

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_file_utils.h"
#include "fsearch_query.h"

#include <glib/gi18n.h>
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
                 const char *path,
                 FsearchDatabaseEntryType type,
                 int32_t icon_size,
                 int32_t scale_factor) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    g_return_val_if_fail(icon_theme, NULL);

    g_autoptr(GIcon) icon = fsearch_file_utils_guess_icon(name, path, type == DATABASE_ENTRY_TYPE_FOLDER);
    const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));

    if (!names) {
        return NULL;
    }

    g_autoptr(GtkIconInfo) icon_info = gtk_icon_theme_choose_icon_for_scale(icon_theme,
                                                                            (const char **)names,
                                                                            icon_size,
                                                                            scale_factor,
                                                                            GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!icon_info) {
        return NULL;
    }

    g_autoptr(GdkPixbuf) pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
    if (!pixbuf) {
        return NULL;
    }

    return gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, win);
}

typedef struct {
    char *display_name;

    FsearchQueryMatchData *match_data;
    PangoAttrList *highlights[NUM_DATABASE_INDEX_TYPES];

    cairo_surface_t *icon_surface;

    GString *path;
    GString *full_path;
    char *size;
    char *type;
    char *extension;
    char time[100];
} DrawRowContext;

static bool
draw_row_ctx_init(FsearchDatabaseView *view, uint32_t row, GdkWindow *bin_window, int32_t icon_size, DrawRowContext *ctx) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    bool ret = true;
    db_view_lock(view);

    g_autoptr(GString) name = NULL;

    const uint32_t num_items = db_view_get_num_entries(view);
    if (row >= num_items) {
        g_debug("[draw_row] row idx out of bound");
        ret = false;
        goto out;
    }

    name = db_view_entry_get_name_for_idx(view, row);
    if (!name) {
        g_debug("[draw_row] failed to get entry name");
        ret = false;
        goto out;
    }
    ctx->display_name = g_filename_display_name(name->str);

    ctx->extension = db_view_entry_get_extension_for_idx(view, row);

    ctx->path = db_view_entry_get_path_for_idx(view, row);

    FsearchQuery *query = db_view_get_query(view);
    if (query) {
        FsearchDatabaseEntry *entry = db_view_entry_get_for_idx(view, row);
        ctx->match_data = fsearch_query_match_data_new();
        fsearch_query_match_data_set_entry(ctx->match_data, entry);

        fsearch_query_highlight(query, ctx->match_data);

        g_clear_pointer(&query, fsearch_query_unref);
    }

    ctx->full_path = db_view_entry_get_path_full_for_idx(view, row);

    FsearchDatabaseEntryType type = db_view_entry_get_type_for_idx(view, row);
    ctx->type = fsearch_file_utils_get_file_type(name->str, type == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);

    ctx->icon_surface = config->show_listview_icons ? get_icon_surface(bin_window,
                                                                       name->str,
                                                                       ctx->full_path->str,
                                                                       type,
                                                                       icon_size,
                                                                       gdk_window_get_scale_factor(bin_window))
                                                    : NULL;

    off_t size = db_view_entry_get_size_for_idx(view, row);
    ctx->size = fsearch_file_utils_get_size_formatted(size, config->show_base_2_units);

    const time_t mtime = db_view_entry_get_mtime_for_idx(view, row);
    strftime(ctx->time,
             100,
             "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime(&mtime));

out:
    db_view_unlock(view);
    return ret;
}

static void
draw_row_ctx_destroy(DrawRowContext *ctx) {
    g_clear_pointer(&ctx->match_data, fsearch_query_match_data_free);
    g_clear_pointer(&ctx->display_name, g_free);
    g_clear_pointer(&ctx->extension, g_free);
    g_clear_pointer(&ctx->type, g_free);
    g_clear_pointer(&ctx->size, g_free);
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (ctx->highlights[i]) {
            g_clear_pointer(&ctx->highlights[i], pango_attr_list_unref);
        }
    }
    g_clear_pointer(&ctx->icon_surface, cairo_surface_destroy);
    if (ctx->path) {
        g_string_free(g_steal_pointer(&ctx->path), TRUE);
    }
    if (ctx->full_path) {
        g_string_free(g_steal_pointer(&ctx->full_path), TRUE);
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
    g_autoptr(GString) name = db_view_entry_get_name_for_idx(view, row);
    if (!name) {
        db_view_unlock(view);
        return NULL;
    }

    int32_t width = col->effective_width - 2 * ROW_PADDING_X;
    g_autofree char *text = NULL;

    switch (col->type) {
    case DATABASE_INDEX_TYPE_NAME:
        if (config->show_listview_icons) {
            int32_t icon_size = get_icon_size_for_height((int32_t)row_height - ROW_PADDING_X);
            width -= 2 * ROW_PADDING_X + icon_size;
        }
        text = g_filename_display_name(name->str);
        break;
    case DATABASE_INDEX_TYPE_PATH: {
        g_autoptr(GString) path = db_view_entry_get_path_for_idx(view, row);
        text = g_filename_display_name(path->str);
        break;
    }
    case DATABASE_INDEX_TYPE_EXTENSION: {
        text = db_view_entry_get_extension_for_idx(view, row);
        break;
    }
    case DATABASE_INDEX_TYPE_FILETYPE: {
        text = fsearch_file_utils_get_file_type(
            name->str,
            db_view_entry_get_type_for_idx(view, row) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
        break;
    }
    case DATABASE_INDEX_TYPE_SIZE:
        text = fsearch_file_utils_get_size_formatted(db_view_entry_get_size_for_idx(view, row),
                                                     config->show_base_2_units);
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

    g_return_val_if_fail(text, NULL);

    pango_layout_set_text(layout, text, -1);

    int32_t layout_width = 0;
    pango_layout_get_pixel_size(layout, &layout_width, NULL);
    width -= layout_width;

    if (width < 0) {
        return g_steal_pointer(&text);
    }
    return NULL;
}

static void
set_attributes(PangoLayout *layout, FsearchQueryMatchData *match_data, FsearchDatabaseIndexType idx) {
    g_assert(idx >= 0 && idx < NUM_DATABASE_INDEX_TYPES);
    PangoAttrList *attrs = fsearch_query_match_get_highlight(match_data, idx);
    if (attrs) {
        pango_layout_set_attributes(layout, attrs);
    }
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
                             gboolean row_hovered,
                             gboolean right_to_left_text) {
    if (!columns) {
        return;
    }

    const int32_t icon_size = get_icon_size_for_height(rect->height - ROW_PADDING_X);

    DrawRowContext ctx = {};
    if (!draw_row_ctx_init(view, row, bin_window, icon_size, &ctx)) {
        return;
    }

    GtkStateFlags flags = gtk_style_context_get_state(context);
    if (row_selected) {
        flags |= GTK_STATE_FLAG_SELECTED;
    }
    if (row_focused) {
        flags |= GTK_STATE_FLAG_FOCUSED;
    }
    if (row_hovered) {
        flags |= GTK_STATE_FLAG_PRELIGHT;
    }

    gtk_style_context_save(context);
    gtk_style_context_set_state(context, flags);

    // Render row background
    gtk_render_background(context, cr, rect->x, rect->y, rect->width, rect->height);
    if (row_hovered) {
        gtk_render_focus(context, cr, rect->x, rect->y, rect->width, rect->height);
    }

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

        const char *text = NULL;
        int text_len = -1;

        switch (column->type) {
        case DATABASE_INDEX_TYPE_NAME: {
            FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
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
            text = ctx.display_name;
        } break;
        case DATABASE_INDEX_TYPE_PATH:
            text = ctx.path->str;
            text_len = (int32_t)ctx.path->len;
            break;
        case DATABASE_INDEX_TYPE_SIZE:
            text = ctx.size;
            break;
        case DATABASE_INDEX_TYPE_EXTENSION:
            text = ctx.extension;
            break;
        case DATABASE_INDEX_TYPE_FILETYPE:
            text = ctx.type;
            break;
        case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
            text = ctx.time;
            break;
        default:
            text = NULL;
        }
        set_attributes(layout, ctx.match_data, column->type);
        pango_layout_set_text(layout, text ? text : _("Invalid row data"), text_len);

        pango_layout_set_width(layout, (column->effective_width - 2 * ROW_PADDING_X - dw) * PANGO_SCALE);
        pango_layout_set_alignment(layout, column->alignment);
        pango_layout_set_ellipsize(layout, column->ellipsize_mode);
        gtk_render_layout(context, cr, x + ROW_PADDING_X + dx, rect->y + ROW_PADDING_Y, layout);
        x += column->effective_width;
        cairo_restore(cr);
    }
    gtk_style_context_restore(context);

    draw_row_ctx_destroy(&ctx);
}

FsearchResultView *
fsearch_result_view_new(void) {
    FsearchResultView *result_view = calloc(1, sizeof(FsearchResultView));
    g_assert(result_view);
    return result_view;
}

void
fsearch_result_view_free(FsearchResultView *result_view) {
    g_clear_pointer(&result_view, free);
}

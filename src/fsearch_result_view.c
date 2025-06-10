#define G_LOG_DOMAIN "fsearch-result-view"

#include "fsearch_result_view.h"

#ifdef _WIN32
#include "win32_compat.h"
#endif

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_file_utils.h"
#include "fsearch_query.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <sys/stat.h>

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

static void
reset_icon_caches(FsearchResultView *result_view) {
    g_hash_table_remove_all(result_view->pixbuf_cache);
    g_hash_table_remove_all(result_view->app_gicon_cache);
}

static void
maybe_reset_icon_caches(FsearchResultView *result_view) {
    const uint32_t cached_icon_limit = 200;
    if (g_hash_table_size(result_view->pixbuf_cache) > cached_icon_limit) {
        g_hash_table_remove_all(result_view->pixbuf_cache);
    }
    if (g_hash_table_size(result_view->app_gicon_cache) > cached_icon_limit) {
        g_hash_table_remove_all(result_view->app_gicon_cache);
    }
}

static GIcon *
get_desktop_file_icon(FsearchResultView *result_view, const char *path) {
    GIcon *icon = g_hash_table_lookup(result_view->app_gicon_cache, path);
    if (!icon) {
        icon = fsearch_file_utils_get_desktop_file_icon(path);
        g_hash_table_insert(result_view->app_gicon_cache, g_strdup(path), icon);
    }
    return g_object_ref(icon);
}

static GdkPixbuf *
get_pixbuf_from_gicon(FsearchResultView *result_view, GIcon *icon, int32_t icon_size, int32_t scale_factor) {
    GdkPixbuf *pixbuf = g_hash_table_lookup(result_view->pixbuf_cache, icon);
    if (pixbuf) {
        return pixbuf;
    }

    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    g_return_val_if_fail(icon_theme, NULL);

    if (G_IS_THEMED_ICON(icon)) {
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

        pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
    }
    else if (G_IS_LOADABLE_ICON(icon)) {
        g_autoptr(GInputStream) stream = g_loadable_icon_load(G_LOADABLE_ICON(icon), icon_size, NULL, NULL, NULL);
        if (stream) {
            pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, icon_size, icon_size, TRUE, NULL, NULL);
        }
    }

    if (pixbuf) {
        g_hash_table_insert(result_view->pixbuf_cache, g_object_ref(icon), pixbuf);
    }
    return pixbuf;
}

static cairo_surface_t *
get_icon_surface(FsearchResultView *result_view,
                 GdkWindow *win,
                 const char *name,
                 const char *path,
                 FsearchDatabaseEntryType type,
                 int32_t icon_size,
                 int32_t scale_factor) {
    maybe_reset_icon_caches(result_view);

    g_autoptr(GIcon) icon = NULL;
    struct stat buffer;
    if (lstat(path, &buffer)) {
        icon = g_themed_icon_new("edit-delete");
    }
    else if (type == DATABASE_ENTRY_TYPE_FILE && fsearch_file_utils_is_desktop_file(path)) {
        icon = get_desktop_file_icon(result_view, path);
    }
    else {
        icon = fsearch_file_utils_guess_icon(name, path, type == DATABASE_ENTRY_TYPE_FOLDER);
    }

    GdkPixbuf *pixbuf = get_pixbuf_from_gicon(result_view, icon, icon_size, scale_factor);
    if (!pixbuf) {
        return NULL;
    }

    return gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, win);
}

typedef struct {
    char *display_name;

    FsearchQueryMatchData *match_data;
    PangoAttrList *highlights[NUM_DATABASE_INDEX_TYPES];

    FsearchDatabaseEntryType entry_type;

    GString *name;
    GString *path;
    GString *full_path;
    char *size;
    char *type;
    char *extension;
    char time[100];
} DrawRowContext;

static void
draw_row_ctx_free(DrawRowContext *ctx) {
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
    if (ctx->name) {
        g_string_free(g_steal_pointer(&ctx->name), TRUE);
    }
    if (ctx->path) {
        g_string_free(g_steal_pointer(&ctx->path), TRUE);
    }
    if (ctx->full_path) {
        g_string_free(g_steal_pointer(&ctx->full_path), TRUE);
    }
    g_clear_pointer(&ctx, free);
}

static DrawRowContext *
draw_row_ctx_new(FsearchDatabaseView *view, uint32_t row, GdkWindow *bin_window, int32_t icon_size) {
    DrawRowContext *ctx = calloc(1, sizeof(DrawRowContext));
    g_assert(ctx);

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    bool ret = true;
    db_view_lock(view);

    const uint32_t num_items = db_view_get_num_entries(view);
    if (row >= num_items) {
        g_debug("[draw_row] row idx out of bound");
        ret = false;
        goto out;
    }

    ctx->name = db_view_entry_get_name_for_idx(view, row);
    if (!ctx->name) {
        g_debug("[draw_row] failed to get entry name");
        ret = false;
        goto out;
    }
    ctx->display_name = g_filename_display_name(ctx->name->str);

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

    ctx->entry_type = db_view_entry_get_type_for_idx(view, row);
    ctx->type = fsearch_file_utils_get_file_type(ctx->name->str,
                                                 ctx->entry_type == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);

    off_t size = db_view_entry_get_size_for_idx(view, row);
    ctx->size = fsearch_file_utils_get_size_formatted(size, config->show_base_2_units);

    const time_t mtime = db_view_entry_get_mtime_for_idx(view, row);
    strftime(ctx->time,
             100,
             "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime(&mtime));

out:
    db_view_unlock(view);
    if (ret) {
        return ctx;
    }
    g_clear_pointer(&ctx, draw_row_ctx_free);
    return NULL;
}

static DrawRowContext *
draw_row_ctx_get(FsearchResultView *result_view, uint32_t row, GdkWindow *bin_window, int32_t icon_size) {
    g_return_val_if_fail(result_view, NULL);

    if (g_hash_table_size(result_view->row_cache) > 100) {
        fsearch_result_view_row_cache_reset(result_view);
    }

    DrawRowContext *ctx = g_hash_table_lookup(result_view->row_cache, GINT_TO_POINTER(row + 1));
    if (ctx) {
        return ctx;
    }
    ctx = draw_row_ctx_new(result_view->database_view, row, bin_window, icon_size);
    if (!ctx) {
        return NULL;
    }
    g_hash_table_insert(result_view->row_cache, GINT_TO_POINTER(row + 1), ctx);
    return ctx;
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
fsearch_result_view_draw_row(FsearchResultView *result_view,
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

    if (result_view->row_height != rect->height) {
        reset_icon_caches(result_view);
    }
    result_view->row_height = rect->height;

    DrawRowContext *ctx = draw_row_ctx_get(result_view, row, bin_window, icon_size);
    if (!ctx) {
        return;
    }

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
    if (row_hovered) {
        GdkRGBA color = {};
        gtk_style_context_get_color(context, flags, &color);
        color.alpha = 0.05;

        cairo_save(cr);
        gdk_cairo_set_source_rgba(cr, &color);
        cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

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
            if (config->show_listview_icons) {
                cairo_surface_t *icon_surface = config->show_listview_icons
                                                  ? get_icon_surface(result_view,
                                                                     bin_window,
                                                                     ctx->name->str,
                                                                     ctx->full_path->str,
                                                                     ctx->entry_type,
                                                                     icon_size,
                                                                     gdk_window_get_scale_factor(bin_window))
                                                  : NULL;
                if (icon_surface) {
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
                                            icon_surface,
                                            x_icon,
                                            rect->y + floor((rect->height - icon_size) / 2.0));
                    g_clear_pointer(&icon_surface, cairo_surface_destroy);
                }
            }
            text = ctx->display_name;
        } break;
        case DATABASE_INDEX_TYPE_PATH:
            text = ctx->path->str;
            text_len = (int32_t)ctx->path->len;
            break;
        case DATABASE_INDEX_TYPE_SIZE:
            text = ctx->size;
            break;
        case DATABASE_INDEX_TYPE_EXTENSION:
            text = ctx->extension;
            break;
        case DATABASE_INDEX_TYPE_FILETYPE:
            text = ctx->type;
            break;
        case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
            text = ctx->time;
            break;
        default:
            text = NULL;
        }

        if (config->highlight_search_terms) {
            set_attributes(layout, ctx->match_data, column->type);
        }

        pango_layout_set_text(layout, text ? text : _("Invalid row data"), text_len);

        pango_layout_set_width(layout, (column->effective_width - 2 * ROW_PADDING_X - dw) * PANGO_SCALE);
        pango_layout_set_alignment(layout, column->alignment);
        pango_layout_set_ellipsize(layout, column->ellipsize_mode);
        gtk_render_layout(context, cr, x + ROW_PADDING_X + dx, rect->y + ROW_PADDING_Y, layout);
        x += column->effective_width;
        cairo_restore(cr);
    }
    gtk_style_context_restore(context);
}

void
fsearch_result_view_row_cache_reset(FsearchResultView *result_view) {
    g_return_if_fail(result_view);
    g_hash_table_remove_all(result_view->row_cache);
}

FsearchResultView *
fsearch_result_view_new(void) {
    FsearchResultView *result_view = calloc(1, sizeof(FsearchResultView));
    g_assert(result_view);

    result_view->row_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)draw_row_ctx_free);
    result_view->pixbuf_cache =
        g_hash_table_new_full(g_icon_hash, (GEqualFunc)g_icon_equal, g_object_unref, g_object_unref);
    result_view->app_gicon_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    return result_view;
}

void
fsearch_result_view_free(FsearchResultView *result_view) {
    g_clear_pointer(&result_view->pixbuf_cache, g_hash_table_unref);
    g_clear_pointer(&result_view->app_gicon_cache, g_hash_table_unref);
    g_clear_pointer(&result_view->row_cache, g_hash_table_unref);
    g_clear_pointer(&result_view, free);
}

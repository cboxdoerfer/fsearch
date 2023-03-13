#define G_LOG_DOMAIN "fsearch-result-view"

#include "fsearch_result_view.h"

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_database_entry_info.h"
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

static gpointer
get_key_from_row_idx(uint32_t idx) {
    return GUINT_TO_POINTER(idx + 1);
}

static gboolean
try_get_entry_info(FsearchResultView *result_view, uint32_t row, FsearchDatabaseEntryInfo **info) {
    gpointer key = get_key_from_row_idx(row);
    if (g_hash_table_lookup_extended(result_view->item_info_cache, key, NULL, (gpointer *)info)) {
        return TRUE;
    }
    if (fsearch_database2_try_get_item_info(result_view->db,
                                            result_view->view_id,
                                            row,
                                            FSEARCH_DATABASE_ENTRY_INFO_FLAG_ALL,
                                            info)
        == FSEARCH_RESULT_SUCCESS) {
        g_hash_table_insert(result_view->item_info_cache, key, *info);
        return TRUE;
    }
    g_autoptr(FsearchDatabaseWork)
        work = fsearch_database_work_new_get_item_info(result_view->view_id, row, FSEARCH_DATABASE_ENTRY_INFO_FLAG_ALL);
    fsearch_database2_queue_work(result_view->db, work);
    g_hash_table_insert(result_view->item_info_cache, key, NULL);
    return FALSE;
}

static void
set_pango_layout_attributes(PangoLayout *layout, FsearchDatabaseEntryInfo *info, FsearchDatabaseIndexProperty idx) {
    if (!info) {
        return;
    }

    g_assert(idx >= 0 && idx < NUM_DATABASE_INDEX_PROPERTIES);
    GHashTable *highlights = fsearch_database_entry_info_get_highlights(info);
    if (!highlights) {
        return;
    }
    PangoAttrList *attrs = g_hash_table_lookup(highlights, GUINT_TO_POINTER(idx));
    if (!attrs) {
        return;
    }

    pango_layout_set_attributes(layout, attrs);
}

char *
fsearch_result_view_query_tooltip(FsearchResultView *view,
                                  uint32_t row,
                                  FsearchListViewColumn *col,
                                  PangoLayout *layout,
                                  uint32_t row_height) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    FsearchDatabaseEntryInfo *info = NULL;
    if (try_get_entry_info(view, row, &info)) {
        if (!info) {
            // TODO: handle async case when info isn't ready yet
            return NULL;
        }
    }
    else {
        return NULL;
    }

    int32_t width = col->effective_width - 2 * ROW_PADDING_X;
    g_autofree char *text = NULL;

    switch (col->type) {
    case DATABASE_INDEX_PROPERTY_NAME: {
        if (config->show_listview_icons) {
            int32_t icon_size = get_icon_size_for_height((int32_t)row_height - ROW_PADDING_X);
            width -= 2 * ROW_PADDING_X + icon_size;
        }
        text = g_filename_display_name(fsearch_database_entry_info_get_name(info)->str);
        break;
    }
    case DATABASE_INDEX_PROPERTY_PATH: {
        text = g_filename_display_name(fsearch_database_entry_info_get_path(info)->str);
        break;
    }
    case DATABASE_INDEX_PROPERTY_EXTENSION: {
        text = g_strdup(fsearch_database_entry_info_get_extension(info)->str);
        break;
    }
    case DATABASE_INDEX_PROPERTY_FILETYPE: {
        text = fsearch_file_utils_get_file_type(
            fsearch_database_entry_info_get_name(info)->str,
            fsearch_database_entry_info_get_entry_type(info) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
        break;
    }
    case DATABASE_INDEX_PROPERTY_SIZE:
        text = fsearch_file_utils_get_size_formatted(fsearch_database_entry_info_get_size(info),
                                                     config->show_base_2_units);
        break;
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME: {
        const time_t mtime = fsearch_database_entry_info_get_mtime(info);
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

static cairo_surface_t *
get_cairo_surface_for_gicon(FsearchResultView *result_view,
                            GdkWindow *win,
                            GIcon *icon,
                            int32_t icon_size,
                            int32_t scale_factor) {
    GdkPixbuf *pixbuf = get_pixbuf_from_gicon(result_view, icon, icon_size, scale_factor);
    if (!pixbuf) {
        return NULL;
    }
    return gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, win);
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

    gboolean pending = FALSE;
    FsearchDatabaseEntryInfo *info = NULL;
    if (try_get_entry_info(result_view, row, &info)) {
        if (!info) {
            pending = TRUE;
        }
    }
    else {
        return;
    }

    GtkStateFlags flags = gtk_style_context_get_state(context);
    if (!pending) {
        row_selected = fsearch_database_entry_info_get_selected(info);
    }
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

        g_autofree char *text_autofree = NULL;
        const char *text = NULL;
        char text_time[100] = "";
        int text_len = -1;

        if (pending) {
            text = "Loading...";
            text_len = strlen(text);
        }
        else {
            switch (column->type) {
            case DATABASE_INDEX_PROPERTY_NAME: {
                text = fsearch_database_entry_info_get_name(info)->str;

                if (config->show_listview_icons) {
                    cairo_surface_t *icon_surface = config->show_listview_icons ? get_cairo_surface_for_gicon(
                                                        result_view,
                                                        bin_window,
                                                        fsearch_database_entry_info_get_icon(info),
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
                break;
            }
            case DATABASE_INDEX_PROPERTY_PATH: {
                GString *path = fsearch_database_entry_info_get_path(info);
                text = path->str;
                text_len = path->len;
                break;
            }
            case DATABASE_INDEX_PROPERTY_SIZE:
                text_autofree = fsearch_file_utils_get_size_formatted(fsearch_database_entry_info_get_size(info),
                                                                      config->show_base_2_units);
                text = text_autofree;
                break;
            case DATABASE_INDEX_PROPERTY_EXTENSION: {
                text = fsearch_database_entry_info_get_extension(info)->str;
                break;
            }
            case DATABASE_INDEX_PROPERTY_FILETYPE:
                text_autofree = fsearch_file_utils_get_file_type(
                    fsearch_database_entry_info_get_name(info)->str,
                    fsearch_database_entry_info_get_entry_type(info) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
                text = text_autofree;
                break;
            case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME: {
                const time_t mtime = fsearch_database_entry_info_get_mtime(info);
                strftime(text_time,
                         100,
                         "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
                         localtime(&mtime));
                text = text_time;
                break;
            }
            default:
                text = NULL;
            }
        }

        if (config->highlight_search_terms) {
            set_pango_layout_attributes(layout, info, column->type);
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
    g_hash_table_remove_all(result_view->item_info_cache);
}

static void
on_item_info_ready(FsearchDatabase2 *db, guint view_id, FsearchDatabaseEntryInfo *info, FsearchResultView *view) {
    if (!info) {
        return;
    }
    if (view_id != view->view_id) {
        return;
    }
    const uint32_t row = fsearch_database_entry_info_get_index(info);
    gpointer key = get_key_from_row_idx(row);
    if (!g_hash_table_lookup(view->item_info_cache, key)) {
        g_hash_table_insert(view->item_info_cache, key, fsearch_database_entry_info_ref(info));
        fsearch_list_view_redraw_row(view->list_view, row);
    }
}

static void
entry_info_free(FsearchDatabaseEntryInfo *info) {
    if (!info) {
        return;
    }
    g_clear_pointer(&info, fsearch_database_entry_info_unref);
}

FsearchResultView *
fsearch_result_view_new(guint view_id) {
    FsearchResultView *result_view = calloc(1, sizeof(FsearchResultView));
    g_assert(result_view);

    result_view->view_id = view_id;
    result_view->item_info_cache =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)entry_info_free);
    result_view->db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    result_view->pixbuf_cache =
        g_hash_table_new_full(g_icon_hash, (GEqualFunc)g_icon_equal, g_object_unref, g_object_unref);
    result_view->app_gicon_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);

    g_signal_connect(result_view->db, "item-info-ready", G_CALLBACK(on_item_info_ready), result_view);
    return result_view;
}

void
fsearch_result_view_free(FsearchResultView *result_view) {
    g_clear_pointer(&result_view->item_info_cache, g_hash_table_unref);
    g_clear_pointer(&result_view->pixbuf_cache, g_hash_table_unref);
    g_clear_pointer(&result_view->app_gicon_cache, g_hash_table_unref);
    g_clear_object(&result_view->db);
    g_clear_pointer(&result_view, free);
}

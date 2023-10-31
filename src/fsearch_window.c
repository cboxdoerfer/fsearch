/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define G_LOG_DOMAIN "fsearch-window"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fsearch_array.h"
#include "fsearch_config.h"
#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_info.h"
#include "fsearch_database_search_info.h"
#include "fsearch_file_utils.h"
#include "fsearch_list_view.h"
#include "fsearch_listview_popup.h"
#include "fsearch_result_view.h"
#include "fsearch_statusbar.h"
#include "fsearch_string_utils.h"
#include "fsearch_ui_utils.h"
#include "fsearch_window.h"
#include "fsearch_window_actions.h"
#include <glib/gi18n.h>

struct _FsearchApplicationWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *app_menu;
    GtkWidget *filter_combobox;
    GtkWidget *filter_revealer;
    GtkWidget *headerbar_box;
    GtkWidget *listview_scrolled_window;
    GtkWidget *main_box;
    GtkWidget *menu_box;
    GtkWidget *overlay_database_empty;
    GtkWidget *overlay_database_loading;
    GtkWidget *overlay_database_updating;
    GtkWidget *overlay_query_empty;
    GtkWidget *overlay_results_empty;
    GtkWidget *overlay_results_sorting;
    GtkWidget *popover_update_button_stack;
    GtkWidget *search_box;
    GtkWidget *search_button_revealer;
    GtkWidget *search_entry;
    GtkWidget *main_stack;
    GtkWidget *main_database_overlay_stack;
    GtkWidget *main_result_overlay;
    GtkWidget *main_search_overlay_stack;

    GtkWidget *statusbar;

    char *active_filter_name;

    FsearchDatabase *db;
    FsearchDatabaseWork *work_search;
    FsearchDatabaseWork *work_sort;

    uint32_t num_files_selected;
    uint32_t num_folders_selected;

    FsearchResultView *result_view;
};

typedef enum {
    OVERLAY_DATABASE,
    OVERLAY_DATABASE_EMPTY,
    OVERLAY_DATABASE_LOADING,
    OVERLAY_DATABASE_UPDATING,
    OVERLAY_QUERY_EMPTY,
    OVERLAY_RESULTS,
    OVERLAY_RESULTS_EMPTY,
    OVERLAY_RESULTS_SORTING,
    NUM_OVERLAYS,
} FsearchOverlay;

G_DEFINE_TYPE(FsearchApplicationWindow, fsearch_application_window, GTK_TYPE_APPLICATION_WINDOW)

static void
perform_search(FsearchApplicationWindow *win);

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay);

static void
on_filter_combobox_changed(GtkComboBox *widget, gpointer user_data);

static void
modify_selection(FsearchApplicationWindow *self, FsearchSelectionType type, int32_t start_idx, int32_t end_idx) {
    const guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(self));
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_modify_selection(win_id, type, start_idx, end_idx);
    fsearch_database_queue_work(self->db, work);
}

static void
redraw_listview(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    gtk_widget_queue_draw(GTK_WIDGET(self->result_view->list_view));
}

static FsearchFilter *
get_active_filter(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    const uint32_t active_filter = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    return fsearch_filter_manager_get_filter(config->filters, active_filter);
}

static FsearchQueryFlags
get_query_flags() {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    FsearchQueryFlags flags = 0;
    if (config->match_case) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }
    if (config->auto_match_case) {
        flags |= QUERY_FLAG_AUTO_MATCH_CASE;
    }
    if (config->enable_regex) {
        flags |= QUERY_FLAG_REGEX;
    }
    if (config->search_in_path) {
        flags |= QUERY_FLAG_SEARCH_IN_PATH;
    }
    if (config->auto_search_in_path) {
        flags |= QUERY_FLAG_AUTO_SEARCH_IN_PATH;
    }
    return flags;
}

static const char *
get_query_text(FsearchApplicationWindow *win) {
    return gtk_entry_get_text(GTK_ENTRY(win->search_entry));
}

static FsearchApplicationWindow *
get_window_for_id(uint32_t win_id) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    return FSEARCH_APPLICATION_WINDOW(gtk_application_get_window_by_id(GTK_APPLICATION(app), win_id));
}

static gboolean
is_empty_search(FsearchApplicationWindow *win) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    if (text && text[0] == '\0' && config->hide_results_on_empty_search) {
        return TRUE;
    }
    return FALSE;
}

static void
fsearch_window_listview_set_empty(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    self->result_view->sort_order = fsearch_list_view_get_sort_order(self->result_view->list_view);
    self->result_view->sort_type = fsearch_list_view_get_sort_type(self->result_view->list_view);
    fsearch_list_view_set_config(self->result_view->list_view,
                                 0,
                                 self->result_view->sort_order,
                                 self->result_view->sort_type);
}

static void
database_load_started(FsearchApplicationWindow *win) {
    show_overlay(win, OVERLAY_DATABASE_LOADING);
}

static void
database_scan_started(FsearchApplicationWindow *win) {
    show_overlay(win, OVERLAY_DATABASE_UPDATING);

    GtkWidget *cancel_update_button = gtk_stack_get_child_by_name(GTK_STACK(win->popover_update_button_stack),
                                                                  "cancel_database_update");
    if (cancel_update_button) {
        gtk_stack_set_visible_child(GTK_STACK(win->popover_update_button_stack), cancel_update_button);
    }
}

static void
fsearch_window_apply_menubar_config(FsearchApplicationWindow *win) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    gtk_widget_set_visible(win->menu_box, config->show_menubar);
    gtk_widget_set_visible(win->app_menu, !config->show_menubar);

    if (config->show_menubar) {
        gtk_window_set_titlebar(GTK_WINDOW(win), NULL);
        gtk_window_set_title(GTK_WINDOW(win), g_get_application_name());

        g_object_ref(G_OBJECT(win->search_box));
        gtk_container_remove(GTK_CONTAINER(win->headerbar_box), win->search_box);
        gtk_box_pack_start(GTK_BOX(win->menu_box), win->search_box, TRUE, TRUE, 0);
        gtk_box_reorder_child(GTK_BOX(win->menu_box), win->search_box, 0);
        g_object_unref(G_OBJECT(win->search_box));
    }
    else {
        GtkStyleContext *list_style = gtk_widget_get_style_context(win->listview_scrolled_window);
        gtk_style_context_add_class(list_style, "results_frame_csd_mode");
    }
    // ensure search entry still has focus after reordering the search_box
    gtk_widget_grab_focus(win->search_entry);
}

static void
fsearch_window_set_overlay_for_database_state(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;

    FsearchDatabaseState state = fsearch_application_get_db_state(app);
    const uint32_t num_items = fsearch_application_get_num_db_entries(app);

    if (num_items > 0) {
        show_overlay(win, OVERLAY_RESULTS);
        return;
    }

    show_overlay(win, OVERLAY_DATABASE);
    if (state == FSEARCH_DATABASE_STATE_LOADING) {
        show_overlay(win, OVERLAY_DATABASE_LOADING);
    }
    else if (state == FSEARCH_DATABASE_STATE_SCANNING) {
        show_overlay(win, OVERLAY_DATABASE_UPDATING);
    }
    else {
        show_overlay(win, OVERLAY_DATABASE_EMPTY);
    }
}

static void
apply_filter_config(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    g_signal_handlers_block_by_func(win->filter_combobox, on_filter_combobox_changed, win);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(win->filter_combobox));

    uint32_t active_filter = 0;
    for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(config->filters); ++i) {
        FsearchFilter *filter = fsearch_filter_manager_get_filter(config->filters, i);
        if (filter && filter->name) {
            if (win->active_filter_name && !strcmp(win->active_filter_name, filter->name)) {
                // in order to restore the previously active filter we remember the index of the filter which matches
                // the active filter name
                active_filter = i;
            }
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(win->filter_combobox), NULL, filter->name);
            g_clear_pointer(&filter, fsearch_filter_unref);
        }
    }
    g_signal_handlers_unblock_by_func(win->filter_combobox, on_filter_combobox_changed, win);
    gtk_combo_box_set_active(GTK_COMBO_BOX(win->filter_combobox), (int32_t)active_filter);

    perform_search(win);
}

static void
fsearch_window_apply_config(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    if (config->restore_window_size) {
        gtk_window_set_default_size(GTK_WINDOW(self), config->window_width, config->window_height);
    }
    fsearch_application_window_apply_search_revealer_config(self);
    fsearch_application_window_apply_statusbar_revealer_config(self);
    apply_filter_config(self);

    fsearch_window_set_overlay_for_database_state(self);
}

static void
fsearch_application_window_constructed(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->constructed(object);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    self->work_search = NULL;
    self->work_sort = NULL;

    fsearch_window_apply_menubar_config(self);

    switch (fsearch_application_get_db_state(app)) {
    case FSEARCH_DATABASE_STATE_LOADING:
        database_load_started(self);
        break;
    case FSEARCH_DATABASE_STATE_SCANNING:
        database_scan_started(self);
        break;
    default:
        break;
    }
}

static void
fsearch_application_window_finalize(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    g_clear_pointer(&self->active_filter_name, free);
    g_clear_pointer(&self->result_view, fsearch_result_view_free);
    g_clear_pointer(&self->work_search, fsearch_database_work_unref);
    g_clear_pointer(&self->work_sort, fsearch_database_work_unref);
    g_clear_object(&self->db);

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->finalize(object);
}

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay) {
    switch (overlay) {
    case OVERLAY_RESULTS:
        gtk_stack_set_visible_child(GTK_STACK(win->main_stack), win->main_result_overlay);
        break;
    case OVERLAY_RESULTS_EMPTY:
        gtk_stack_set_visible_child(GTK_STACK(win->main_search_overlay_stack), win->overlay_results_empty);
        break;
    case OVERLAY_RESULTS_SORTING:
        gtk_stack_set_visible_child(GTK_STACK(win->main_stack), win->overlay_results_sorting);
        break;
    case OVERLAY_DATABASE:
        gtk_stack_set_visible_child(GTK_STACK(win->main_stack), win->main_database_overlay_stack);
        break;
    case OVERLAY_DATABASE_EMPTY:
        gtk_stack_set_visible_child(GTK_STACK(win->main_database_overlay_stack), win->overlay_database_empty);
        break;
    case OVERLAY_QUERY_EMPTY:
        gtk_stack_set_visible_child(GTK_STACK(win->main_search_overlay_stack), win->overlay_query_empty);
        break;
    case OVERLAY_DATABASE_LOADING:
        gtk_stack_set_visible_child(GTK_STACK(win->main_database_overlay_stack), win->overlay_database_loading);
        break;
    case OVERLAY_DATABASE_UPDATING:
        gtk_stack_set_visible_child(GTK_STACK(win->main_database_overlay_stack), win->overlay_database_updating);
        break;
    default:
        gtk_stack_set_visible_child(GTK_STACK(win->main_stack), win->main_result_overlay);
        g_debug("[win] overlay %d unknown", overlay);
    }
}

static void
apply_search_info(FsearchApplicationWindow *win, FsearchDatabaseSearchInfo *info, bool reset_view) {
    if (!info) {
        return;
    }
    g_autoptr(FsearchQuery) query = fsearch_database_search_info_get_query(info);
    if (query) {
        fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                                  FSEARCH_STATUSBAR_REVEALER_SMART_MATCH_CASE,
                                                  query->triggers_auto_match_case);
        fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                                  FSEARCH_STATUSBAR_REVEALER_SMART_SEARCH_IN_PATH,
                                                  query->triggers_auto_match_path);
    }

    win->num_files_selected = fsearch_database_search_info_get_num_files_selected(info);
    win->num_folders_selected = fsearch_database_search_info_get_num_folders_selected(info);

    win->result_view->sort_order = fsearch_database_search_info_get_sort_order(info);
    win->result_view->sort_type = fsearch_database_search_info_get_sort_type(info);

    const uint32_t num_rows = fsearch_database_search_info_get_num_entries(info);
    fsearch_statusbar_set_num_search_results(FSEARCH_STATUSBAR(win->statusbar), num_rows);

    const uint32_t num_files = fsearch_database_search_info_get_num_files(info);
    const uint32_t num_folders = fsearch_database_search_info_get_num_folders(info);
    fsearch_statusbar_set_selection(FSEARCH_STATUSBAR(win->statusbar),
                                    win->num_files_selected,
                                    win->num_folders_selected,
                                    num_files,
                                    num_folders);

    fsearch_result_view_row_cache_reset(win->result_view);
    if (reset_view) {
        fsearch_list_view_set_config(win->result_view->list_view,
                                     num_rows,
                                     win->result_view->sort_order,
                                     win->result_view->sort_type);
    }
    else {
        fsearch_list_view_update(win->result_view->list_view,
                                 num_rows,
                                 win->result_view->sort_order,
                                 win->result_view->sort_type);
    }
    fsearch_window_actions_update(win);

    if (is_empty_search(win)) {
        show_overlay(win, OVERLAY_QUERY_EMPTY);
        gtk_widget_show(win->main_search_overlay_stack);
    }
    else if (num_rows == 0) {
        show_overlay(win, OVERLAY_RESULTS_EMPTY);
        gtk_widget_show(win->main_search_overlay_stack);
    }
    else {
        gtk_widget_hide(win->main_search_overlay_stack);
    }
}

static void
on_sort_finished(FsearchDatabase *db, guint id, FsearchDatabaseSearchInfo *info, gpointer user_data) {
    FsearchApplicationWindow *win = get_window_for_id(id);

    if (win) {
        apply_search_info(win, info, true);

        g_clear_pointer(&win->work_sort, fsearch_database_work_unref);
    }
}

static void
on_sort_started(FsearchDatabase *db, gpointer data, gpointer user_data) {
    const guint win_id = GPOINTER_TO_UINT(data);
    FsearchApplicationWindow *win = get_window_for_id(win_id);
    if (win) {
        fsearch_statusbar_set_sort_status_delayed(FSEARCH_STATUSBAR(win->statusbar));
    }
}

static void
on_selection_changed(FsearchDatabase *db, guint id, FsearchDatabaseSearchInfo *info, gpointer self) {
    FsearchApplicationWindow *win = get_window_for_id(id);

    if (win) {
        apply_search_info(win, info, false);
    }
}

static void
on_search_finished(FsearchDatabase *db, guint id, FsearchDatabaseSearchInfo *info, gpointer self) {
    FsearchApplicationWindow *win = get_window_for_id(id);

    if (win) {
        apply_search_info(win, info, true);
        g_clear_pointer(&win->work_search, fsearch_database_work_unref);
    }
}

static void
on_search_started(FsearchDatabase *db, gpointer data, gpointer user_data) {
    const guint win_id = GPOINTER_TO_UINT(data);
    FsearchApplicationWindow *win = get_window_for_id(win_id);

    if (win) {
        fsearch_statusbar_set_query_status_delayed(FSEARCH_STATUSBAR(win->statusbar));
    }
    return;
}

static void
perform_search(FsearchApplicationWindow *win) {
    if (!win) {
        return;
    }

    const gchar *text = get_query_text(win);
    const guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));
    FsearchFilter *filter = get_active_filter(win);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    FsearchQuery *query = fsearch_query_new(text, filter, config->filters, get_query_flags(), "test");
    if (win->work_search) {
        fsearch_database_work_cancel(win->work_search);
    }
    g_clear_pointer(&win->work_search, fsearch_database_work_unref);
    win->work_search = fsearch_database_work_new_search(win_id,
                                                        query,
                                                        fsearch_list_view_get_sort_order(win->result_view->list_view),
                                                        fsearch_list_view_get_sort_type(win->result_view->list_view));
    g_clear_pointer(&filter, fsearch_filter_unref);
    fsearch_database_queue_work(win->db, win->work_search);
}

static gboolean
on_fsearch_list_view_popup(FsearchListView *view, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    const guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));

    return listview_popup_menu(user_data, win->db, win_id);
}

static gboolean
on_listview_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));
    GActionGroup *group = G_ACTION_GROUP(win);

    GdkModifierType default_modifiers = gtk_accelerator_get_default_mod_mask();
    guint keyval;
    GdkModifierType state;

    gdk_event_get_state(event, &state);
    gdk_event_get_keyval(event, &keyval);

    if ((state & default_modifiers) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        switch (keyval) {
        case GDK_KEY_C:
            g_action_group_activate_action(group, "copy_filepath_clipboard", NULL);
            return GDK_EVENT_STOP;
        default:
            return GDK_EVENT_PROPAGATE;
        }
    }
    else if ((state & default_modifiers) == GDK_CONTROL_MASK) {
        switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            g_action_group_activate_action(group, "open_folder", NULL);
            return GDK_EVENT_STOP;
        case GDK_KEY_c:
            g_action_group_activate_action(group, "copy_clipboard", NULL);
            return GDK_EVENT_STOP;
        case GDK_KEY_x:
            g_action_group_activate_action(group, "cut_clipboard", NULL);
            return GDK_EVENT_STOP;
        default:
            return GDK_EVENT_PROPAGATE;
        }
    }
    else if ((state & default_modifiers) == GDK_SHIFT_MASK) {
        switch (keyval) {
        case GDK_KEY_Delete:
            g_action_group_activate_action(group, "delete_selection", NULL);
            return GDK_EVENT_STOP;
        default:
            return GDK_EVENT_PROPAGATE;
        }
    }
    else {
        switch (keyval) {
        case GDK_KEY_Delete:
            g_action_group_activate_action(group, "move_to_trash", NULL);
            return GDK_EVENT_STOP;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            g_action_group_activate_action(group, "open", NULL);
            return GDK_EVENT_STOP;
        default:
            return GDK_EVENT_PROPAGATE;
        }
    }
    return GDK_EVENT_PROPAGATE;
}

static void
on_fsearch_list_view_row_activated(FsearchListView *view, FsearchDatabaseIndexProperty col, int row_idx, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    int launch_folder = false;
    if (config->double_click_path && col == DATABASE_INDEX_PROPERTY_PATH) {
        launch_folder = true;
    }

    fsearch_window_action_open_generic(self, launch_folder ? true : false, true);
    return;
}

static void
on_search_entry_changed(GtkEntry *entry, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    if (config->search_as_you_type) {
        perform_search(win);
    }
}

static char *
on_listview_query_tooltip(PangoLayout *layout,
                          uint32_t row_height,
                          uint32_t row_idx,
                          FsearchListViewColumn *col,
                          gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    return fsearch_result_view_query_tooltip(win->result_view, row_idx, col, layout, row_height);
}

static void
on_listview_draw_row(cairo_t *cr,
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
                     gpointer user_data) {
    if (!columns) {
        return;
    }

    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);

    fsearch_result_view_draw_row(win->result_view,
                                 cr,
                                 bin_window,
                                 layout,
                                 context,
                                 columns,
                                 rect,
                                 row,
                                 row_selected,
                                 row_focused,
                                 row_hovered,
                                 right_to_left_text);
}

static void
on_listview_sort(int sort_order, GtkSortType sort_type, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    const guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));

    if (win->work_sort) {
        fsearch_database_work_cancel(win->work_sort);
    }
    g_clear_pointer(&win->work_sort, fsearch_database_work_unref);
    win->work_sort = fsearch_database_work_new_sort(win_id, sort_order, sort_type);

    fsearch_database_queue_work(win->db, win->work_sort);
}

static void
add_columns(FsearchListView *view, FsearchConfig *config) {
    const bool restore = config->restore_column_config;
    FsearchListViewColumn *name_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_NAME,
                                                                   _("Name"),
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   TRUE,
                                                                   TRUE,
                                                                   restore ? config->name_column_width : 250);
    FsearchListViewColumn *path_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_PATH,
                                                                   _("Path"),
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_path_column : TRUE,
                                                                   FALSE,
                                                                   restore ? config->path_column_width : 250);
    FsearchListViewColumn *size_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_SIZE,
                                                                   _("Size"),
                                                                   PANGO_ALIGN_RIGHT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_size_column : TRUE,
                                                                   FALSE,
                                                                   restore ? config->size_column_width : 75);
    FsearchListViewColumn *type_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_FILETYPE,
                                                                   _("Type"),
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_type_column : FALSE,
                                                                   FALSE,
                                                                   restore ? config->type_column_width : 100);
    FsearchListViewColumn *ext_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_EXTENSION,
                                                                  _("Extension"),
                                                                  PANGO_ALIGN_LEFT,
                                                                  PANGO_ELLIPSIZE_END,
                                                                  restore ? config->show_extension_column : TRUE,
                                                                  FALSE,
                                                                  restore ? config->extension_column_width : 100);
    FsearchListViewColumn *changed_col = fsearch_list_view_column_new(DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                                                      _("Date Modified"),
                                                                      PANGO_ALIGN_RIGHT,
                                                                      PANGO_ELLIPSIZE_END,
                                                                      restore ? config->show_modified_column : TRUE,
                                                                      FALSE,
                                                                      restore ? config->modified_column_width : 125);

    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), name_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), path_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), ext_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), type_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), size_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), changed_col);
    fsearch_list_view_column_set_tooltip(type_col,
                                         _("Sorting by <b>Type</b> can take a few seconds with many results.\n\n"
                                           "This sort order is not persistent, it will be reset when the search term "
                                           "changes."));
    fsearch_list_view_column_set_emblem(type_col, "emblem-important-symbolic", TRUE);
}

static guint
on_listview_row_num_selected(gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    return fsearch_application_window_get_num_selected(win);
}

static void
on_listview_row_unselect_all(gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    modify_selection(win, FSEARCH_SELECTION_TYPE_CLEAR, 0, 0);
}

static void
on_listview_row_toggle_range(int start_row, int end_row, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    modify_selection(win, FSEARCH_SELECTION_TYPE_TOGGLE_RANGE, start_row, end_row);
}

static void
on_listview_row_select_range(int start_row, int end_row, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    modify_selection(win, FSEARCH_SELECTION_TYPE_SELECT_RANGE, start_row, end_row);
}

static void
on_listview_row_select_toggle(int row, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    modify_selection(win, FSEARCH_SELECTION_TYPE_TOGGLE, row, 0);
}

static void
on_listview_row_select(int row, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    modify_selection(win, FSEARCH_SELECTION_TYPE_SELECT, row, 0);
}

static gboolean
on_listview_row_is_selected(int row, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);

    g_autoptr(FsearchDatabaseEntryInfo) info = NULL;
    // TODO: handle async case where entry info isn't ready yet or maybe use blocking call?
    if (fsearch_database_try_get_item_info(win->result_view->db,
                                           win->result_view->view_id,
                                           row,
                                           FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED,
                                           &info)
        == FSEARCH_RESULT_SUCCESS) {
        return fsearch_database_entry_info_get_selected(info);
    }
    return FALSE;
}

static void
fsearch_application_window_init_overlays(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/"
                                                                  "fsearch_overlay.ui");

    win->main_database_overlay_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_database_overlay_stack"));
    win->main_search_overlay_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_search_overlay_stack"));

    // Overlay when no search results are found
    win->overlay_results_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_results_empty"));

    // Overlay when database is empty
    win->overlay_database_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_empty"));

    // Overlay when search query is empty
    win->overlay_query_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_query_empty"));

    // Overlay when database is updating
    win->overlay_database_updating = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_updating"));

    // Overlay when database is loading
    win->overlay_database_loading = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_loading"));

    // Overlay when results are being sorted
    win->overlay_results_sorting = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_results_sorting"));

    gtk_stack_add_named(GTK_STACK(win->main_stack), win->overlay_results_sorting, "overlay_results_sorting");
    gtk_stack_add_named(GTK_STACK(win->main_stack), win->main_database_overlay_stack, "overlay_database_stack");

    gtk_overlay_add_overlay(GTK_OVERLAY(win->main_result_overlay), win->main_search_overlay_stack);
    gtk_stack_set_visible_child(GTK_STACK(win->main_stack), win->main_database_overlay_stack);

    gtk_widget_show_all(win->main_stack);
}

static void
fsearch_application_window_init_listview(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(win->listview_scrolled_window));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(win->listview_scrolled_window));
    FsearchListView *list_view = fsearch_list_view_new(hadj, vadj);
    gtk_container_add(GTK_CONTAINER(win->listview_scrolled_window), GTK_WIDGET(list_view));

    gtk_widget_show((GTK_WIDGET(list_view)));
    fsearch_list_view_set_query_tooltip_func(list_view, on_listview_query_tooltip, win);
    fsearch_list_view_set_draw_row_func(list_view, on_listview_draw_row, win);
    fsearch_list_view_set_sort_func(list_view, on_listview_sort, win);
    fsearch_list_view_set_selection_handlers(list_view,
                                             on_listview_row_is_selected,
                                             on_listview_row_select,
                                             on_listview_row_select_toggle,
                                             on_listview_row_select_range,
                                             on_listview_row_toggle_range,
                                             on_listview_row_unselect_all,
                                             on_listview_row_num_selected,
                                             win);
    fsearch_list_view_set_single_click_activate(list_view, config->single_click_open);
    gtk_widget_set_has_tooltip(GTK_WIDGET(list_view), config->enable_list_tooltips);

    add_columns(list_view, config);

    g_signal_connect_object(list_view, "row-popup", G_CALLBACK(on_fsearch_list_view_popup), win, G_CONNECT_AFTER);
    g_signal_connect_object(list_view, "row-activated", G_CALLBACK(on_fsearch_list_view_row_activated), win, G_CONNECT_AFTER);
    g_signal_connect(list_view, "key-press-event", G_CALLBACK(on_listview_key_press_event), win);

    win->result_view->list_view = list_view;
}

static void
on_database_update_finished(FsearchDatabase *db2, FsearchDatabaseInfo *info, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    fsearch_window_listview_set_empty(win);
    fsearch_statusbar_set_num_search_results(FSEARCH_STATUSBAR(win->statusbar), 0);

    GtkWidget *update_database_button = gtk_stack_get_child_by_name(GTK_STACK(win->popover_update_button_stack),
                                                                    "update_database");
    if (update_database_button) {
        gtk_stack_set_visible_child(GTK_STACK(win->popover_update_button_stack), update_database_button);
    }
    fsearch_window_set_overlay_for_database_state(win);

    perform_search(win);
}

static void
on_database_load_started(FsearchDatabase *db2, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    database_load_started(win);
}

static void
on_database_scan_started(FsearchDatabase *db2, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    database_scan_started(win);
}

static void
fsearch_application_window_init(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    guint id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(self));
    self->result_view = fsearch_result_view_new(id);

    self->statusbar = GTK_WIDGET(fsearch_statusbar_new());
    gtk_box_pack_end(GTK_BOX(self->main_box), self->statusbar, FALSE, TRUE, 0);

    fsearch_window_actions_init(self);
    fsearch_application_window_init_listview(self);
    fsearch_application_window_init_overlays(self);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    self->db = fsearch_application_get_db(app);
    g_signal_connect_object(self->db, "search-started", G_CALLBACK(on_search_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "search-finished", G_CALLBACK(on_search_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "sort-started", G_CALLBACK(on_sort_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "sort-finished", G_CALLBACK(on_sort_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "scan-started", G_CALLBACK(on_database_scan_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "scan-finished", G_CALLBACK(on_database_update_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "load-started", G_CALLBACK(on_database_load_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "load-finished", G_CALLBACK(on_database_update_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(self->db, "selection-changed", G_CALLBACK(on_selection_changed), self, G_CONNECT_AFTER);
}

static void
on_filter_combobox_changed(GtkComboBox *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    char *active_filter_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(win->filter_combobox));
    if (active_filter_name) {
        g_clear_pointer(&win->active_filter_name, free);
        win->active_filter_name = active_filter_name;
    }
    fsearch_statusbar_set_filter(FSEARCH_STATUSBAR(win->statusbar), active ? active_filter_name : NULL);

    perform_search(win);
}

static gboolean
on_search_entry_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    guint keyval;
    gdk_event_get_keyval(event, &keyval);
    if (keyval == GDK_KEY_Down) {
        const gint cursor_idx = fsearch_list_view_get_cursor(win->result_view->list_view);
        gtk_widget_grab_focus(GTK_WIDGET(win->result_view->list_view));
        fsearch_list_view_set_cursor(win->result_view->list_view, cursor_idx);
        return TRUE;
    }
    return FALSE;
}

static void
on_search_entry_activate(GtkButton *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (config->search_as_you_type) {
        // select the first entry in the result list if the selection is empty
        if (win->num_folders_selected == 0 && win->num_files_selected == 0) {
            modify_selection(win, FSEARCH_SELECTION_TYPE_SELECT, 0, 0);
        }
        gtk_widget_grab_focus(GTK_WIDGET(win->result_view->list_view));
    }
    else {
        perform_search(win);
    }
}

static gboolean
on_fsearch_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(widget);
    fsearch_application_window_prepare_shutdown(win);
    g_clear_pointer(&widget, gtk_widget_destroy);
    return TRUE;
}

static void
fsearch_application_window_class_init(FsearchApplicationWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->constructed = fsearch_application_window_constructed;
    object_class->finalize = fsearch_application_window_finalize;

    gtk_widget_class_set_template_from_resource(widget_class, "/io/github/cboxdoerfer/fsearch/ui/fsearch_window.ui");
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, app_menu);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_combobox);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview_scrolled_window);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_result_overlay);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, menu_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_update_button_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_entry);

    gtk_widget_class_bind_template_callback(widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_fsearch_window_delete_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_activate);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_key_press_event);
}

void
fsearch_application_window_apply_statusbar_revealer_config(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    GtkStyleContext *filter_style = gtk_widget_get_style_context(win->listview_scrolled_window);
    if (!config->show_statusbar) {
        gtk_style_context_add_class(filter_style, "results_frame_last");
    }
    else {
        gtk_style_context_remove_class(filter_style, "results_frame_last");
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->statusbar), config->show_statusbar);

    fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                              FSEARCH_STATUSBAR_REVEALER_MATCH_CASE,
                                              config->match_case);
    fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                              FSEARCH_STATUSBAR_REVEALER_REGEX,
                                              config->enable_regex);
    fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                              FSEARCH_STATUSBAR_REVEALER_SEARCH_IN_PATH,
                                              config->search_in_path);
}

void
fsearch_application_window_apply_search_revealer_config(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    GtkStyleContext *filter_style = gtk_widget_get_style_context(win->filter_combobox);
    if (config->show_search_button && config->show_filter) {
        gtk_style_context_add_class(filter_style, "filter_centered");
    }
    else {
        gtk_style_context_remove_class(filter_style, "filter_centered");
    }
    GtkStyleContext *entry_style = gtk_widget_get_style_context(win->search_entry);
    if (config->show_search_button || config->show_filter) {
        gtk_style_context_add_class(entry_style, "search_entry_has_neighbours");
    }
    else {
        gtk_style_context_remove_class(entry_style, "search_entry_has_neighbours");
    }

    gtk_revealer_set_reveal_child(GTK_REVEALER(win->filter_revealer), config->show_filter);
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_button_revealer), config->show_search_button);
}

void
fsearch_application_window_update_query_flags(FsearchApplicationWindow *win) {
    apply_filter_config(win);
}

static FsearchDatabaseIndexProperty
get_sort_order_for_name(const char *name) {
    if (!name) {
        g_warning("[get_sort_order_for_name] name is nullptr");
        return DATABASE_INDEX_PROPERTY_NAME;
    }
    if (!strcmp(name, DATABASE_INDEX_PROPERTY_NAME_STRING)) {
        return DATABASE_INDEX_PROPERTY_NAME;
    }
    else if (!strcmp(name, DATABASE_INDEX_PROPERTY_PATH_STRING)) {
        return DATABASE_INDEX_PROPERTY_PATH;
    }
    else if (!strcmp(name, DATABASE_INDEX_PROPERTY_SIZE_STRING)) {
        return DATABASE_INDEX_PROPERTY_SIZE;
    }
    else if (!strcmp(name, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME_STRING)) {
        return DATABASE_INDEX_PROPERTY_MODIFICATION_TIME;
    }
    else if (!strcmp(name, DATABASE_INDEX_PROPERTY_EXTENSION_STRING)) {
        return DATABASE_INDEX_PROPERTY_EXTENSION;
    }
    else if (!strcmp(name, DATABASE_INDEX_PROPERTY_FILETYPE_STRING)) {
        return DATABASE_INDEX_PROPERTY_FILETYPE;
    }
    else {
        return DATABASE_INDEX_PROPERTY_NAME;
    }
}

static char *
get_sort_name_for_type(FsearchDatabaseIndexProperty type) {
    const char *name = NULL;
    switch (type) {
    case DATABASE_INDEX_PROPERTY_NAME:
        name = DATABASE_INDEX_PROPERTY_NAME_STRING;
        break;
    case DATABASE_INDEX_PROPERTY_PATH:
        name = DATABASE_INDEX_PROPERTY_PATH_STRING;
        break;
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        name = DATABASE_INDEX_PROPERTY_MODIFICATION_TIME_STRING;
        break;
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        name = DATABASE_INDEX_PROPERTY_EXTENSION_STRING;
        break;
    case DATABASE_INDEX_PROPERTY_FILETYPE:
        name = DATABASE_INDEX_PROPERTY_FILETYPE_STRING;
        break;
    case DATABASE_INDEX_PROPERTY_SIZE:
        name = DATABASE_INDEX_PROPERTY_SIZE_STRING;
        break;
    default:
        name = DATABASE_INDEX_PROPERTY_NAME_STRING;
    }
    return g_strdup(name);
}

void
fsearch_application_window_prepare_shutdown(gpointer self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    FsearchApplicationWindow *win = self;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gint width = 850;
    gint height = 800;
    gtk_window_get_size(GTK_WINDOW(self), &width, &height);
    config->window_width = width;
    config->window_height = height;

    if (win->result_view && win->result_view->list_view) {
        FsearchListView *list_view = win->result_view->list_view;
        config->sort_ascending = fsearch_list_view_get_sort_type(list_view) == GTK_SORT_ASCENDING ? true : false;

        if (config->sort_by) {
            g_clear_pointer(&config->sort_by, g_free);
        }
        config->sort_by = get_sort_name_for_type(fsearch_list_view_get_sort_order(list_view));

        // update the config with the widths of all columns whose width we can store
        const struct {
            int type;
            uint32_t *width;
        } columns[] = {
            {DATABASE_INDEX_PROPERTY_NAME, &config->name_column_width},
            {DATABASE_INDEX_PROPERTY_PATH, &config->path_column_width},
            {DATABASE_INDEX_PROPERTY_FILETYPE, &config->type_column_width},
            {DATABASE_INDEX_PROPERTY_EXTENSION, &config->extension_column_width},
            {DATABASE_INDEX_PROPERTY_SIZE, &config->size_column_width},
            {DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &config->modified_column_width},
        };
        for (int i = 0; i < G_N_ELEMENTS(columns); i++) {
            const FsearchListViewColumn *col = fsearch_list_view_get_first_column_for_type(list_view, columns[i].type);
            if (col) {
                *columns[i].width = col->width;
            }
        }
    }
}

void
fsearch_application_window_added(FsearchApplicationWindow *win, FsearchApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));
    guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));

    if (win_id <= 0) {
        g_warning("[window_added] window isn't part of FsearchApplication");
        return;
    }

    win->result_view->view_id = win_id;

    FsearchConfig *config = fsearch_application_get_config(app);

    FsearchDatabaseIndexProperty sort_order = config->restore_sort_order ? get_sort_order_for_name(config->sort_by)
                                                                         : DATABASE_INDEX_PROPERTY_NAME;
    if (sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
        // file type order is not indexed, so it would make startup really slow
        // -> fall back to sort by name instead
        sort_order = DATABASE_INDEX_PROPERTY_NAME;
    }
    const GtkSortType sort_type = config->restore_sort_order
                                    ? (config->sort_ascending ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING)
                                    : GTK_SORT_ASCENDING;

    fsearch_window_apply_config(win);
    fsearch_list_view_set_config(win->result_view->list_view, 0, sort_order, sort_type);
    perform_search(win);
}

void
fsearch_application_window_cancel_current_task(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));
    if (win->work_search) {
        fsearch_database_work_cancel(win->work_search);
    }
    if (win->work_sort) {
        fsearch_database_work_cancel(win->work_sort);
    }
}

void
fsearch_application_window_invert_selection(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    modify_selection(self, FSEARCH_SELECTION_TYPE_INVERT, 0, 0);
}

void
fsearch_application_window_unselect_all(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    modify_selection(self, FSEARCH_SELECTION_TYPE_CLEAR, 0, 0);
}

void
fsearch_application_window_select_all(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    modify_selection(self, FSEARCH_SELECTION_TYPE_ALL, 0, 0);
}

uint32_t
fsearch_application_window_get_num_selected(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    return self->num_folders_selected + self->num_files_selected;
}

void
fsearch_application_window_selection_for_each(FsearchApplicationWindow *self,
                                              FsearchDatabaseForeachFunc func,
                                              gpointer user_data) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));

    const guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(self));
    fsearch_database_selection_foreach(self->db, win_id, func, user_data);
}

void
fsearch_application_window_focus_search_entry(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));
    // Make sure the entry also has focus and the text is selected
    gtk_widget_grab_focus(win->search_entry);
}

GtkEntry *
fsearch_application_window_get_search_entry(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    return GTK_ENTRY(self->search_entry);
}

FsearchStatusbar *
fsearch_application_window_get_statusbar(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    return FSEARCH_STATUSBAR(self->statusbar);
}

void
fsearch_application_window_set_database_index_progress(FsearchApplicationWindow *self, const char *text) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    fsearch_statusbar_set_database_index_text(FSEARCH_STATUSBAR(self->statusbar), text);
}

uint32_t
fsearch_application_window_get_num_rows(FsearchApplicationWindow *self) {
    if (self->result_view->list_view) {
        return fsearch_list_view_get_num_rows(self->result_view->list_view);
    }
    return 0;
}

gint
fsearch_application_window_get_active_filter(FsearchApplicationWindow *self) {
    return gtk_combo_box_get_active(GTK_COMBO_BOX(self->filter_combobox));
}

void
fsearch_application_window_set_active_filter(FsearchApplicationWindow *self, guint active_filter) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->filter_combobox), (gint)active_filter);
}

void
fsearch_application_window_update_listview_config(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(win));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    fsearch_list_view_set_single_click_activate(win->result_view->list_view, config->single_click_open);
    gtk_widget_set_has_tooltip(GTK_WIDGET(win->result_view->list_view), config->enable_list_tooltips);

    redraw_listview(win);
}

void
fsearch_application_window_toggle_app_menu(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    GtkToggleButton *app_menu = GTK_TOGGLE_BUTTON(self->app_menu);
    gtk_toggle_button_set_active(app_menu, !gtk_toggle_button_get_active(app_menu));
}

FsearchListView *
fsearch_application_window_get_listview(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_IS_APPLICATION_WINDOW(self));
    return self->result_view->list_view;
}

FsearchApplicationWindow *
fsearch_application_window_new(FsearchApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    return g_object_new(FSEARCH_APPLICATION_WINDOW_TYPE, "application", app, NULL);
}

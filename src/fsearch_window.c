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
#include "fsearch_file_utils.h"
#include "fsearch_limits.h"
#include "fsearch_list_view.h"
#include "fsearch_listview_popup.h"
#include "fsearch_statusbar.h"
#include "fsearch_string_utils.h"
#include "fsearch_task.h"
#include "fsearch_ui_utils.h"
#include "fsearch_window.h"
#include "fsearch_window_actions.h"
#include <glib/gi18n.h>
#include <math.h>

struct _FsearchApplicationWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *app_menu;
    GtkWidget *filter_combobox;
    GtkWidget *filter_revealer;
    GtkWidget *headerbar_box;
    GtkWidget *listview;
    GtkWidget *listview_scrolled_window;
    GtkWidget *main_box;
    GtkWidget *menu_box;
    GtkWidget *overlay_database_empty;
    GtkWidget *overlay_database_loading;
    GtkWidget *overlay_database_updating;
    GtkWidget *overlay_query_empty;
    GtkWidget *overlay_results_empty;
    GtkWidget *overlay_results_sorting;
    GtkWidget *popover_cancel_update_db;
    GtkWidget *popover_update_db;
    GtkWidget *search_box;
    GtkWidget *search_button_revealer;
    GtkWidget *search_entry;
    GtkWidget *search_overlay;

    GtkWidget *statusbar;

    FsearchDatabaseView *db_view;

    FsearchDatabaseIndexType sort_order;
    GtkSortType sort_type;

    guint statusbar_timeout_id;
};

typedef enum {
    OVERLAY_DATABASE_EMPTY,
    OVERLAY_DATABASE_LOADING,
    OVERLAY_QUERY_EMPTY,
    OVERLAY_RESULTS_EMPTY,
    OVERLAY_RESULTS_SORTING,
    NUM_OVERLAYS,
} FsearchOverlay;

static void
perform_search(FsearchApplicationWindow *win);

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay);

static void
hide_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay);

static void
hide_overlays(FsearchApplicationWindow *win);

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay);

static FsearchFilter *
get_active_filter(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    uint32_t active_filter = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    GList *filter_element = g_list_nth(fsearch_application_get_filters(app), active_filter);
    FsearchFilter *filter = filter_element->data;
    return filter;
}

static FsearchQueryFlags
get_query_flags() {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    FsearchQueryFlags flags = {.enable_regex = config->enable_regex,
                               .match_case = config->match_case,
                               .auto_match_case = config->auto_match_case,
                               .search_in_path = config->search_in_path,
                               .auto_search_in_path = config->auto_search_in_path};
    return flags;
}

const char *
get_query_text(FsearchApplicationWindow *win) {
    return gtk_entry_get_text(GTK_ENTRY(win->search_entry));
}

static FsearchApplicationWindow *
get_window_for_id(uint32_t win_id) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    return FSEARCH_WINDOW_WINDOW(gtk_application_get_window_by_id(GTK_APPLICATION(app), win_id));
}

static void
fsearch_window_listview_set_empty(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    self->sort_order = fsearch_list_view_get_sort_order(FSEARCH_LIST_VIEW(self->listview));
    self->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(self->listview));
    fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->listview), 0, self->sort_order, self->sort_type);
}

static gboolean
fsearch_window_sort_finished(gpointer data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(data);
    gtk_widget_show(win->listview);
    hide_overlay(win, OVERLAY_RESULTS_SORTING);
    return G_SOURCE_REMOVE;
}

static gboolean
fsearch_window_sort_started(gpointer data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(data);
    gtk_widget_hide(win->listview);
    show_overlay(win, OVERLAY_RESULTS_SORTING);
    return G_SOURCE_REMOVE;
}

static void *
fsearch_list_view_get_entry_for_row(int row_idx, GtkSortType sort_type, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    if (!win || !win->db_view) {
        return NULL;
    }

    if (sort_type == GTK_SORT_DESCENDING) {
        row_idx = (int)db_view_get_num_entries(win->db_view) - row_idx - 1;
    }

    return db_view_get_entry(win->db_view, row_idx);
}

static void
database_load_started(FsearchApplicationWindow *win) {
    show_overlay(win, OVERLAY_DATABASE_LOADING);
}

static void
database_scan_started(FsearchApplicationWindow *win) {
    gtk_widget_hide(win->popover_update_db);
    gtk_widget_show(win->popover_cancel_update_db);
}

void
fsearch_application_window_update_query_flags(FsearchApplicationWindow *win) {
    db_view_set_query_flags(win->db_view, get_query_flags());
}

void
fsearch_application_window_prepare_shutdown(gpointer self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    // FsearchApplicationWindow *win = self;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gint width = 800;
    gint height = 800;
    gtk_window_get_size(GTK_WINDOW(self), &width, &height);
    config->window_width = width;
    config->window_height = height;

    // gint sort_column_id = 0;
    // GtkSortType order = GTK_SORT_ASCENDING;
    // gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(win->list_model), &sort_column_id, &order);

    // if (config->sort_by) {
    //    g_free(config->sort_by);
    //    config->sort_by = NULL;
    //}

    // if (sort_column_id == SORT_ID_NAME) {
    //    config->sort_by = g_strdup("Name");
    //}
    // else if (sort_column_id == SORT_ID_PATH) {
    //    config->sort_by = g_strdup("Path");
    //}
    // else if (sort_column_id == SORT_ID_TYPE) {
    //    config->sort_by = g_strdup("Type");
    //}
    // else if (sort_column_id == SORT_ID_SIZE) {
    //    config->sort_by = g_strdup("Size");
    //}
    // else if (sort_column_id == SORT_ID_CHANGED) {
    //    config->sort_by = g_strdup("Date Modified");
    //}
    // else {
    //    config->sort_by = g_strdup("Name");
    //}

    // if (order == GTK_SORT_ASCENDING) {
    //    config->sort_ascending = true;
    //}
    // else {
    //    config->sort_ascending = false;
    //}
}

void
fsearch_application_window_remove_model(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    fsearch_window_listview_set_empty(win);
}

void
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

void
fsearch_window_apply_statusbar_revealer_config(FsearchApplicationWindow *win) {
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
fsearch_window_apply_search_revealer_config(FsearchApplicationWindow *win) {
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

static void
fsearch_window_apply_config(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    if (config->restore_window_size) {
        gtk_window_set_default_size(GTK_WINDOW(self), config->window_width, config->window_height);
    }
    fsearch_window_apply_search_revealer_config(self);
    fsearch_window_apply_statusbar_revealer_config(self);

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(self->filter_combobox));
    for (GList *f = fsearch_application_get_filters(app); f != NULL; f = f->next) {
        FsearchFilter *filter = f->data;
        if (filter && filter->name) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->filter_combobox), NULL, filter->name);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(self->filter_combobox), 0);
    }
    FsearchDatabase *db = fsearch_application_get_db(app);
    if (!db) {
        show_overlay(self, OVERLAY_DATABASE_EMPTY);
        return;
    }

    uint32_t num_items = db_get_num_entries(db);

    if (!config->indexes || num_items == 0) {
        show_overlay(self, OVERLAY_DATABASE_EMPTY);
    }

    db_unref(db);
}

G_DEFINE_TYPE(FsearchApplicationWindow, fsearch_application_window, GTK_TYPE_APPLICATION_WINDOW)

static void
fsearch_application_window_constructed(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->constructed(object);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;

    fsearch_window_apply_config(self);

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
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->finalize(object);
}

static void
hide_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay) {
    switch (overlay) {
    case OVERLAY_RESULTS_EMPTY:
        gtk_widget_hide(win->overlay_results_empty);
        break;
    case OVERLAY_RESULTS_SORTING:
        gtk_widget_hide(win->overlay_results_sorting);
        break;
    case OVERLAY_DATABASE_EMPTY:
        gtk_widget_hide(win->overlay_database_empty);
        break;
    case OVERLAY_QUERY_EMPTY:
        gtk_widget_hide(win->overlay_query_empty);
        break;
    case OVERLAY_DATABASE_LOADING:
        gtk_widget_hide(win->overlay_database_loading);
        break;
    default:
        g_debug("[win] overlay %d unknown", overlay);
    }
}
static void
hide_overlays(FsearchApplicationWindow *win) {
    gtk_widget_hide(win->overlay_database_empty);
    gtk_widget_hide(win->overlay_database_loading);
    gtk_widget_hide(win->overlay_database_updating);
    gtk_widget_hide(win->overlay_query_empty);
    gtk_widget_hide(win->overlay_results_empty);
    gtk_widget_hide(win->overlay_results_sorting);
}

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay) {
    hide_overlays(win);

    switch (overlay) {
    case OVERLAY_RESULTS_EMPTY:
        gtk_widget_show(win->overlay_results_empty);
        break;
    case OVERLAY_RESULTS_SORTING:
        gtk_widget_show(win->overlay_results_sorting);
        break;
    case OVERLAY_DATABASE_EMPTY:
        gtk_widget_show(win->overlay_database_empty);
        break;
    case OVERLAY_QUERY_EMPTY:
        gtk_widget_show(win->overlay_query_empty);
        break;
    case OVERLAY_DATABASE_LOADING:
        gtk_widget_show(win->overlay_database_loading);
        break;
    default:
        g_debug("[win] overlay %d unknown", overlay);
    }
}

uint32_t
fsearch_application_window_get_num_results(FsearchApplicationWindow *self) {
    if (self->db_view) {
        return db_view_get_num_entries(self->db_view);
    }
    return 0;
}

gint
fsearch_application_window_get_active_filter(FsearchApplicationWindow *self) {
    return gtk_combo_box_get_active(GTK_COMBO_BOX(self->filter_combobox));
}

void
fsearch_application_window_set_active_filter(FsearchApplicationWindow *self, guint active_filter) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->filter_combobox), active_filter);
}

static int
fsearch_window_db_view_search_finished_cb(gpointer data) {
    const guint win_id = GPOINTER_TO_UINT(data);
    FsearchApplicationWindow *win = get_window_for_id(win_id);

    if (!win) {
        return G_SOURCE_REMOVE;
    }
    const uint32_t num_rows = db_view_get_num_entries(win->db_view);
    win->sort_order = db_view_get_sort_order(win->db_view);
    win->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(win->listview));
    fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(win->listview), num_rows, win->sort_order, win->sort_type);
    return G_SOURCE_REMOVE;
}

static void
fsearch_window_db_view_search_finished(FsearchDatabaseView *view, gpointer user_data) {
    if (!user_data) {
        return;
    }
    g_idle_add(fsearch_window_db_view_search_finished_cb, user_data);
}

static int
fsearch_window_db_view_search_started_cb(gpointer data) {
    const guint win_id = GPOINTER_TO_UINT(data);
    FsearchApplicationWindow *win = get_window_for_id(win_id);

    if (!win) {
        return G_SOURCE_REMOVE;
    }
    fsearch_statusbar_set_query_status_delayed(FSEARCH_STATUSBAR(win->statusbar));
    return G_SOURCE_REMOVE;
}

static void
fsearch_window_db_view_search_started(FsearchDatabaseView *view, gpointer user_data) {
    if (!user_data) {
        return;
    }
    g_idle_add(fsearch_window_db_view_search_started_cb, user_data);
}

static void
perform_search(FsearchApplicationWindow *win) {
    if (!win || !win->db_view) {
        return;
    }

    const gchar *text = get_query_text(win);
    db_view_set_query_text(win->db_view, text);

    bool reveal_smart_case = false;
    bool reveal_smart_path = false;
    if (!fs_str_is_empty(text)) {
        bool has_separator = strchr(text, G_DIR_SEPARATOR) ? 1 : 0;
        bool has_upper_text = fs_str_has_upper(text) ? 1 : 0;
        FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
        FsearchConfig *config = fsearch_application_get_config(app);
        reveal_smart_case = config->auto_match_case && !config->match_case && has_upper_text;
        reveal_smart_path = config->auto_search_in_path && !config->search_in_path && has_separator;
    }

    fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                              FSEARCH_STATUSBAR_REVEALER_SMART_MATCH_CASE,
                                              reveal_smart_case);
    fsearch_statusbar_set_revealer_visibility(FSEARCH_STATUSBAR(win->statusbar),
                                              FSEARCH_STATUSBAR_REVEALER_SMART_SEARCH_IN_PATH,
                                              reveal_smart_path);
}

typedef struct {
    uint32_t num_folders;
    uint32_t num_files;
} count_results_ctx;

static void
count_results_cb(gpointer key, gpointer value, count_results_ctx *ctx) {
    if (!value) {
        return;
    }
    FsearchDatabaseEntry *entry = value;
    FsearchDatabaseEntryType type = db_entry_get_type(entry);
    if (type == DATABASE_ENTRY_TYPE_FOLDER) {
        ctx->num_folders++;
    }
    else if (type == DATABASE_ENTRY_TYPE_FILE) {
        ctx->num_files++;
    }
}

static gboolean
on_fsearch_list_view_popup(FsearchListView *view, int row_idx, GtkSortType sort_type, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;

    FsearchDatabaseEntry *entry = fsearch_list_view_get_entry_for_row(row_idx, sort_type, win);
    if (!entry) {
        return FALSE;
    }

    return listview_popup_menu(user_data, db_entry_get_name(entry), db_entry_get_type(entry));
}

static gboolean
on_listview_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
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
            return TRUE;
        default:
            return FALSE;
        }
    }
    else if ((state & default_modifiers) == GDK_CONTROL_MASK) {
        switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            g_action_group_activate_action(group, "open_folder", NULL);
            return TRUE;
        case GDK_KEY_c:
            g_action_group_activate_action(group, "copy_clipboard", NULL);
            return TRUE;
        case GDK_KEY_x:
            g_action_group_activate_action(group, "cut_clipboard", NULL);
            return TRUE;
        default:
            return FALSE;
        }
    }
    else if ((state & default_modifiers) == GDK_SHIFT_MASK) {
        switch (keyval) {
        case GDK_KEY_Delete:
            g_action_group_activate_action(group, "delete_selection", NULL);
            return TRUE;
        default:
            return FALSE;
        }
    }
    else {
        switch (keyval) {
        case GDK_KEY_Delete:
            g_action_group_activate_action(group, "move_to_trash", NULL);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            g_action_group_activate_action(group, "open", NULL);
            return TRUE;
        default:
            return FALSE;
        }
    }
    return FALSE;
}

static void
on_file_open_failed_response(GtkDialog *dialog, GtkResponseType response, gpointer user_data) {
    if (response != GTK_RESPONSE_YES) {
        fsearch_window_action_after_file_open(false);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
on_fsearch_list_view_row_activated(FsearchListView *view,
                                   FsearchDatabaseIndexType col,
                                   int row_idx,
                                   GtkSortType sort_type,
                                   gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    int launch_folder = false;
    if (config->double_click_path && col == DATABASE_INDEX_TYPE_PATH) {
        launch_folder = true;
    }

    FsearchDatabaseEntry *entry = fsearch_list_view_get_entry_for_row(row_idx, sort_type, self);
    if (!entry) {
        return;
    }

    if (!launch_folder ? fsearch_file_utils_launch_entry(entry)
                       : fsearch_file_utils_launch_entry_with_command(entry, config->folder_open_cmd)) {
        // open succeeded
        fsearch_window_action_after_file_open(true);
    }
    else {
        // open failed
        if ((config->action_after_file_open_keyboard || config->action_after_file_open_mouse)
            && config->show_dialog_failed_opening) {
            ui_utils_run_gtk_dialog_async(GTK_WIDGET(self),
                                          GTK_MESSAGE_WARNING,
                                          GTK_BUTTONS_YES_NO,
                                          _("Failed to open file"),
                                          _("Do you want to keep the window open?"),
                                          G_CALLBACK(on_file_open_failed_response),
                                          NULL);
        }
    }
}

static void
on_fsearch_list_view_selection_changed(FsearchListView *view, gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    fsearch_window_actions_update(self);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;
    if (self->db_view) {
        num_folders = db_view_get_num_folders(self->db_view);
        num_files = db_view_get_num_files(self->db_view);
    }

    count_results_ctx ctx = {0, 0};
    fsearch_list_view_selection_for_each(view, (GHFunc)count_results_cb, &ctx);

    fsearch_statusbar_set_selection(FSEARCH_STATUSBAR(self->statusbar),
                                    ctx.num_files,
                                    ctx.num_folders,
                                    num_files,
                                    num_folders);
}

static void
on_search_entry_changed(GtkEntry *entry, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    if (config->search_as_you_type) {
        perform_search(win);
    }
}

void
fsearch_application_window_update_listview_config(FsearchApplicationWindow *app) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(app));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    fsearch_list_view_set_single_click_activate(FSEARCH_LIST_VIEW(app->listview), config->single_click_open);
    gtk_widget_set_has_tooltip(GTK_WIDGET(app->listview), config->enable_list_tooltips);
}

static cairo_surface_t *
get_icon_surface(GdkWindow *win,
                 const char *name,
                 FsearchDatabaseEntryType type,
                 const char *path,
                 int icon_size,
                 int scale_factor) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    if (!icon_theme) {
        return NULL;
    }

    cairo_surface_t *icon_surface = NULL;
    // GIcon *icon = fsearch_file_utils_get_icon_for_path(path);
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
draw_row_ctx_init(uint32_t row,
                  GtkSortType sort_type,
                  FsearchApplicationWindow *win,
                  GdkWindow *bin_window,
                  int icon_size,
                  DrawRowContext *ctx) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    FsearchDatabaseEntry *entry = fsearch_list_view_get_entry_for_row(row, sort_type, win);
    if (!entry) {
        return;
    }
    const char *name = db_entry_get_name(entry);
    if (!name) {
        return;
    }
    ctx->display_name = g_filename_display_name(name);

    FsearchQuery *query = db_view_get_query(win->db_view);
    ctx->name_attr = query ? fsearch_query_highlight_match(query, name) : NULL;

    ctx->path = db_entry_get_path(entry);
    if (query && ((query->has_separator && query->flags.auto_search_in_path) || query->flags.search_in_path)) {
        ctx->path_attr = fsearch_query_highlight_match(query, ctx->path->str);
    }

    ctx->full_path = g_string_new_len(ctx->path->str, ctx->path->len);
    g_string_append_c(ctx->full_path, G_DIR_SEPARATOR);
    g_string_append(ctx->full_path, name);

    ctx->type =
        fsearch_file_utils_get_file_type(name, db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);

    ctx->icon_surface = config->show_listview_icons ? get_icon_surface(bin_window,
                                                                       name,
                                                                       db_entry_get_type(entry),
                                                                       ctx->full_path->str,
                                                                       icon_size,
                                                                       gtk_widget_get_scale_factor(GTK_WIDGET(win)))
                                                    : NULL;

    off_t size = db_entry_get_size(entry);
    ctx->size = fsearch_file_utils_get_size_formatted(size, config->show_base_2_units);

    const time_t mtime = db_entry_get_mtime(entry);
    strftime(ctx->time,
             100,
             "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime(&mtime));
}

static int
get_icon_size_for_height(int height) {
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

static char *
fsearch_list_view_query_tooltip(PangoLayout *layout,
                                GtkSortType sort_type,
                                uint32_t row_height,
                                uint32_t row_idx,
                                FsearchListViewColumn *col,
                                gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    FsearchDatabaseEntry *entry = fsearch_list_view_get_entry_for_row(row_idx, sort_type, win);
    if (!entry) {
        return NULL;
    }
    const char *name = db_entry_get_name(entry);
    if (!name) {
        return NULL;
    }

    int width = col->effective_width - 2 * ROW_PADDING_X;
    char *text = NULL;

    switch (col->type) {
    case DATABASE_INDEX_TYPE_NAME:
        if (config->show_listview_icons) {
            int icon_size = get_icon_size_for_height(row_height - ROW_PADDING_X);
            width -= 2 * ROW_PADDING_X + icon_size;
        }
        text = g_filename_display_name(name);
        break;
    case DATABASE_INDEX_TYPE_PATH: {
        GString *path = db_entry_get_path(entry);
        text = g_filename_display_name(path->str);
        g_string_free(path, TRUE);
        path = NULL;
        break;
    }
    case DATABASE_INDEX_TYPE_FILETYPE: {
        text = fsearch_file_utils_get_file_type(name,
                                                db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER ? TRUE : FALSE);
        break;
    }
    case DATABASE_INDEX_TYPE_SIZE:
        text = fsearch_file_utils_get_size_formatted(db_entry_get_size(entry), config->show_base_2_units);
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME: {
        const time_t mtime = db_entry_get_mtime(entry);
        char mtime_formatted[100] = "";
        strftime(mtime_formatted,
                 sizeof(mtime_formatted),
                 "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
                 localtime(&mtime));
        text = g_strdup(mtime_formatted);
        break;
    }
    default:
        return NULL;
    }

    if (!text) {
        return NULL;
    }

    pango_layout_set_text(layout, text, -1);

    int layout_width = 0;
    pango_layout_get_pixel_size(layout, &layout_width, NULL);
    width -= layout_width;

    if (width < 0) {
        return text;
    }

    g_free(text);
    text = NULL;

    return NULL;
}

static void
fsearch_list_view_draw_row(cairo_t *cr,
                           GdkWindow *bin_window,
                           PangoLayout *layout,
                           GtkStyleContext *context,
                           GList *columns,
                           cairo_rectangle_int_t *rect,
                           GtkSortType sort_type,
                           uint32_t row,
                           gboolean row_selected,
                           gboolean row_focused,
                           gboolean right_to_left_text,
                           gpointer user_data) {
    if (!columns) {
        return;
    }

    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    const int icon_size = get_icon_size_for_height(rect->height - ROW_PADDING_X);

    DrawRowContext ctx = {};
    draw_row_ctx_init(row, sort_type, win, bin_window, icon_size, &ctx);

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
    uint32_t x = rect->x;
    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        if (!column->visible) {
            continue;
        }
        cairo_save(cr);
        cairo_rectangle(cr, x, rect->y, column->effective_width, rect->height);
        cairo_clip(cr);
        int dx = 0;
        int dw = 0;
        pango_layout_set_attributes(layout, NULL);
        switch (column->type) {
        case DATABASE_INDEX_TYPE_NAME: {
            if (config->show_listview_icons && ctx.icon_surface) {
                int x_icon = x;
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
            pango_layout_set_text(layout, ctx.path->str, ctx.path->len);
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

void
fsearch_results_sort_func(int sort_order, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    if (!win->db_view) {
        return;
    }
    win->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(win->listview));
    win->sort_order = sort_order;

    db_view_set_sort_order(win->db_view, win->sort_order);
}

static void
add_columns(FsearchListView *view, FsearchConfig *config) {
    bool restore = config->restore_column_config;
    FsearchListViewColumn *name_col = fsearch_list_view_column_new(DATABASE_INDEX_TYPE_NAME,
                                                                   "Name",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   TRUE,
                                                                   TRUE,
                                                                   restore ? config->name_column_width : 250);
    FsearchListViewColumn *path_col = fsearch_list_view_column_new(DATABASE_INDEX_TYPE_PATH,
                                                                   "Path",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_path_column : TRUE,
                                                                   FALSE,
                                                                   restore ? config->path_column_width : 250);
    FsearchListViewColumn *size_col = fsearch_list_view_column_new(DATABASE_INDEX_TYPE_SIZE,
                                                                   "Size",
                                                                   PANGO_ALIGN_RIGHT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_size_column : TRUE,
                                                                   FALSE,
                                                                   restore ? config->size_column_width : 75);
    FsearchListViewColumn *type_col = fsearch_list_view_column_new(DATABASE_INDEX_TYPE_FILETYPE,
                                                                   "Type",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   restore ? config->show_type_column : TRUE,
                                                                   FALSE,
                                                                   restore ? config->type_column_width : 100);
    FsearchListViewColumn *changed_col = fsearch_list_view_column_new(DATABASE_INDEX_TYPE_MODIFICATION_TIME,
                                                                      "Date Modified",
                                                                      PANGO_ALIGN_RIGHT,
                                                                      PANGO_ELLIPSIZE_END,
                                                                      restore ? config->show_modified_column : TRUE,
                                                                      FALSE,
                                                                      restore ? config->modified_column_width : 125);

    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), name_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), path_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), type_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), size_col);
    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), changed_col);
    fsearch_list_view_column_set_tooltip(
        type_col,
        _("Sorting by <b>Type</b> can be very slow with many results and it can't be aborted.\n\n"
          "This sort order is not persistent, it will be reset when the search term changes."));
    fsearch_list_view_column_set_emblem(type_col, "emblem-important-symbolic", TRUE);
}

static void
create_view_and_model(FsearchApplicationWindow *app) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(app));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(app->listview_scrolled_window));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->listview_scrolled_window));
    app->listview = GTK_WIDGET(fsearch_list_view_new(hadj, vadj));
    gtk_container_add(GTK_CONTAINER(app->listview_scrolled_window), GTK_WIDGET(app->listview));

    gtk_widget_show(app->listview);
    fsearch_list_view_set_query_tooltip_func(FSEARCH_LIST_VIEW(app->listview), fsearch_list_view_query_tooltip, app);
    fsearch_list_view_set_draw_row_func(FSEARCH_LIST_VIEW(app->listview), fsearch_list_view_draw_row, app);
    fsearch_list_view_set_row_data_func(FSEARCH_LIST_VIEW(app->listview), fsearch_list_view_get_entry_for_row, app);
    fsearch_list_view_set_sort_func(FSEARCH_LIST_VIEW(app->listview), fsearch_results_sort_func, app);
    fsearch_list_view_set_single_click_activate(FSEARCH_LIST_VIEW(app->listview), config->single_click_open);
    gtk_widget_set_has_tooltip(GTK_WIDGET(app->listview), config->enable_list_tooltips);

    add_columns(FSEARCH_LIST_VIEW(app->listview), config);

    g_signal_connect_object(app->listview, "row-popup", G_CALLBACK(on_fsearch_list_view_popup), app, G_CONNECT_AFTER);
    g_signal_connect_object(app->listview,
                            "selection-changed",
                            G_CALLBACK(on_fsearch_list_view_selection_changed),
                            app,
                            G_CONNECT_AFTER);
    g_signal_connect_object(app->listview,
                            "row-activated",
                            G_CALLBACK(on_fsearch_list_view_row_activated),
                            app,
                            G_CONNECT_AFTER);
    g_signal_connect(app->listview, "key-press-event", G_CALLBACK(on_listview_key_press_event), app);
}

static void
database_update_finished_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    fsearch_statusbar_set_query_text(FSEARCH_STATUSBAR(win->statusbar), "");

    fsearch_list_view_selection_clear(FSEARCH_LIST_VIEW(win->listview));

    hide_overlay(win, OVERLAY_DATABASE_LOADING);
    gtk_widget_show(win->popover_update_db);
    gtk_widget_hide(win->popover_cancel_update_db);

    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    uint32_t num_items = db_get_num_entries(db);

    if (!db || num_items == 0) {
        show_overlay(win, OVERLAY_DATABASE_EMPTY);
    }
    else {
        hide_overlay(win, OVERLAY_DATABASE_EMPTY);
    }

    db_view_unregister(win->db_view);
    db_view_register(db, win->db_view);

    db_unref(db);
}

static void
database_load_started_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    database_load_started(win);
}

static void
database_scan_started_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    database_scan_started(win);
}

static void
fsearch_application_window_init(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    self->statusbar = GTK_WIDGET(fsearch_statusbar_new());
    gtk_box_pack_end(GTK_BOX(self->main_box), self->statusbar, FALSE, TRUE, 0);

    fsearch_window_actions_init(self);
    create_view_and_model(self);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    g_signal_connect_object(app, "database-scan-started", G_CALLBACK(database_scan_started_cb), self, G_CONNECT_AFTER);
    g_signal_connect_object(app,
                            "database-update-finished",
                            G_CALLBACK(database_update_finished_cb),
                            self,
                            G_CONNECT_AFTER);
    g_signal_connect_object(app, "database-load-started", G_CALLBACK(database_load_started_cb), self, G_CONNECT_AFTER);

    GtkBuilder *builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/overlay.ui");

    // Overlay when no search results are found
    self->overlay_results_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_results_empty"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_results_empty);

    // Overlay when database is empty
    self->overlay_database_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_empty"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_database_empty);

    // Overlay when search query is empty
    self->overlay_query_empty = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_query_empty"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_query_empty);

    // Overlay when database is updating
    self->overlay_database_updating = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_updating"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_database_updating);

    // Overlay when database is loading
    self->overlay_database_loading = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_database_loading"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_database_loading);

    // Overlay when results are being sorted
    self->overlay_results_sorting = GTK_WIDGET(gtk_builder_get_object(builder, "overlay_results_sorting"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->overlay_results_sorting);

    g_object_unref(builder);
}

static void
on_filter_combobox_changed(GtkComboBox *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    const char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(win->filter_combobox));
    fsearch_statusbar_set_filter(FSEARCH_STATUSBAR(win->statusbar), active ? text : NULL);

    db_view_set_filter(win->db_view, get_active_filter(win));
}

static gboolean
on_search_entry_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    guint keyval;
    gdk_event_get_keyval(event, &keyval);
    if (keyval == GDK_KEY_Down) {
        gint cursor_idx = fsearch_list_view_get_cursor(FSEARCH_LIST_VIEW(win->listview));
        gtk_widget_grab_focus(win->listview);
        fsearch_list_view_set_cursor(FSEARCH_LIST_VIEW(win->listview), cursor_idx);
        return TRUE;
    }
    return FALSE;
}

static void
on_search_entry_activate(GtkButton *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    perform_search(win);
}

static gboolean
on_fsearch_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(widget);
    fsearch_application_window_prepare_shutdown(win);
    gtk_widget_destroy(widget);
    return TRUE;
}

static int
fsearch_window_db_view_changed_cb(gpointer data) {
    const guint win_id = GPOINTER_TO_UINT(data);
    FsearchApplicationWindow *win = get_window_for_id(win_id);

    if (!win) {
        return G_SOURCE_REMOVE;
    }

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    const uint32_t num_rows = db_view_get_num_entries(win->db_view);

    win->sort_order = db_view_get_sort_order(win->db_view);
    win->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(win->listview));
    fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(win->listview), num_rows, win->sort_order, win->sort_type);
    fsearch_window_actions_update(win);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gchar sb_text[100] = "";
    snprintf(sb_text, sizeof(sb_text), _("%'d Items"), num_rows);
    fsearch_statusbar_set_query_text(FSEARCH_STATUSBAR(win->statusbar), sb_text);

    if (text && text[0] == '\0' && config->hide_results_on_empty_search) {
        show_overlay(win, OVERLAY_QUERY_EMPTY);
    }
    else if (num_rows == 0) {
        show_overlay(win, OVERLAY_RESULTS_EMPTY);
    }
    else {
        hide_overlays(win);
    }
    return G_SOURCE_REMOVE;
}

static void
fsearch_window_db_view_changed(FsearchDatabaseView *view, gpointer user_data) {
    if (!user_data) {
        return;
    }
    g_idle_add(fsearch_window_db_view_changed_cb, user_data);
}

void
fsearch_application_window_added(FsearchApplicationWindow *win, FsearchApplication *app) {
    guint win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));

    if (win_id <= 0) {
        g_debug("[window_added] id = 0");
        return;
    }
    win->db_view = db_view_new(get_query_text(win),
                               get_query_flags(),
                               get_active_filter(win),
                               win->sort_order,
                               fsearch_window_db_view_changed,
                               fsearch_window_db_view_search_started,
                               fsearch_window_db_view_search_finished,
                               NULL,
                               NULL,
                               GUINT_TO_POINTER(win_id));
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    if (db) {
        db_view_register(db, win->db_view);
        db_unref(db);
    }
}

void
fsearch_application_window_removed(FsearchApplicationWindow *win, FsearchApplication *app) {
    if (win->db_view) {
        db_view_free(win->db_view);
        win->db_view = NULL;
    }
}

static void
fsearch_application_window_class_init(FsearchApplicationWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->constructed = fsearch_application_window_constructed;
    object_class->finalize = fsearch_application_window_finalize;

    gtk_widget_class_set_template_from_resource(widget_class, "/io/github/cboxdoerfer/fsearch/ui/fsearch.glade");
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, app_menu);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_combobox);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview_scrolled_window);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, menu_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_cancel_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_overlay);

    gtk_widget_class_bind_template_callback(widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_fsearch_window_delete_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_activate);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_key_press_event);
}

GtkEntry *
fsearch_application_window_get_search_entry(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return GTK_ENTRY(self->search_entry);
}

FsearchStatusbar *
fsearch_application_window_get_statusbar(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return FSEARCH_STATUSBAR(self->statusbar);
}

void
fsearch_application_window_update_database_label(FsearchApplicationWindow *self, const char *text) {
    fsearch_statusbar_set_database_indexing_state(FSEARCH_STATUSBAR(self->statusbar), text);
}

FsearchListView *
fsearch_application_window_get_listview(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return FSEARCH_LIST_VIEW(self->listview);
}

FsearchApplicationWindow *
fsearch_application_window_new(FsearchApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    return g_object_new(FSEARCH_APPLICATION_WINDOW_TYPE, "application", app, NULL);
}

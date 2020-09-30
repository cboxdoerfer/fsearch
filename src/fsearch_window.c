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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "array.h"
#include "database.h"
#include "database_search.h"
#include "debug.h"
#include "fsearch_config.h"
#include "fsearch_limits.h"
#include "fsearch_window.h"
#include "fsearch_window_actions.h"
#include "list_model.h"
#include "listview.h"
#include "listview_popup.h"
#include "ui_utils.h"
#include "utils.h"
#include <glib/gi18n.h>

struct _FsearchApplicationWindow {
    GtkApplicationWindow parent_instance;
    DatabaseSearch *search;
    DatabaseSearchResult *search_result;

    GtkWidget *app_menu;
    GtkWidget *database_box1;
    GtkWidget *database_box2;
    GtkWidget *database_icon;
    GtkWidget *database_label1;
    GtkWidget *database_label;
    GtkWidget *database_loading_label;
    GtkWidget *database_loading_overlay;
    GtkWidget *database_spinner;
    GtkWidget *database_stack;
    GtkWidget *database_updating_label;
    GtkWidget *database_updating_overlay;
    GtkWidget *empty_database_overlay;
    GtkWidget *empty_search_query_overlay;
    GtkWidget *filter_combobox;
    GtkWidget *filter_revealer;
    GtkWidget *headerbar;
    GtkWidget *headerbar_box;
    GtkWidget *listview;
    GtkWidget *match_case_revealer;
    GtkWidget *main_box;
    GtkWidget *menubar;
    GtkWidget *menu_box;
    GtkWidget *no_search_results_overlay;
    GtkWidget *num_files_label;
    GtkWidget *num_folders_label;
    GtkWidget *revealer;
    GtkWidget *scrolledwindow1;
    GtkWidget *popover_update_db;
    GtkWidget *popover_cancel_update_db;
    GtkWidget *search_box;
    GtkWidget *search_button;
    GtkWidget *search_button_revealer;
    GtkWidget *search_entry;
    GtkWidget *search_icon;
    GtkWidget *search_in_path_revealer;
    GtkWidget *search_label;
    GtkWidget *search_mode_revealer;
    GtkWidget *search_overlay;
    GtkWidget *statusbar;
    GtkWidget *statusbar_revealer;
    GtkWidget *statusbar_scan_label;
    GtkWidget *statusbar_scan_status_label;

    GtkTreeSelection *listview_selection;

    ListModel *list_model;

    FsearchQueryHighlight *query_highlight;
    int32_t num_searches_active;

    guint statusbar_timeout_id;

    GMutex mutex;
};

static gboolean
perform_search(FsearchApplicationWindow *win);

static void
init_statusbar(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    gtk_spinner_stop(GTK_SPINNER(self->database_spinner));

    gtk_stack_set_visible_child(GTK_STACK(self->database_stack), self->database_box2);
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);

    uint32_t num_items = 0;
    if (db) {
        num_items = db_get_num_entries(db);
        db_unref(db);
    }

    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("%'d Items"), num_items);
    gtk_label_set_text(GTK_LABEL(self->database_label), db_text);
}

static void
remove_model_from_list(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    g_object_ref(self->list_model); /* destroy store automatically with view */
    gtk_tree_view_set_model(GTK_TREE_VIEW(self->listview), NULL);
}

static void
apply_model_to_list(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    gtk_tree_view_set_model(GTK_TREE_VIEW(self->listview), GTK_TREE_MODEL(self->list_model));
}

gboolean
fsearch_application_window_update_search(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    perform_search(win);
    return FALSE;
}

void
fsearch_application_window_prepare_shutdown(gpointer self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    FsearchApplicationWindow *win = self;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gint width = 800;
    gint height = 800;
    gtk_window_get_size(GTK_WINDOW(self), &width, &height);
    config->window_width = width;
    config->window_height = height;

    gint sort_column_id = 0;
    GtkSortType order = GTK_SORT_ASCENDING;
    gtk_tree_sortable_get_sort_column_id(
        GTK_TREE_SORTABLE(win->list_model), &sort_column_id, &order);

    if (win->query_highlight) {
        fsearch_query_highlight_free(win->query_highlight);
        win->query_highlight = NULL;
    }
    if (config->sort_by) {
        g_free(config->sort_by);
        config->sort_by = NULL;
    }

    if (sort_column_id == SORT_ID_NAME) {
        config->sort_by = g_strdup("Name");
    }
    else if (sort_column_id == SORT_ID_PATH) {
        config->sort_by = g_strdup("Path");
    }
    else if (sort_column_id == SORT_ID_TYPE) {
        config->sort_by = g_strdup("Type");
    }
    else if (sort_column_id == SORT_ID_SIZE) {
        config->sort_by = g_strdup("Size");
    }
    else if (sort_column_id == SORT_ID_CHANGED) {
        config->sort_by = g_strdup("Date Modified");
    }
    else {
        config->sort_by = g_strdup("Name");
    }

    if (order == GTK_SORT_ASCENDING) {
        config->sort_ascending = true;
    }
    else {
        config->sort_ascending = false;
    }
}

void
fsearch_application_window_apply_model(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    apply_model_to_list(win);
}

void
fsearch_application_window_remove_model(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    remove_model_from_list(win);
}

void
fsearch_apply_menubar_config(FsearchApplicationWindow *win) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    gtk_window_set_titlebar(GTK_WINDOW(win), config->show_menubar ? NULL : win->headerbar);
    gtk_window_set_title(GTK_WINDOW(win), g_get_application_name());
    gtk_widget_set_visible(win->menubar, config->show_menubar);
    gtk_widget_set_visible(win->menu_box, config->show_menubar);
    gtk_widget_set_visible(win->headerbar, !config->show_menubar);

    if (config->show_menubar) {
        if (gtk_widget_get_parent(win->search_box) == win->headerbar_box) {
            gtk_container_remove(GTK_CONTAINER(win->headerbar_box), win->search_box);
            gtk_box_pack_start(GTK_BOX(win->menu_box), win->search_box, TRUE, TRUE, 0);
            gtk_box_reorder_child(GTK_BOX(win->menu_box), win->search_box, 0);
        }
    }
    else {
        if (gtk_widget_get_parent(win->search_box) == win->menu_box) {
            gtk_container_remove(GTK_CONTAINER(win->menu_box), win->search_box);
            gtk_box_pack_start(GTK_BOX(win->headerbar_box), win->search_box, TRUE, TRUE, 0);
            gtk_box_reorder_child(GTK_BOX(win->headerbar_box), win->search_box, 0);
        }
    }
    // ensure search entry still has focus after reordering the search_box
    gtk_widget_grab_focus(win->search_entry);
}

void
fsearch_window_apply_statusbar_revealer_config(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    GtkStyleContext *filter_style = gtk_widget_get_style_context(win->scrolledwindow1);
    if (!config->show_statusbar) {
        gtk_style_context_add_class(filter_style, "results_frame_last");
    }
    else {
        gtk_style_context_remove_class(filter_style, "results_frame_last");
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->statusbar_revealer), config->show_statusbar);
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
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_button_revealer),
                                  config->show_search_button);
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
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->match_case_revealer), config->match_case);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_mode_revealer), config->enable_regex);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_in_path_revealer),
                                  config->search_in_path);

    FsearchDatabase *db = fsearch_application_get_db(app);
    if (!db) {
        gtk_widget_show(self->empty_database_overlay);
        return;
    }

    uint32_t num_items = db_get_num_entries(db);

    if (!config->locations || num_items == 0) {
        gtk_widget_show(self->empty_database_overlay);
    }

    db_unref(db);
}

G_DEFINE_TYPE(FsearchApplicationWindow, fsearch_application_window, GTK_TYPE_APPLICATION_WINDOW)

static void
fsearch_application_window_constructed(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->constructed(object);

    self->num_searches_active = 0;
    self->search = NULL;
    self->search = db_search_new(fsearch_application_get_thread_pool(FSEARCH_APPLICATION_DEFAULT));
    g_mutex_init(&self->mutex);
    fsearch_window_apply_config(self);

    fsearch_apply_menubar_config(self);

    init_statusbar(self);
}

static void
fsearch_application_window_finalize(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    if (self->search) {
        db_search_free(self->search);
        self->search = NULL;
    }
    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->finalize(object);
}

typedef enum _FsearchOverlay {
    NO_SEARCH_RESULTS_OVERLAY,
    NO_SEARCH_QUERY_OVERLAY,
    NO_DATABASE_OVERLAY,
    DATABASE_UPDATING_OVERLAY,
    DATABASE_LOADING_OVERLAY,
} FsearchOverlay;

static void
hide_overlays(FsearchApplicationWindow *win) {
    gtk_widget_hide(win->no_search_results_overlay);
    gtk_widget_hide(win->empty_database_overlay);
    gtk_widget_hide(win->empty_search_query_overlay);
    gtk_widget_hide(win->database_updating_overlay);
    gtk_widget_hide(win->database_loading_overlay);
}

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay) {
    hide_overlays(win);

    switch (overlay) {
    case NO_SEARCH_RESULTS_OVERLAY:
        gtk_widget_show(win->no_search_results_overlay);
        break;
    case NO_DATABASE_OVERLAY:
        gtk_widget_show(win->empty_database_overlay);
        break;
    case NO_SEARCH_QUERY_OVERLAY:
        gtk_widget_show(win->empty_search_query_overlay);
        break;
    case DATABASE_UPDATING_OVERLAY:
        gtk_widget_show(win->database_updating_overlay);
        break;
    case DATABASE_LOADING_OVERLAY:
        gtk_widget_show(win->database_loading_overlay);
        break;
    }
}

static void
statusbar_remove_update_cb(FsearchApplicationWindow *win) {
    if (win->statusbar_timeout_id) {
        g_source_remove(win->statusbar_timeout_id);
        win->statusbar_timeout_id = 0;
    }
}
static void
statusbar_update(FsearchApplicationWindow *win, const char *text) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    statusbar_remove_update_cb(win);
    gtk_label_set_text(GTK_LABEL(win->search_label), text);
}

static gboolean
statusbar_set_query_status(gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    gtk_label_set_text(GTK_LABEL(win->search_label), _("Quering…"));
    win->statusbar_timeout_id = 0;
    return FALSE;
}

static void
statusbar_update_delayed(FsearchApplicationWindow *win, const char *text) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    statusbar_remove_update_cb(win);
    win->statusbar_timeout_id = g_timeout_add(200, statusbar_set_query_status, win);
}

static gboolean
search_cancelled_cb(gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    if (!win) {
        return FALSE;
    }
    win->num_searches_active--;
    return FALSE;
}

static gboolean
update_model_cb(gpointer user_data) {
    DatabaseSearchResult *result = user_data;
    FsearchApplicationWindow *win = result->cb_data;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    FsearchDatabase *db = fsearch_application_get_db(app);

    win->num_searches_active--;
    remove_model_from_list(win);
    db_search_results_clear(win->search);

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    uint32_t num_results = 0;
    if (db == result->db) {
        GPtrArray *results = result->results;
        if (results && results->len > 0) {
            list_model_set_results(win->list_model, results);
            win->search->results = results;
            win->search->num_folders = result->num_folders;
            win->search->num_files = result->num_files;
            num_results = results->len;
            list_model_update_sort(win->list_model);
            if (win->query_highlight) {
                fsearch_query_highlight_free(win->query_highlight);
                win->query_highlight = NULL;
            }
            win->query_highlight = fsearch_query_highlight_new(text,
                                                               config->enable_regex,
                                                               config->match_case,
                                                               config->auto_match_case,
                                                               config->auto_search_in_path,
                                                               config->search_in_path);
        }
        else {
            list_model_set_results(win->list_model, NULL);
            win->search->results = NULL;
            win->search->num_folders = 0;
            win->search->num_files = 0;
            num_results = 0;
        }
    }

    apply_model_to_list(win);
    gchar sb_text[100] = "";
    snprintf(sb_text, sizeof(sb_text), _("%'d Items"), num_results);
    statusbar_update(win, sb_text);

    if (text[0] == '\0' && config->hide_results_on_empty_search) {
        show_overlay(win, NO_SEARCH_QUERY_OVERLAY);
    }
    else if (num_results == 0) {
        show_overlay(win, NO_SEARCH_RESULTS_OVERLAY);
    }
    else {
        hide_overlays(win);
    }

    if (db) {
        db_unref(db);
    }
    if (result->db) {
        db_unref(result->db);
    }
    free(result);
    result = NULL;
    return FALSE;
}

void
fsearch_application_window_search_cancelled(void *data) {
    g_idle_add(search_cancelled_cb, data);
}

void
fsearch_application_window_update_results(void *data) {
    g_idle_add(update_model_cb, data);
}

static gboolean
perform_search(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    if (!win->search) {
        trace("[search] not set\n");
        return FALSE;
    }

    if (!config->locations) {
        show_overlay(win, NO_DATABASE_OVERLAY);
        return FALSE;
    }

    fsearch_application_state_lock(app);
    FsearchDatabase *db = fsearch_application_get_db(app);
    if (!db) {
        fsearch_application_state_unlock(app);
        return FALSE;
    }
    if (!db_try_lock(db)) {
        trace("[search] database locked\n");
        db_unref(db);
        fsearch_application_state_unlock(app);
        return FALSE;
    }

    win->num_searches_active++;

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    trace("[search] %s\n", text);
    FsearchFilter filter = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    uint32_t max_results = config->limit_results ? config->num_results : 0;
    FsearchQuery *q = fsearch_query_new(text,
                                        db,
                                        filter,
                                        fsearch_application_window_update_results,
                                        win,
                                        fsearch_application_window_search_cancelled,
                                        win,
                                        max_results,
                                        config->match_case,
                                        config->auto_match_case,
                                        config->enable_regex,
                                        config->auto_search_in_path,
                                        config->search_in_path,
                                        !config->hide_results_on_empty_search);
    db_unlock(db);
    statusbar_update_delayed(win, _("Quering…"));
    db_search_queue(win->search, q);
    fsearch_application_state_unlock(app);
    return FALSE;
}

typedef struct _count_results_ctx {
    uint32_t num_folders;
    uint32_t num_files;
} count_results_ctx;

static void
count_results_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
    count_results_ctx *ctx = (count_results_ctx *)data;
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (entry) {
        BTreeNode *node = db_search_entry_get_node(entry);
        if (node->is_dir) {
            ctx->num_folders++;
        }
        else {
            ctx->num_files++;
        }
    }
}

static gboolean
on_listview_popup_menu(GtkWidget *widget, gpointer user_data) {
    listview_popup_menu(widget, NULL);
    return TRUE;
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

static gboolean
on_listview_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    g_return_val_if_fail(user_data != NULL, FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    GdkWindow *window = gdk_event_get_window(event);
    if (G_UNLIKELY(window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget)))) {
        // clicked outside of list (e.g. column header)
        return FALSE;
    }

    GdkEventType type = gdk_event_get_event_type(event);
    if (type == GDK_BUTTON_PRESS) {
        if (gdk_event_triggers_context_menu((GdkEvent *)event)) {
            listview_popup_menu(widget, event);
            return TRUE;
        }
    }
    else if (type == GDK_2BUTTON_PRESS) {
        if (window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))) {
            // GtkTreeViewColumn *column = NULL;
            // GtkTreePath *path = NULL;
            // gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
            //                               event->x,
            //                               event->y,
            //                               &path,
            //                               &column,
            //                               NULL,
            //                               NULL);
            // if (path) {
            //    gtk_tree_path_free(path);
            //}
            return TRUE;
        }
    }

    return FALSE;
}

static void
on_listview_row_activated(GtkTreeView *tree_view,
                          GtkTreePath *path,
                          GtkTreeViewColumn *column,
                          gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path)) {
        return;
    }
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter.user_data;
    if (!entry) {
        return;
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    int launch_folder = false;
    if (config->double_click_path &&
        gtk_tree_view_column_get_sort_column_id(column) + 1 == LIST_MODEL_COL_PATH) {
        launch_folder = true;
    }

    BTreeNode *node = db_search_entry_get_node(entry);

    if (!launch_folder ? launch_node(node) : launch_node_path(node, config->folder_open_cmd)) {
        // open succeeded
        fsearch_window_action_after_file_open(true);
    }
    else {
        // open failed
        if ((config->action_after_file_open_keyboard || config->action_after_file_open_mouse) &&
            config->show_dialog_failed_opening) {
            gint response = ui_utils_run_gtk_dialog(GTK_WIDGET(self),
                                                    GTK_MESSAGE_WARNING,
                                                    GTK_BUTTONS_YES_NO,
                                                    _("Failed to open file"),
                                                    _("Do you want to keep the window open?"));
            if (response != GTK_RESPONSE_YES) {
                fsearch_window_action_after_file_open(false);
            }
        }
    }
}

static void
on_listview_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    fsearch_window_actions_update(self);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;
    if (self->search) {
        num_folders = db_search_get_num_folders(self->search);
        num_files = db_search_get_num_files(self->search);
    }
    if (!num_folders && !num_files) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->revealer), FALSE);
        return;
    }

    count_results_ctx ctx = {0, 0};
    gtk_tree_selection_selected_foreach(sel, (GtkTreeSelectionForeachFunc)count_results_cb, &ctx);

    if (!ctx.num_folders && !ctx.num_files) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->revealer), FALSE);
    }
    else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->revealer), TRUE);
        char text[100] = "";
        snprintf(text, sizeof(text), "%d/%d", ctx.num_folders, num_folders);
        gtk_label_set_text(GTK_LABEL(self->num_folders_label), text);
        snprintf(text, sizeof(text), "%d/%d", ctx.num_files, num_files);
        gtk_label_set_text(GTK_LABEL(self->num_files_label), text);
    }
}

static gboolean
toggle_action_on_2button_press(GdkEvent *event, const char *action, gpointer user_data) {
    guint button;
    gdk_event_get_button(event, &button);
    GdkEventType type = gdk_event_get_event_type(event);
    if (button != GDK_BUTTON_PRIMARY || type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    GActionGroup *group = G_ACTION_GROUP(win);
    GVariant *state = g_action_group_get_action_state(group, action);
    g_action_group_change_action_state(
        group, action, g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_variant_unref(state);
    return TRUE;
}

static gboolean
on_search_mode_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "search_mode", user_data);
}

static gboolean
on_search_in_path_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "search_in_path", user_data);
}

static gboolean
on_match_case_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "match_case", user_data);
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

    GtkTreeView *list = GTK_TREE_VIEW(app->listview);

    listview_remove_column(list, LIST_MODEL_COL_NAME);
    listview_add_column(
        list, LIST_MODEL_COL_NAME, config->name_column_width, config->name_column_pos, app);
    listview_remove_column(list, LIST_MODEL_COL_PATH);
    listview_add_column(
        list, LIST_MODEL_COL_PATH, config->path_column_width, config->path_column_pos, app);

    gtk_tree_view_set_activate_on_single_click(list, config->single_click_open);
}

static void
create_view_and_model(FsearchApplicationWindow *app) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(app));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    app->list_model = list_model_new();
    GtkTreeView *list = GTK_TREE_VIEW(app->listview);

    if (!config->restore_column_config) {
        listview_add_default_columns(list, app);
    }
    else {
        listview_add_column(
            list, LIST_MODEL_COL_NAME, config->name_column_width, config->name_column_pos, app);

        if (config->show_path_column) {
            listview_add_column(
                list, LIST_MODEL_COL_PATH, config->path_column_width, config->path_column_pos, app);
        }
        if (config->show_type_column) {
            listview_add_column(
                list, LIST_MODEL_COL_TYPE, config->type_column_width, config->type_column_pos, app);
        }
        if (config->show_size_column) {
            listview_add_column(
                list, LIST_MODEL_COL_SIZE, config->size_column_width, config->size_column_pos, app);
        }
        if (config->show_modified_column) {
            listview_add_column(list,
                                LIST_MODEL_COL_CHANGED,
                                config->modified_column_width,
                                config->modified_column_pos,
                                app);
        }
    }
    list_model_sort_init(app->list_model,
                         config->restore_sort_order ? config->sort_by : "Name",
                         config->restore_sort_order ? config->sort_ascending : true);

    gtk_tree_view_set_activate_on_single_click(list, config->single_click_open);

    gtk_tree_view_set_model(list, GTK_TREE_MODEL(app->list_model));
    g_object_unref(app->list_model); /* destroy store automatically with view */
}

static void
database_update_finished_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    statusbar_update(win, "");

    fsearch_application_window_update_search(win);

    hide_overlays(win);
    gtk_spinner_stop(GTK_SPINNER(win->database_spinner));
    gtk_widget_show(win->popover_update_db);
    gtk_widget_hide(win->popover_cancel_update_db);
    gtk_widget_hide(win->statusbar_scan_label);
    gtk_widget_hide(win->statusbar_scan_status_label);

    gtk_stack_set_visible_child(GTK_STACK(win->database_stack), win->database_box2);
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    uint32_t num_items = db_get_num_entries(db);

    if (!db || num_items == 0) {
        show_overlay(win, NO_DATABASE_OVERLAY);
    }

    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("%'d Items"), num_items);
    gtk_label_set_text(GTK_LABEL(win->database_label), db_text);

    time_t timestamp = db_get_timestamp(db);
    strftime(db_text,
             sizeof(db_text),
             _("Last Updated: %Y-%m-%d %H:%M"), //"%Y-%m-%d %H:%M",
             localtime(&timestamp));
    db_unref(db);
}

static void
database_load_started_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    gtk_stack_set_visible_child(GTK_STACK(win->database_stack), win->database_box1);
    gtk_spinner_start(GTK_SPINNER(win->database_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("Loading Database…"));
    gtk_label_set_text(GTK_LABEL(win->database_label1), db_text);

    show_overlay(win, DATABASE_LOADING_OVERLAY);
}

static void
database_update_started_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gtk_widget_hide(win->popover_update_db);
    gtk_widget_show(win->popover_cancel_update_db);

    if (config->show_indexing_status) {
        gtk_widget_show(win->statusbar_scan_label);
        gtk_widget_show(win->statusbar_scan_status_label);
    }
    gtk_stack_set_visible_child(GTK_STACK(win->database_stack), win->database_box1);
    gtk_spinner_start(GTK_SPINNER(win->database_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("Updating Database…"));
    gtk_label_set_text(GTK_LABEL(win->database_label1), db_text);
}

static void
fsearch_application_window_init(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    fsearch_window_actions_init(self);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/org/fsearch/fsearch/shared.css");
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    create_view_and_model(self);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    g_signal_connect_object(app,
                            "database-update-started",
                            G_CALLBACK(database_update_started_cb),
                            self,
                            G_CONNECT_AFTER);
    g_signal_connect_object(app,
                            "database-update-finished",
                            G_CALLBACK(database_update_finished_cb),
                            self,
                            G_CONNECT_AFTER);
    g_signal_connect_object(
        app, "database-load-started", G_CALLBACK(database_load_started_cb), self, G_CONNECT_AFTER);

    GtkBuilder *builder = gtk_builder_new_from_resource("/org/fsearch/fsearch/overlay.ui");
    GtkBuilder *menu_builder = gtk_builder_new_from_resource("/org/fsearch/fsearch/menus.ui");
    GMenuModel *menu_model =
        G_MENU_MODEL(gtk_builder_get_object(menu_builder, "fsearch_main_menu"));
    self->menubar = gtk_menu_bar_new_from_model(menu_model);
    gtk_widget_show(self->menubar);
    gtk_box_pack_start(GTK_BOX(self->main_box), self->menubar, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(self->main_box), self->menubar, 0);

    // Overlay when no search results are found
    self->no_search_results_overlay =
        GTK_WIDGET(gtk_builder_get_object(builder, "no_search_results"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->no_search_results_overlay);

    // Overlay when database is empty
    self->empty_database_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "empty_database"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->empty_database_overlay);

    // Overlay when search query is empty
    self->empty_search_query_overlay =
        GTK_WIDGET(gtk_builder_get_object(builder, "empty_search_query"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->empty_search_query_overlay);

    // Overlay when database is updating
    self->database_updating_overlay =
        GTK_WIDGET(gtk_builder_get_object(builder, "database_updating"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->database_updating_overlay);
    self->database_updating_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "database_updating_label"));

    // Overlay when database is loading
    self->database_loading_overlay =
        GTK_WIDGET(gtk_builder_get_object(builder, "database_loading"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->database_loading_overlay);
    self->database_loading_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "database_loading_label"));

    g_object_unref(builder);
    g_object_unref(menu_builder);
}

static void
on_filter_combobox_changed(GtkComboBox *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    perform_search(win);
}

static gboolean
on_search_entry_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    guint keyval;
    gdk_event_get_keyval(event, &keyval);
    if (keyval == GDK_KEY_Down) {
        GtkTreeIter iter = {};
        GtkTreePath *path = NULL;

        gtk_tree_view_get_cursor(GTK_TREE_VIEW(win->listview), &path, NULL);
        if (!path) {
            gtk_tree_model_get_iter_first(GTK_TREE_MODEL(win->list_model), &iter);
            path = gtk_tree_model_get_path(GTK_TREE_MODEL(win->list_model), &iter);
        }

        if (path) {
            gtk_widget_grab_focus(win->listview);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(win->listview), path, NULL, FALSE);
            gtk_tree_path_free(path);
        }
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
on_listview_query_tooltip(GtkWidget *widget,
                          gint x,
                          gint y,
                          gboolean keyboard_mode,
                          GtkTooltip *tooltip,
                          gpointer user_data) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    if (!config->enable_list_tooltips) {
        return FALSE;
    }
    gboolean ret_val = FALSE;

    GtkTreeModel *model = NULL;
    GtkTreePath *path = NULL;
    GtkTreeIter iter = {0};

    if (!gtk_tree_view_get_tooltip_context(
            GTK_TREE_VIEW(widget), &x, &y, keyboard_mode, &model, &path, &iter)) {
        return ret_val;
    }

    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter.user_data;
    if (entry) {
        BTreeNode *node = db_search_entry_get_node(entry);
        if (node) {
            char path_name[PATH_MAX] = "";
            btree_node_get_path_full(node, path_name, sizeof(path_name));
            gtk_tree_view_set_tooltip_row(GTK_TREE_VIEW(widget), tooltip, path);
            gtk_tooltip_set_text(tooltip, path_name);
            ret_val = TRUE;
        }
    }
    gtk_tree_path_free(path);
    return ret_val;
}

static gboolean
on_fsearch_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(widget);
    if (win->num_searches_active > 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   _("Background tasks are active."));
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            _("Closing the window isn't possible right now.\nPlease wait a "
              "moment and then try again."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        trace("[window] search is pending, window close blocked\n");
        return TRUE;
    }
    fsearch_application_window_prepare_shutdown(win);
    gtk_widget_destroy(widget);
    return TRUE;
}

static void
fsearch_application_window_class_init(FsearchApplicationWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->constructed = fsearch_application_window_constructed;
    object_class->finalize = fsearch_application_window_finalize;
    gtk_widget_class_set_template_from_resource(widget_class, "/org/fsearch/fsearch/fsearch.glade");
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_overlay);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, app_menu);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, statusbar_revealer);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, search_in_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_box);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, match_case_revealer);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, search_mode_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, search_button_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, menu_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_combobox);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, listview_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, num_folders_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, num_files_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_spinner);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_icon);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_box1);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_box2);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, database_label1);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_icon);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, scrolledwindow1);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, popover_cancel_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_update_db);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, statusbar_scan_label);
    gtk_widget_class_bind_template_child(
        widget_class, FsearchApplicationWindow, statusbar_scan_status_label);

    gtk_widget_class_bind_template_callback(widget_class, on_fsearch_window_delete_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_key_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_popup_menu);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_selection_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_row_activated);
    gtk_widget_class_bind_template_callback(widget_class, on_match_case_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class,
                                            on_search_in_path_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_mode_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_activate);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_key_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_query_tooltip);
}

GtkEntry *
fsearch_application_window_get_search_entry(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return GTK_ENTRY(self->search_entry);
}

void
fsearch_application_window_update_database_label(FsearchApplicationWindow *self, const char *text) {
    gtk_label_set_text(GTK_LABEL(self->statusbar_scan_status_label), text);
}

GtkWidget *
fsearch_application_window_get_search_in_path_revealer(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return self->search_in_path_revealer;
}

GtkWidget *
fsearch_application_window_get_match_case_revealer(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return self->match_case_revealer;
}

GtkWidget *
fsearch_application_window_get_search_mode_revealer(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return self->search_mode_revealer;
}

GtkTreeView *
fsearch_application_window_get_listview(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return GTK_TREE_VIEW(self->listview);
}

GtkTreeSelection *
fsearch_application_window_get_listview_selection(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    return self->listview_selection;
}

FsearchQueryHighlight *
fsearch_application_window_get_query_highlight(FsearchApplicationWindow *self) {
    if (self->query_highlight) {
        return self->query_highlight;
    }
    return NULL;
}

FsearchApplicationWindow *
fsearch_application_window_new(FsearchApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    return g_object_new(FSEARCH_APPLICATION_WINDOW_TYPE, "application", app, NULL);
}


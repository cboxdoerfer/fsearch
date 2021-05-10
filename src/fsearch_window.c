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
#include "fsearch_database_search.h"
#include "fsearch_file_utils.h"
#include "fsearch_limits.h"
#include "fsearch_list_view.h"
#include "fsearch_listview_popup.h"
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
    GtkWidget *match_case_revealer;
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
    GtkWidget *search_filter_label;
    GtkWidget *search_filter_revealer;
    GtkWidget *search_in_path_revealer;
    GtkWidget *search_mode_revealer;
    GtkWidget *search_overlay;
    GtkWidget *smart_case_revealer;
    GtkWidget *smart_path_revealer;
    GtkWidget *statusbar_database_stack;
    GtkWidget *statusbar_database_status_box;
    GtkWidget *statusbar_database_status_label;
    GtkWidget *statusbar_database_updating_box;
    GtkWidget *statusbar_database_updating_label;
    GtkWidget *statusbar_database_updating_spinner;
    GtkWidget *statusbar_revealer;
    GtkWidget *statusbar_scan_label;
    GtkWidget *statusbar_scan_status_label;
    GtkWidget *statusbar_search_label;
    GtkWidget *statusbar_selection_num_files_label;
    GtkWidget *statusbar_selection_num_folders_label;
    GtkWidget *statusbar_selection_revealer;

    DatabaseSearchResult *result;

    FsearchTaskQueue *task_queue;
    FsearchQueryHighlight *query_highlight;
    uint32_t query_id;

    FsearchDatabaseIndexType sort_order;
    GtkSortType sort_type;

    bool closing;

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

typedef struct {
    DynamicArrayCompareDataFunc compare_func;
    bool parallel_sort;

    FsearchDatabase *db;
    FsearchApplicationWindow *win;
} FsearchSortContext;

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

static void
fsearch_window_listview_set_empty(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    self->sort_order = fsearch_list_view_get_sort_order(FSEARCH_LIST_VIEW(self->listview));
    self->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(self->listview));
    fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->listview), 0, self->sort_order, self->sort_type);
}

static void
fsearch_window_apply_results(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    self->sort_type = fsearch_list_view_get_sort_type(FSEARCH_LIST_VIEW(self->listview));

    if (self->result && self->result->files) {
        self->sort_order = self->result->query->sort_order;
        fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->listview),
                                       db_search_result_get_num_entries(self->result),
                                       self->sort_order,
                                       self->sort_type);
    }
    else {
        fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->listview), 0, self->sort_order, self->sort_type);
    }
    fsearch_window_actions_update(self);
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

static void
fsearch_window_sort_array(DynamicArray *array, DynamicArrayCompareDataFunc sort_func, bool parallel_sort) {
    if (!array) {
        return;
    }
    if (parallel_sort) {
        darray_sort_multi_threaded(array, (DynamicArrayCompareFunc)sort_func);
    }
    else {
        darray_sort(array, (DynamicArrayCompareFunc)sort_func);
    }
}

static gpointer
fsearch_window_sort_task(gpointer data, GCancellable *cancellable) {
    FsearchSortContext *ctx = data;

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    if (ctx->win->result) {
        g_idle_add(fsearch_window_sort_started, ctx->win);
        fsearch_window_sort_array(ctx->win->result->folders, ctx->compare_func, ctx->parallel_sort);
        fsearch_window_sort_array(ctx->win->result->files, ctx->compare_func, ctx->parallel_sort);
        g_idle_add(fsearch_window_sort_finished, ctx->win);
    }

    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_destroy(timer);
    timer = NULL;

    g_debug("[sort] finished in %2.fms", seconds * 1000);

    return NULL;
}

static void
fsearch_window_sort_task_cancelled(FsearchTask *task, gpointer data) {
    FsearchSortContext *ctx = data;
    db_unref(ctx->db);

    free(ctx);
    ctx = NULL;

    fsearch_task_free(task);
    task = NULL;
}

static void
fsearch_window_sort_task_finished(FsearchTask *task, gpointer result, gpointer data) {
    fsearch_window_sort_task_cancelled(task, data);
}

static void *
fsearch_list_view_get_entry_for_row(int row_idx, GtkSortType sort_type, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    if (!win || !win->result) {
        return NULL;
    }

    if (sort_type == GTK_SORT_DESCENDING) {
        row_idx = (int)db_search_result_get_num_entries(win->result) - row_idx - 1;
    }

    return db_search_result_get_entry(win->result, row_idx);
}

static void
database_load_started(FsearchApplicationWindow *win) {
    gtk_stack_set_visible_child(GTK_STACK(win->statusbar_database_stack), win->statusbar_database_updating_box);
    gtk_spinner_start(GTK_SPINNER(win->statusbar_database_updating_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("Loading Database…"));
    gtk_label_set_text(GTK_LABEL(win->statusbar_database_updating_label), db_text);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    if (config->show_indexing_status) {
        gtk_widget_show(win->statusbar_scan_label);
        gtk_widget_show(win->statusbar_scan_status_label);
    }

    show_overlay(win, OVERLAY_DATABASE_LOADING);
}

static void
database_scan_started(FsearchApplicationWindow *win) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gtk_widget_hide(win->popover_update_db);
    gtk_widget_show(win->popover_cancel_update_db);

    if (config->show_indexing_status) {
        gtk_widget_show(win->statusbar_scan_label);
        gtk_widget_show(win->statusbar_scan_status_label);
    }
    gtk_stack_set_visible_child(GTK_STACK(win->statusbar_database_stack), win->statusbar_database_updating_box);
    gtk_spinner_start(GTK_SPINNER(win->statusbar_database_updating_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("Updating Database…"));
    gtk_label_set_text(GTK_LABEL(win->statusbar_database_updating_label), db_text);
}

static void
init_statusbar(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    gtk_spinner_stop(GTK_SPINNER(self->statusbar_database_updating_spinner));

    gtk_stack_set_visible_child(GTK_STACK(self->statusbar_database_stack), self->statusbar_database_status_box);
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);

    uint32_t num_items = 0;
    if (db) {
        num_items = db_get_num_entries(db);
        db_unref(db);
    }

    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("%'d Items"), num_items);
    gtk_label_set_text(GTK_LABEL(self->statusbar_database_status_label), db_text);
}

gboolean
fsearch_application_window_update_search(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    perform_search(win);
    return FALSE;
}

void
fsearch_application_window_prepare_close(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    self->closing = true;
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
fsearch_apply_menubar_config(FsearchApplicationWindow *win) {
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
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->match_case_revealer), config->match_case);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_mode_revealer), config->enable_regex);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_in_path_revealer), config->search_in_path);

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

    self->task_queue = fsearch_task_queue_new("fsearch_task_thread");
    fsearch_window_apply_config(self);

    fsearch_apply_menubar_config(self);

    init_statusbar(self);

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

    if (self->task_queue) {
        fsearch_task_queue_free(self->task_queue);
        self->task_queue = NULL;
    }
    if (self->result) {
        db_search_result_unref(self->result);
        self->result = NULL;
    }
    if (self->query_highlight) {
        fsearch_query_highlight_free(self->query_highlight);
        self->query_highlight = NULL;
    }

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
    gtk_label_set_text(GTK_LABEL(win->statusbar_search_label), text);
}

static gboolean
statusbar_set_query_status(gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    gtk_label_set_text(GTK_LABEL(win->statusbar_search_label), _("Querying…"));
    win->statusbar_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void
statusbar_update_delayed(FsearchApplicationWindow *win, const char *text) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    statusbar_remove_update_cb(win);
    win->statusbar_timeout_id = g_timeout_add(200, statusbar_set_query_status, win);
}

static void
fsearch_window_apply_search_result(FsearchApplicationWindow *win, DatabaseSearchResult *search_result) {
    fsearch_window_listview_set_empty(win);

    if (win->query_highlight) {
        fsearch_query_highlight_free(win->query_highlight);
        win->query_highlight = NULL;
    }

    if (win->result) {
        db_search_result_unref(win->result);
        win->result = NULL;
    }
    win->result = search_result;

    const gchar *text = search_result->query->text;

    uint32_t num_results = db_search_result_get_num_entries(search_result);
    if (num_results > 0) {
        win->query_highlight = fsearch_query_highlight_new(text, search_result->query->flags);
    }
    else {
        num_results = 0;
    }

    fsearch_window_apply_results(win);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    gchar sb_text[100] = "";
    snprintf(sb_text, sizeof(sb_text), _("%'d Items"), num_results);
    statusbar_update(win, sb_text);

    if (text && text[0] == '\0' && config->hide_results_on_empty_search) {
        show_overlay(win, OVERLAY_QUERY_EMPTY);
    }
    else if (num_results == 0) {
        show_overlay(win, OVERLAY_RESULTS_EMPTY);
    }
    else {
        hide_overlays(win);
    }
}

static gboolean
fsearch_window_search_apply_result(gpointer user_data) {
    DatabaseSearchResult *search_result = user_data;
    FsearchQuery *query = search_result->query;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchApplicationWindow *win =
        FSEARCH_WINDOW_WINDOW(gtk_application_get_window_by_id(GTK_APPLICATION(app), query->window_id));

    FsearchDatabase *queried_db = query->data;
    FsearchDatabase *active_db = fsearch_application_get_db(app);

    if (win && !win->closing && queried_db && active_db && queried_db == active_db) {
        // if they window is about to be destroyed we don't want to update its widgets
        // they might already be gone anyway
        fsearch_window_apply_search_result(win, search_result);
    }
    else {
        db_search_result_unref(search_result);
        search_result = NULL;
    }

    if (active_db) {
        db_unref(active_db);
        active_db = NULL;
    }

    if (queried_db) {
        db_unref(queried_db);
        queried_db = NULL;
    }

    return G_SOURCE_REMOVE;
}

gboolean
fsearch_window_search_cancelled(gpointer data) {
    FsearchQuery *query = data;
    FsearchDatabase *db = query->data;

    if (db) {
        db_unref(db);
        db = NULL;
    }

    fsearch_query_free(query);
    query = NULL;

    return G_SOURCE_REMOVE;
}

uint32_t
fsearch_application_window_get_num_results(FsearchApplicationWindow *self) {
    if (self->result) {
        return db_search_result_get_num_entries(self->result);
    }
    return 0;
}

static void
fsearch_window_search_task_cancelled(FsearchTask *task, gpointer data) {
    if (data) {
        g_idle_add(fsearch_window_search_cancelled, data);
    }

    fsearch_task_free(task);
    task = NULL;
}

static void
fsearch_window_search_task_finished(FsearchTask *task, gpointer result, gpointer data) {
    if (result) {
        g_idle_add(fsearch_window_search_apply_result, result);
    }
    else if (data) {
        g_idle_add(fsearch_window_search_cancelled, data);
    }

    fsearch_task_free(task);
    task = NULL;
}

static void
perform_search(FsearchApplicationWindow *win) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    const uint32_t win_id = gtk_application_window_get_id(GTK_APPLICATION_WINDOW(win));
    if (win_id <= 0) {
        // window isn't attached to the application yet, so searching won't have any effect
        return;
    }

    if (!win->task_queue) {
        g_debug("[win] abort search, queue is missing");
        return;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    if (!config->indexes) {
        show_overlay(win, OVERLAY_DATABASE_EMPTY);
        return;
    }

    fsearch_application_state_lock(app);
    FsearchDatabase *db = fsearch_application_get_db(app);
    if (!db || db_get_num_entries(db) < 1 || !db_try_lock(db)) {
        goto search_failed;
    }

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    g_debug("[query %d.%d] started with query '%s'", win_id, win->query_id, text);

    FsearchQueryFlags flags = {.enable_regex = config->enable_regex,
                               .match_case = config->match_case,
                               .auto_match_case = config->auto_match_case,
                               .search_in_path = config->search_in_path,
                               .auto_search_in_path = config->auto_search_in_path};

    uint32_t active_filter = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    GList *filter_element = g_list_nth(fsearch_application_get_filters(app), active_filter);
    FsearchFilter *filter = filter_element->data;

    DynamicArray *files = db_get_files_sorted(db, win->sort_order);
    DynamicArray *folders = db_get_folders_sorted(db, win->sort_order);
    FsearchDatabaseIndexType sort_order = fsearch_list_view_get_sort_order(FSEARCH_LIST_VIEW(win->listview));
    if (!files || !folders) {
        g_debug("no fast sort for type: %d\n", sort_order);
        files = db_get_files(db);
        folders = db_get_folders(db);
        sort_order = DATABASE_INDEX_TYPE_NAME;
    }

    FsearchQuery *q = fsearch_query_new(text,
                                        files,
                                        folders,
                                        db_get_num_folders(db),
                                        db_get_num_files(db),
                                        sort_order,
                                        filter,
                                        fsearch_application_get_thread_pool(app),
                                        flags,
                                        win->query_id++,
                                        win_id,
                                        !config->hide_results_on_empty_search,
                                        db);

    db_unlock(db);
    statusbar_update_delayed(win, _("Querying…"));
    db_search_queue(win->task_queue, q, fsearch_window_search_task_finished, fsearch_window_search_task_cancelled);

    bool reveal_smart_case = false;
    bool reveal_smart_path = false;
    if (!fs_str_is_empty(text)) {
        bool has_separator = strchr(text, G_DIR_SEPARATOR) ? 1 : 0;
        bool has_upper_text = fs_str_has_upper(text) ? 1 : 0;
        reveal_smart_case = config->auto_match_case && !config->match_case && has_upper_text;
        reveal_smart_path = config->auto_search_in_path && !config->search_in_path && has_separator;
    }

    gtk_revealer_set_reveal_child(GTK_REVEALER(win->smart_case_revealer), reveal_smart_case);
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->smart_path_revealer), reveal_smart_path);

    fsearch_application_state_unlock(app);
    return;

search_failed:
    if (db) {
        db_unref(db);
    }
    fsearch_application_state_unlock(app);
    return;
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
    if (self->result) {
        num_folders = db_search_result_get_num_folders(self->result);
        num_files = db_search_result_get_num_files(self->result);
    }
    if (!num_folders && !num_files) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->statusbar_selection_revealer), FALSE);
        return;
    }

    count_results_ctx ctx = {0, 0};
    fsearch_list_view_selection_for_each(view, (GHFunc)count_results_cb, &ctx);

    if (!ctx.num_folders && !ctx.num_files) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->statusbar_selection_revealer), FALSE);
    }
    else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->statusbar_selection_revealer), TRUE);
        char text[100] = "";
        snprintf(text, sizeof(text), "%d/%d", ctx.num_folders, num_folders);
        gtk_label_set_text(GTK_LABEL(self->statusbar_selection_num_folders_label), text);
        snprintf(text, sizeof(text), "%d/%d", ctx.num_files, num_files);
        gtk_label_set_text(GTK_LABEL(self->statusbar_selection_num_files_label), text);
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
    g_action_group_change_action_state(group, action, g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_variant_unref(state);
    return TRUE;
}

static gboolean
on_search_filter_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    guint button;
    gdk_event_get_button(event, &button);
    GdkEventType type = gdk_event_get_event_type(event);
    if (button != GDK_BUTTON_PRIMARY || type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));
    gtk_combo_box_set_active(GTK_COMBO_BOX(win->filter_combobox), 0);
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
    ctx->name_attr = win->query_highlight ? fsearch_query_highlight_match(win->query_highlight, name) : NULL;

    ctx->path = db_entry_get_path(entry);
    if (win->query_highlight
        && ((win->query_highlight->has_separator && win->query_highlight->flags.auto_search_in_path)
            || win->query_highlight->flags.search_in_path)) {
        ctx->path_attr = fsearch_query_highlight_match(win->query_highlight, ctx->path->str);
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
    if (!win->result) {
        return;
    }

    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    if (!db) {
        return;
    }

    if (fsearch_query_matches_everything(win->result->query)) {
        // we're matching everything, so if the database has the entries already sorted we don't need
        // to sort again
        DynamicArray *files = db_get_files_sorted(db, sort_order);
        DynamicArray *folders = db_get_folders_sorted(db, sort_order);
        if (files && folders) {
            darray_free(win->result->files);
            darray_free(win->result->folders);
            win->result->files = darray_copy(files);
            win->result->folders = darray_copy(folders);
            win->sort_order = sort_order;
            db_unref(db);
            return;
        }
    }

    bool parallel_sort = true;

    g_debug("[sort] started: %d", sort_order);
    DynamicArrayCompareFunc func = NULL;
    switch (sort_order) {
    case DATABASE_INDEX_TYPE_NAME:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_name;
        break;
    case DATABASE_INDEX_TYPE_PATH:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_path;
        break;
    case DATABASE_INDEX_TYPE_SIZE:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_size;
        break;
    case DATABASE_INDEX_TYPE_FILETYPE:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_type;
        parallel_sort = false;
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_modification_time;
        break;
    default:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_position;
    }

    FsearchSortContext *ctx = calloc(1, sizeof(FsearchSortContext));
    g_assert(ctx != NULL);

    ctx->win = win;
    ctx->db = db;
    ctx->compare_func = (DynamicArrayCompareDataFunc)func;
    ctx->parallel_sort = parallel_sort;

    FsearchTask *task = fsearch_task_new(1,
                                         fsearch_window_sort_task,
                                         fsearch_window_sort_task_finished,
                                         fsearch_window_sort_task_cancelled,
                                         ctx);
    fsearch_task_queue(win->task_queue, task, FSEARCH_TASK_CLEAR_SAME_ID);

    win->sort_order = sort_order;
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

    statusbar_update(win, "");

    fsearch_list_view_selection_clear(FSEARCH_LIST_VIEW(win->listview));
    fsearch_application_window_update_search(win);

    hide_overlay(win, OVERLAY_DATABASE_LOADING);
    gtk_spinner_stop(GTK_SPINNER(win->statusbar_database_updating_spinner));
    gtk_widget_show(win->popover_update_db);
    gtk_widget_hide(win->popover_cancel_update_db);
    gtk_widget_hide(win->statusbar_scan_label);
    gtk_widget_hide(win->statusbar_scan_status_label);

    gtk_stack_set_visible_child(GTK_STACK(win->statusbar_database_stack), win->statusbar_database_status_box);
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    uint32_t num_items = db_get_num_entries(db);

    if (!db || num_items == 0) {
        show_overlay(win, OVERLAY_DATABASE_EMPTY);
    }
    else {
        hide_overlay(win, OVERLAY_DATABASE_EMPTY);
    }

    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("%'d Items"), num_items);
    gtk_label_set_text(GTK_LABEL(win->statusbar_database_status_label), db_text);

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
    gtk_label_set_text(GTK_LABEL(win->search_filter_label), text);

    gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_filter_revealer), active);

    perform_search(win);
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

static void
fsearch_application_window_class_init(FsearchApplicationWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->constructed = fsearch_application_window_constructed;
    object_class->finalize = fsearch_application_window_finalize;

    gtk_widget_class_set_template_from_resource(widget_class, "/io/github/cboxdoerfer/fsearch/ui/fsearch.glade");
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, app_menu);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_updating_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_status_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_status_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_updating_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_updating_spinner);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_database_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_combobox);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview_scrolled_window);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, match_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, menu_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_num_files_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_num_folders_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_cancel_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_filter_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_in_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_search_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_mode_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_overlay);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, smart_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, smart_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_scan_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_scan_status_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_revealer);

    gtk_widget_class_bind_template_callback(widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_fsearch_window_delete_event);
    gtk_widget_class_bind_template_callback(widget_class, on_match_case_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_activate);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_search_entry_key_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_filter_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_in_path_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_mode_label_button_press_event);
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
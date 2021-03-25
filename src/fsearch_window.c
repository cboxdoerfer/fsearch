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
#include "fsearch_list_view.h"
#include "fsearch_timer.h"
#include "fsearch_window.h"
#include "fsearch_window_actions.h"
#include "listview_popup.h"
#include "string_utils.h"
#include "ui_utils.h"
#include "utils.h"
#include <glib/gi18n.h>
#include <math.h>

struct _FsearchApplicationWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *app_menu;
    GtkWidget *statusbar_database_updating_box;
    GtkWidget *statusbar_database_status_box;
    GtkWidget *statusbar_database_updating_label;
    GtkWidget *statusbar_database_status_label;
    GtkWidget *database_loading_label;
    GtkWidget *database_loading_overlay;
    GtkWidget *statusbar_database_updating_spinner;
    GtkWidget *statusbar_database_stack;
    GtkWidget *database_updating_label;
    GtkWidget *database_updating_overlay;
    GtkWidget *empty_database_overlay;
    GtkWidget *empty_search_query_overlay;
    GtkWidget *filter_combobox;
    GtkWidget *filter_revealer;
    GtkWidget *headerbar;
    GtkWidget *headerbar_box;
    GtkWidget *listview;
    GtkWidget *new_listview;
    GtkWidget *match_case_revealer;
    GtkWidget *main_box;
    GtkWidget *menu_box;
    GtkWidget *no_search_results_overlay;
    GtkWidget *statusbar_selection_num_files_label;
    GtkWidget *statusbar_selection_num_folders_label;
    GtkWidget *statusbar_selection_revealer;
    GtkWidget *listview_scrolled_window;
    GtkWidget *popover_update_db;
    GtkWidget *popover_cancel_update_db;
    GtkWidget *search_box;
    GtkWidget *search_button;
    GtkWidget *search_button_revealer;
    GtkWidget *search_entry;
    GtkWidget *statusbar_search_icon;
    GtkWidget *search_in_path_revealer;
    GtkWidget *smart_path_revealer;
    GtkWidget *smart_case_revealer;
    GtkWidget *statusbar_search_label;
    GtkWidget *search_filter_revealer;
    GtkWidget *search_filter_label;
    GtkWidget *search_mode_revealer;
    GtkWidget *search_overlay;
    GtkWidget *statusbar;
    GtkWidget *statusbar_revealer;
    GtkWidget *statusbar_scan_label;
    GtkWidget *statusbar_scan_status_label;

    GtkTreeSelection *listview_selection;

    DatabaseSearch *search;
    DatabaseSearchResult *result;

    FsearchListViewColumnType sort_order;
    FsearchQueryHighlight *query_highlight;

    bool closing;

    guint statusbar_timeout_id;
};

typedef enum _FsearchOverlay {
    NO_SEARCH_RESULTS_OVERLAY,
    NO_SEARCH_QUERY_OVERLAY,
    NO_DATABASE_OVERLAY,
    DATABASE_UPDATING_OVERLAY,
    DATABASE_LOADING_OVERLAY,
} FsearchOverlay;

static gboolean
perform_search(FsearchApplicationWindow *win);

static void
show_overlay(FsearchApplicationWindow *win, FsearchOverlay overlay);

static void *
fsearch_list_view_get_node_for_row(int row_idx, GtkSortType sort_type, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    if (!win || !win->result || !win->result->entries || !darray_get_num_items(win->result->entries)
        || darray_get_num_items(win->result->entries) <= row_idx) {
        return NULL;
    }

    if (sort_type == GTK_SORT_DESCENDING) {
        row_idx = darray_get_num_items(win->result->entries) - row_idx - 1;
    }
    return darray_get_item(win->result->entries, row_idx);
}

static void
database_load_started(FsearchApplicationWindow *win) {
    gtk_stack_set_visible_child(GTK_STACK(win->statusbar_database_stack), win->statusbar_database_updating_box);
    gtk_spinner_start(GTK_SPINNER(win->statusbar_database_updating_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("Loading Database…"));
    gtk_label_set_text(GTK_LABEL(win->statusbar_database_updating_label), db_text);

    show_overlay(win, DATABASE_LOADING_OVERLAY);
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

static void
remove_model_from_list(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->new_listview), 0);
}

static void
apply_model_to_list(FsearchApplicationWindow *self) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    if (self->result && self->result->entries) {
        fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->new_listview),
                                       darray_get_num_items(self->result->entries));
    }
    else {
        fsearch_list_view_set_num_rows(FSEARCH_LIST_VIEW(self->new_listview), 0);
    }
    fsearch_window_actions_update(self);
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
    FsearchApplicationWindow *win = self;
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

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;

    self->search = NULL;
    self->search = db_search_new(fsearch_application_get_thread_pool(app));
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
    case FSEARCH_DATABASE_STATE_IDLE:
        break;
    default:
        break;
    }
}

static void
fsearch_application_window_finalize(GObject *object) {
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));

    if (self->result) {
        db_search_result_free(self->result);
        self->result = NULL;
    }
    if (self->query_highlight) {
        fsearch_query_highlight_free(self->query_highlight);
        self->query_highlight = NULL;
    }
    if (self->search) {
        db_search_free(self->search);
        self->search = NULL;
    }

    G_OBJECT_CLASS(fsearch_application_window_parent_class)->finalize(object);
}

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

static gboolean
search_cancelled_cb(gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    if (!win) {
        return G_SOURCE_REMOVE;
    }
    g_object_unref(win);
    return G_SOURCE_REMOVE;
}

typedef struct {
    FsearchDatabase *db;
    FsearchApplicationWindow *win;
} FsearchQueryContext;

static void
fsearch_window_apply_search_result(FsearchApplicationWindow *win,
                                   DatabaseSearchResult *search_result,
                                   FsearchQueryContext *query_ctx) {
    remove_model_from_list(win);

    if (win->query_highlight) {
        fsearch_query_highlight_free(win->query_highlight);
        win->query_highlight = NULL;
    }

    if (win->result) {
        db_search_result_free(win->result);
        win->result = NULL;
    }
    win->result = search_result;

    const gchar *text = search_result->query->text;
    uint32_t num_results = 0;

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchDatabase *db = fsearch_application_get_db(app);
    if (db == query_ctx->db) {
        DynamicArray *entries = search_result->entries;
        if (entries && darray_get_num_items(entries) > 0) {
            num_results = darray_get_num_items(entries);
            win->query_highlight = fsearch_query_highlight_new(text, search_result->query->flags);
        }
        else {
            num_results = 0;
        }
    }

    db_unref(db);
    db = NULL;

    apply_model_to_list(win);

    FsearchConfig *config = fsearch_application_get_config(app);

    gchar sb_text[100] = "";
    if (config->limit_results && num_results == config->num_results) {
        snprintf(sb_text, sizeof(sb_text), _("≥%'d Items"), num_results);
    }
    else {
        snprintf(sb_text, sizeof(sb_text), _("%'d Items"), num_results);
    }
    statusbar_update(win, sb_text);

    if (text && text[0] == '\0' && config->hide_results_on_empty_search) {
        show_overlay(win, NO_SEARCH_QUERY_OVERLAY);
    }
    else if (num_results == 0) {
        show_overlay(win, NO_SEARCH_RESULTS_OVERLAY);
    }
    else {
        hide_overlays(win);
    }
}

static gboolean
fsearch_window_search_successful_cb(gpointer user_data) {
    DatabaseSearchResult *search_result = user_data;
    FsearchQueryContext *query_ctx = search_result->cb_data;
    FsearchApplicationWindow *win = query_ctx->win;

    if (!win->closing) {
        // if they window is about to be destroyed we don't want to update its widgets
        // they might already be gone anyway
        fsearch_window_apply_search_result(win, search_result, query_ctx);
    }

    g_object_unref(win);

    if (query_ctx->db) {
        db_unref(query_ctx->db);
    }
    free(query_ctx);
    query_ctx = NULL;

    return G_SOURCE_REMOVE;
}

void
fsearch_application_window_search_cancelled(void *data) {
    FsearchQueryContext *ctx = data;
    if (!ctx) {
        return;
    }

    g_idle_add(search_cancelled_cb, ctx->win);

    if (ctx->db) {
        db_unref(ctx->db);
        ctx->db = NULL;
    }
    free(ctx);
}

void
fsearch_application_window_update_results(void *data) {
    g_idle_add(fsearch_window_search_successful_cb, data);
}

uint32_t
fsearch_application_window_get_num_results(FsearchApplicationWindow *self) {
    if (self->result && self->result->entries) {
        return darray_get_num_items(self->result->entries);
    }
    return 0;
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
    if (!db || !db_get_entries(db)) {
        fsearch_application_state_unlock(app);
        return FALSE;
    }
    if (!db_try_lock(db)) {
        trace("[search] database locked\n");
        db_unref(db);
        fsearch_application_state_unlock(app);
        return FALSE;
    }

    g_object_ref(win);

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(win->search_entry));
    trace("[search] %s\n", text);
    uint32_t max_results = config->limit_results ? config->num_results : 0;

    FsearchQueryFlags flags = {.enable_regex = config->enable_regex,
                               .match_case = config->match_case,
                               .auto_match_case = config->auto_match_case,
                               .search_in_path = config->search_in_path,
                               .auto_search_in_path = config->auto_search_in_path};

    uint32_t active_filter = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    GList *filter_element = g_list_nth(fsearch_application_get_filters(app), active_filter);
    FsearchFilter *filter = filter_element->data;

    FsearchQueryContext *ctx = calloc(1, sizeof(FsearchQueryContext));
    ctx->win = win;
    ctx->db = db;

    FsearchQuery *q = fsearch_query_new(text,
                                        db_get_entries(db),
                                        filter,
                                        fsearch_application_window_update_results,
                                        ctx,
                                        fsearch_application_window_search_cancelled,
                                        ctx,
                                        max_results,
                                        flags,
                                        !config->hide_results_on_empty_search);

    db_unlock(db);
    statusbar_update_delayed(win, _("Querying…"));
    db_search_queue(win->search, q);

    bool reveal_smart_case = false;
    bool reveal_smart_path = false;
    if (!fs_str_is_empty(text)) {
        bool has_separator = strchr(text, '/') ? 1 : 0;
        bool has_upper_text = fs_str_has_upper(text) ? 1 : 0;
        reveal_smart_case = config->auto_match_case && !config->match_case && has_upper_text;
        reveal_smart_path = config->auto_search_in_path && !config->search_in_path && has_separator;
    }

    gtk_revealer_set_reveal_child(GTK_REVEALER(win->smart_case_revealer), reveal_smart_case);
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->smart_path_revealer), reveal_smart_path);

    fsearch_application_state_unlock(app);
    return FALSE;
}

typedef struct _count_results_ctx {
    uint32_t num_folders;
    uint32_t num_files;
} count_results_ctx;

static void
count_results_cb(gpointer key, gpointer value, count_results_ctx *ctx) {
    if (value) {
        BTreeNode *node = value;
        if (node->is_dir) {
            ctx->num_folders++;
        }
        else {
            ctx->num_files++;
        }
    }
}

static gboolean
on_fsearch_list_view_popup(FsearchListView *view, int row_idx, GtkSortType sort_type, gpointer user_data) {
    BTreeNode *node = fsearch_list_view_get_node_for_row(row_idx, sort_type, user_data);
    listview_popup_menu(user_data, node);
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
                                   FsearchListViewColumnType col,
                                   int row_idx,
                                   GtkSortType sort_type,
                                   gpointer user_data) {
    FsearchApplicationWindow *self = user_data;
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    int launch_folder = false;
    if (config->double_click_path && col == FSEARCH_LIST_VIEW_COLUMN_PATH) {
        launch_folder = true;
    }

    BTreeNode *node = fsearch_list_view_get_node_for_row(row_idx, sort_type, self);
    if (!node) {
        return;
    }

    if (!launch_folder ? launch_node(node) : launch_node_path(node, config->folder_open_cmd)) {
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
        num_folders = self->result->num_folders;
        num_files = self->result->num_files;
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

void
fsearch_window_listview_block_selection_changed(FsearchApplicationWindow *self, gboolean block) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    // if (block) {
    //    g_signal_handlers_block_by_func(self->listview_selection, on_listview_selection_changed, self);
    //}
    // else {
    //    g_signal_handlers_unblock_by_func(self->listview_selection, on_listview_selection_changed, self);
    //}
}

void
fsearch_window_listview_selection_changed(FsearchApplicationWindow *self) {

    g_assert(FSEARCH_WINDOW_IS_WINDOW(self));
    // on_listview_selection_changed(self->listview_selection, self);
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

    // GtkTreeView *list = GTK_TREE_VIEW(app->listview);

    // listview_remove_column(list, LIST_MODEL_COL_NAME);
    // listview_add_column(list, LIST_MODEL_COL_NAME, config->name_column_width, config->name_column_pos, app);
    // listview_remove_column(list, LIST_MODEL_COL_PATH);
    // listview_add_column(list, LIST_MODEL_COL_PATH, config->path_column_width, config->path_column_pos, app);

    // gtk_tree_view_set_activate_on_single_click(list, config->single_click_open);
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
draw_row_ctx_init(BTreeNode *node,
                  FsearchApplicationWindow *win,
                  GdkWindow *bin_window,
                  int icon_size,
                  DrawRowContext *ctx) {
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    ctx->display_name = g_filename_display_name(node->name);
    ctx->name_attr = fsearch_query_highlight_match(win->query_highlight, node->name);

    char path_raw[PATH_MAX] = "";
    btree_node_get_path(node, path_raw, sizeof(path_raw));

    ctx->path = g_string_new(path_raw);
    ctx->path_attr = fsearch_query_highlight_match(win->query_highlight, path_raw);

    ctx->full_path = g_string_new_len(ctx->path->str, ctx->path->len);
    g_string_append_c(ctx->full_path, '/');
    g_string_append(ctx->full_path, node->name);

    ctx->type = get_file_type(node, ctx->full_path->str);

    ctx->icon_surface =
        config->show_listview_icons
            ? get_icon_surface(bin_window, ctx->full_path->str, icon_size, gtk_widget_get_scale_factor(GTK_WIDGET(win)))
            : NULL;

    if (!node->is_dir) {
        FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
        if (config->show_base_2_units) {
            ctx->size = g_format_size_full(node->size, G_FORMAT_SIZE_IEC_UNITS);
        }
        else {
            ctx->size = g_format_size_full(node->size, G_FORMAT_SIZE_DEFAULT);
        }
    }
    else {
        char buffer[100] = "";
        uint32_t num_children = btree_node_n_children(node);
        if (num_children == 1) {
            snprintf(buffer, sizeof(buffer), "%d Item", num_children);
        }
        else {
            snprintf(buffer, sizeof(buffer), "%d Items", num_children);
        }
        ctx->size = g_strdup(buffer);
    }

    strftime(ctx->time,
             100,
             "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime(&node->mtime));
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
                           gpointer user_data) {
    if (!columns) {
        return;
    }

    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    BTreeNode *node = fsearch_list_view_get_node_for_row(row, sort_type, win);
    if (!node) {
        return;
    }
    if (!node->name) {
        return;
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    DrawRowContext ctx = {};

    int icon_size = get_icon_size_for_height(rect->height - ROW_PADDING_X);

    draw_row_ctx_init(node, win, bin_window, icon_size, &ctx);

    GtkStateFlags flags = gtk_style_context_get_state(context);
    if (row_selected) {
        flags |= GTK_STATE_FLAG_SELECTED;
    }
    if (row_focused) {
        flags |= GTK_STATE_FLAG_FOCUSED;
    }

    gtk_style_context_save(context);
    gtk_style_context_set_state(context, flags);

    uint32_t x = rect->x;
    gtk_render_background(context, cr, x, rect->y, rect->width, rect->height);

    for (GList *col = columns; col != NULL; col = col->next) {
        FsearchListViewColumn *column = col->data;
        cairo_save(cr);
        cairo_rectangle(cr, x, rect->y, column->effective_width, rect->height);
        cairo_clip(cr);
        int dx = 0;
        pango_layout_set_attributes(layout, NULL);
        switch (column->type) {
        case FSEARCH_LIST_VIEW_COLUMN_NAME: {
            if (config->show_listview_icons && ctx.icon_surface) {
                gtk_render_icon_surface(context,
                                        cr,
                                        ctx.icon_surface,
                                        x + ROW_PADDING_X,
                                        rect->y + floor((rect->height - icon_size) / 2.0));
                dx += icon_size + 2 * ROW_PADDING_X;
            }
            pango_layout_set_attributes(layout, ctx.name_attr);
            pango_layout_set_text(layout, ctx.display_name, -1);
        } break;
        case FSEARCH_LIST_VIEW_COLUMN_PATH:
            pango_layout_set_attributes(layout, ctx.path_attr);
            pango_layout_set_text(layout, ctx.path->str, ctx.path->len);
            break;
        case FSEARCH_LIST_VIEW_COLUMN_SIZE:
            pango_layout_set_text(layout, ctx.size, -1);
            break;
        case FSEARCH_LIST_VIEW_COLUMN_TYPE:
            pango_layout_set_text(layout, ctx.type, -1);
            break;
        case FSEARCH_LIST_VIEW_COLUMN_CHANGED:
            pango_layout_set_text(layout, ctx.time, -1);
            break;
        default:
            pango_layout_set_text(layout, "Unkown column", -1);
        }

        pango_layout_set_width(layout, (column->effective_width - 2 * ROW_PADDING_X - dx) * PANGO_SCALE);
        pango_layout_set_alignment(layout, column->alignment);
        pango_layout_set_ellipsize(layout, column->ellipsize_mode);
        gtk_render_layout(context, cr, x + ROW_PADDING_X + dx, rect->y + ROW_PADDING_Y, layout);
        x += column->effective_width;
        cairo_restore(cr);
    }
    gtk_style_context_restore(context);

    draw_row_ctx_free(&ctx);
}

static gint
compare_func(BTreeNode **a, BTreeNode **b, FsearchApplicationWindow *win) {
    return compare_nodes(win->sort_order, *a, *b);
}

void
fsearch_results_sort_func(FsearchListViewColumnType sort_order, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_WINDOW_WINDOW(user_data);
    win->sort_order = sort_order;

    GTimer *timer = fsearch_timer_start();
    trace("[sort] started: %d\n", sort_order);
    darray_sort_with_data(win->result->entries, (DynamicArrayCompareDataFunc)compare_func, win);
    fsearch_timer_stop(timer, "[sort] finished in %2.fms\n");
}

static void
add_columns(FsearchListView *view, FsearchConfig *config) {
    bool restore = config->restore_column_config;
    FsearchListViewColumn *name_col = fsearch_list_view_column_new(FSEARCH_LIST_VIEW_COLUMN_NAME,
                                                                   "Name",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   TRUE,
                                                                   restore ? config->name_column_width : 250);
    FsearchListViewColumn *path_col = fsearch_list_view_column_new(FSEARCH_LIST_VIEW_COLUMN_PATH,
                                                                   "Path",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   FALSE,
                                                                   restore ? config->path_column_width : 250);
    FsearchListViewColumn *size_col = fsearch_list_view_column_new(FSEARCH_LIST_VIEW_COLUMN_SIZE,
                                                                   "Size",
                                                                   PANGO_ALIGN_RIGHT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   FALSE,
                                                                   restore ? config->size_column_width : 75);
    FsearchListViewColumn *type_col = fsearch_list_view_column_new(FSEARCH_LIST_VIEW_COLUMN_TYPE,
                                                                   "Type",
                                                                   PANGO_ALIGN_LEFT,
                                                                   PANGO_ELLIPSIZE_END,
                                                                   FALSE,
                                                                   restore ? config->type_column_width : 100);
    FsearchListViewColumn *changed_col = fsearch_list_view_column_new(FSEARCH_LIST_VIEW_COLUMN_CHANGED,
                                                                      "Modified Time",
                                                                      PANGO_ALIGN_RIGHT,
                                                                      PANGO_ELLIPSIZE_END,
                                                                      FALSE,
                                                                      restore ? config->modified_column_width : 125);

    fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), name_col);
    if (config->show_path_column) {
        fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), path_col);
    }
    if (config->show_type_column) {
        fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), type_col);
    }
    if (config->show_size_column) {
        fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), size_col);
    }
    if (config->show_modified_column) {
        fsearch_list_view_append_column(FSEARCH_LIST_VIEW(view), changed_col);
    }
}

static void
create_view_and_model(FsearchApplicationWindow *app) {
    g_assert(FSEARCH_WINDOW_IS_WINDOW(app));

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);

    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(app->listview_scrolled_window));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->listview_scrolled_window));
    app->new_listview = GTK_WIDGET(fsearch_list_view_new(hadj, vadj));
    gtk_container_add(GTK_CONTAINER(app->listview_scrolled_window), GTK_WIDGET(app->new_listview));

    gtk_widget_show(app->new_listview);
    fsearch_list_view_set_draw_row_func(FSEARCH_LIST_VIEW(app->new_listview), fsearch_list_view_draw_row, app);
    fsearch_list_view_set_row_data_func(FSEARCH_LIST_VIEW(app->new_listview), fsearch_list_view_get_node_for_row, app);
    fsearch_list_view_set_sort_func(FSEARCH_LIST_VIEW(app->new_listview), fsearch_results_sort_func, app);

    add_columns(FSEARCH_LIST_VIEW(app->new_listview), config);

    g_signal_connect_object(app->new_listview,
                            "row-popup",
                            G_CALLBACK(on_fsearch_list_view_popup),
                            app,
                            G_CONNECT_AFTER);
    g_signal_connect_object(app->new_listview,
                            "selection-changed",
                            G_CALLBACK(on_fsearch_list_view_selection_changed),
                            app,
                            G_CONNECT_AFTER);
    g_signal_connect_object(app->new_listview,
                            "row-activated",
                            G_CALLBACK(on_fsearch_list_view_row_activated),
                            app,
                            G_CONNECT_AFTER);

    // app->list_model = list_model_new();
    // GtkTreeView *list = GTK_TREE_VIEW(app->listview);

    // if (!config->restore_column_config) {
    //    listview_add_default_columns(list, config, app);
    //}
    // else {
    //    listview_add_column(list, LIST_MODEL_COL_NAME, config->name_column_width, config->name_column_pos,
    //    app);

    //    if (config->show_path_column) {
    //        listview_add_column(list, LIST_MODEL_COL_PATH, config->path_column_width, config->path_column_pos,
    //        app);
    //    }
    //    if (config->show_type_column) {
    //        listview_add_column(list, LIST_MODEL_COL_TYPE, config->type_column_width, config->type_column_pos,
    //        app);
    //    }
    //    if (config->show_size_column) {
    //        listview_add_column(list, LIST_MODEL_COL_SIZE, config->size_column_width, config->size_column_pos,
    //        app);
    //    }
    //    if (config->show_modified_column) {
    //        listview_add_column(list,
    //                            LIST_MODEL_COL_CHANGED,
    //                            config->modified_column_width,
    //                            config->modified_column_pos,
    //                            app);
    //    }
    //}
    // list_model_sort_init(app->list_model,
    //                     config->restore_sort_order ? config->sort_by : "Name",
    //                     config->restore_sort_order ? config->sort_ascending : true);

    // gtk_tree_view_set_activate_on_single_click(list, config->single_click_open);

    // gtk_tree_view_set_model(list, GTK_TREE_MODEL(app->list_model));
    // g_object_unref(app->list_model); /* destroy store automatically with view */
}

static void
database_update_finished_cb(gpointer data, gpointer user_data) {
    FsearchApplicationWindow *win = (FsearchApplicationWindow *)user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    statusbar_update(win, "");

    fsearch_list_view_selection_clear(FSEARCH_LIST_VIEW(win->new_listview));
    fsearch_application_window_update_search(win);

    hide_overlays(win);
    gtk_spinner_stop(GTK_SPINNER(win->statusbar_database_updating_spinner));
    gtk_widget_show(win->popover_update_db);
    gtk_widget_hide(win->popover_cancel_update_db);
    gtk_widget_hide(win->statusbar_scan_label);
    gtk_widget_hide(win->statusbar_scan_status_label);

    gtk_stack_set_visible_child(GTK_STACK(win->statusbar_database_stack), win->statusbar_database_status_box);
    FsearchDatabase *db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
    uint32_t num_items = db_get_num_entries(db);

    if (!db || num_items == 0) {
        show_overlay(win, NO_DATABASE_OVERLAY);
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
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/io/github/cboxdoerfer/fsearch/ui/shared.css");
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

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
    self->no_search_results_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "no_search_results"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->no_search_results_overlay);

    // Overlay when database is empty
    self->empty_database_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "empty_database"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->empty_database_overlay);

    // Overlay when search query is empty
    self->empty_search_query_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "empty_search_query"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->empty_search_query_overlay);

    // Overlay when database is updating
    self->database_updating_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "database_updating"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->database_updating_overlay);
    self->database_updating_label = GTK_WIDGET(gtk_builder_get_object(builder, "database_updating_label"));

    // Overlay when database is loading
    self->database_loading_overlay = GTK_WIDGET(gtk_builder_get_object(builder, "database_loading"));
    gtk_overlay_add_overlay(GTK_OVERLAY(self->search_overlay), self->database_loading_overlay);
    self->database_loading_label = GTK_WIDGET(gtk_builder_get_object(builder, "database_loading_label"));

    g_object_unref(builder);
}

static void
on_filter_combobox_changed(GtkComboBox *widget, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    g_assert(FSEARCH_WINDOW_IS_WINDOW(win));

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(win->filter_combobox));
    const char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(win->filter_combobox));
    gtk_label_set_text(GTK_LABEL(win->search_filter_label), text);

    if (active == 0) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_filter_revealer), FALSE);
    }
    else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(win->search_filter_revealer), TRUE);
    }

    perform_search(win);
}

static gboolean
on_search_entry_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchApplicationWindow *win = user_data;
    guint keyval;
    gdk_event_get_keyval(event, &keyval);
    if (keyval == GDK_KEY_Down) {
        gint cursor_idx = fsearch_list_view_get_cursor(FSEARCH_LIST_VIEW(win->new_listview));
        gtk_widget_grab_focus(win->new_listview);
        fsearch_list_view_set_cursor(FSEARCH_LIST_VIEW(win->new_listview), cursor_idx);
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

    if (!gtk_tree_view_get_tooltip_context(GTK_TREE_VIEW(widget), &x, &y, keyboard_mode, &model, &path, &iter)) {
        return ret_val;
    }

    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter.user_data;
    if (entry) {
        BTreeNode *node = db_search_entry_get_node(entry);
        if (node) {
            char path_name[PATH_MAX] = "";
            btree_node_get_path_full(node, path_name, sizeof(path_name));
            char *display_name = g_filename_display_name(path_name);
            if (display_name) {
                gtk_tree_view_set_tooltip_row(GTK_TREE_VIEW(widget), tooltip, path);
                gtk_tooltip_set_text(tooltip, display_name);
                g_free(display_name);
                display_name = NULL;
                ret_val = TRUE;
            }
        }
    }
    gtk_tree_path_free(path);
    return ret_val;
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
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, headerbar_box);
    // gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview_scrolled_window);
    // gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, listview_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, main_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, match_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, menu_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_num_files_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_num_folders_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_cancel_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, popover_update_db);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_button_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_filter_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_search_icon);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_in_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_search_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_mode_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, search_overlay);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, smart_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, smart_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_scan_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_scan_status_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchApplicationWindow, statusbar_selection_revealer);

    gtk_widget_class_bind_template_callback(widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_fsearch_window_delete_event);
    // gtk_widget_class_bind_template_callback(widget_class, on_listview_key_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_listview_query_tooltip);
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
    return FSEARCH_LIST_VIEW(self->new_listview);
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


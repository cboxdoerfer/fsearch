/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

#include <linux/limits.h>
#include "fsearch_window.h"
#include "fsearch_window_actions.h"
#include "list_model.h"
#include "database.h"
#include "iconstore.h"
#include "config.h"
#include "utils.h"
#include "array.h"
#include "database_search.h"
#include "listview.h"
#include "debug.h"

struct _FsearchApplicationWindow {
    GtkApplicationWindow parent_instance;
    DatabaseSearch *search;

    GtkWidget *database_updating_overlay;
    GtkWidget *database_updating_label;
    GtkWidget *empty_search_query_overlay;
    GtkWidget *no_search_results_overlay;
    GtkWidget *empty_database_overlay;
    GtkWidget *menubar;
    GtkWidget *search_overlay;
    GtkWidget *statusbar;
    GtkWidget *search_mode_revealer;
    GtkWidget *match_case_revealer;
    GtkWidget *search_in_path_revealer;
    GtkWidget *search_button;
    GtkWidget *search_entry;
    GtkWidget *filter_combobox;
    GtkWidget *listview;
    GtkTreeSelection *listview_selection;
    GtkWidget *selection_toggle_button;
    GtkWidget *selection_popover;
    GtkWidget *num_folders_label;
    GtkWidget *num_files_label;
    GtkWidget *database_toggle_button;
    GtkWidget *database_stack;
    GtkWidget *database_box1;
    GtkWidget *database_box2;
    GtkWidget *database_icon;
    GtkWidget *database_spinner;
    GtkWidget *database_label;
    GtkWidget *database_label1;
    GtkWidget *database_popover;
    GtkWidget *search_label;
    GtkWidget *search_icon;
    GtkWidget *revealer;
    GtkWidget *scrolledwindow1;

    ListModel *list_model;

    GMutex mutex;
};

static gboolean
perform_search (FsearchApplicationWindow *win);

static void
init_statusbar (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));

    gtk_spinner_stop (GTK_SPINNER (self->database_spinner));

    gtk_stack_set_visible_child (GTK_STACK (self->database_stack), self->database_box2);
    Database *db = fsearch_application_get_db (FSEARCH_APPLICATION_DEFAULT);

    uint32_t num_items = 0;
    if (db) {
        num_items = db_get_num_entries (db);
    }

    gchar db_text[100] = "";
    snprintf (db_text, sizeof (db_text), "%'d Items", num_items);
    gtk_label_set_text (GTK_LABEL (self->database_label), db_text);
}

static void
remove_model_from_list (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    g_object_ref(self->list_model); /* destroy store automatically with view */
    gtk_tree_view_set_model (GTK_TREE_VIEW (self->listview), NULL);

}

static void
apply_model_to_list (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    gtk_tree_view_set_model (GTK_TREE_VIEW (self->listview),
                             GTK_TREE_MODEL (self->list_model));

}

gboolean
fsearch_application_window_update_search (gpointer window)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (window));
    FsearchApplicationWindow *win = window;
    perform_search (win);
    return FALSE;
}

void
fsearch_application_window_prepare_shutdown (gpointer self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config (app);

    gint width = 800;
    gint height = 800;
    gtk_window_get_size (GTK_WINDOW (self), &width, &height);
    config->window_width = width;
    config->window_height = height;
}

void
fsearch_application_window_apply_model (gpointer window)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (window));
    FsearchApplicationWindow *win = window;
    apply_model_to_list (win);
}

void
fsearch_application_window_remove_model (gpointer window)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (window));
    FsearchApplicationWindow *win = window;
    remove_model_from_list (win);
}

static void
fsearch_window_apply_config (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config (app);

    if (config->restore_window_size) {
        gtk_window_set_default_size (GTK_WINDOW (self),
                                     config->window_width,
                                     config->window_height);
    }
    gtk_widget_set_visible (self->menubar, config->show_menubar);
    gtk_widget_set_visible (self->statusbar, config->show_statusbar);
    gtk_widget_set_visible (self->filter_combobox, config->show_filter);
    gtk_widget_set_visible (self->search_button, config->show_search_button);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->match_case_revealer), config->match_case);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->search_mode_revealer), config->enable_regex);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->search_in_path_revealer), config->search_in_path);


    if (!config->locations) {
        gtk_widget_show (self->empty_database_overlay);
    }
}

G_DEFINE_TYPE (FsearchApplicationWindow, fsearch_application_window, GTK_TYPE_APPLICATION_WINDOW)

static void
fsearch_application_window_constructed (GObject *object)
{
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));

    G_OBJECT_CLASS (fsearch_application_window_parent_class)->constructed (object);

    self->search = NULL;
    g_mutex_init (&self->mutex);
    fsearch_window_apply_config (self);
    fsearch_window_actions_init (self);
    init_statusbar (self);
}

static void
fsearch_application_window_finalize (GObject *object)
{
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)object;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));

    if (self->search) {
        db_search_free (self->search);
        self->search = NULL;
    }
    g_mutex_clear (&self->mutex);


    G_OBJECT_CLASS (fsearch_application_window_parent_class)->finalize (object);
}

static void
reset_sort_order (FsearchApplicationWindow *win)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    GList *list = gtk_tree_view_get_columns (GTK_TREE_VIEW (win->listview));
    GList *l;
    for (l = list; l != NULL; l = l->next)
    {
        GtkTreeViewColumn *col = GTK_TREE_VIEW_COLUMN (l->data);
        if (l == list) {
            gtk_tree_view_column_set_sort_order (col, GTK_SORT_ASCENDING);
            gtk_tree_view_column_set_sort_indicator (col, TRUE);
            gtk_tree_view_column_set_sort_column_id (col, SORT_ID_NAME);
        }
        else {
            gtk_tree_view_column_set_sort_order (col, GTK_SORT_ASCENDING);
            gtk_tree_view_column_set_sort_indicator (col, FALSE);
        }
    }
    g_list_free (list);
}

typedef enum {
    NO_SEARCH_RESULTS_OVERLAY,
    NO_SEARCH_QUERY_OVERLAY,
    NO_DATABASE_OVERLAY,
    DATABASE_UPDATING_OVERLAY,
    N_FSEARCH_OVERLAYS
} FsearchOverlay;

static void
hide_overlays (FsearchApplicationWindow *win)
{
    gtk_widget_hide (win->no_search_results_overlay);
    gtk_widget_hide (win->empty_database_overlay);
    gtk_widget_hide (win->empty_search_query_overlay);
    gtk_widget_hide (win->database_updating_overlay);
}

static void
show_overlay (FsearchApplicationWindow *win, FsearchOverlay overlay)
{
    hide_overlays (win);

    switch (overlay) {
        case NO_SEARCH_RESULTS_OVERLAY:
            gtk_widget_show (win->no_search_results_overlay);
            break;
        case NO_DATABASE_OVERLAY:
            gtk_widget_show (win->empty_database_overlay);
            break;
        case NO_SEARCH_QUERY_OVERLAY:
            gtk_widget_show (win->empty_search_query_overlay);
            break;
        case DATABASE_UPDATING_OVERLAY:
            gtk_widget_show (win->database_updating_overlay);
            break;
        default:
            return;
    }
}

static void
update_statusbar (FsearchApplicationWindow *win, const char *text)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    gtk_label_set_text (GTK_LABEL (win->search_label), text);
}

gboolean
update_model_cb (gpointer user_data)
{

    DatabaseSearchResult *result = user_data;
    FsearchApplicationWindow *win = result->cb_data;
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config (app);

    remove_model_from_list (win);
    db_search_results_clear (win->search);

    uint32_t num_results = 0;
    GPtrArray *results = result->results;
    if (results) {
        list_set_results (win->list_model, results);
        win->search->results = results;
        num_results = results->len;
    }
    else {
        list_set_results (win->list_model, NULL);
        win->search->results = NULL;
        num_results = 0;
    }

    apply_model_to_list (win);
    gchar sb_text[100] = "";
    snprintf (sb_text, sizeof (sb_text), "%'d Items", num_results);
    update_statusbar (win, sb_text);
    reset_sort_order (win);

    const gchar *text = gtk_entry_get_text (GTK_ENTRY (win->search_entry));
    if (text[0] == '\0' && config->hide_results_on_empty_search) {
        show_overlay (win, NO_SEARCH_QUERY_OVERLAY);
    }
    else if (num_results == 0) {
        show_overlay (win, NO_SEARCH_RESULTS_OVERLAY);
    }
    else {
        hide_overlays (win);
    }

    free (result);
    result = NULL;
    return FALSE;
}

void
fsearch_application_window_update_results (void *data)
{
    g_idle_add (update_model_cb, data);
}

static gboolean
perform_search (FsearchApplicationWindow *win)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config (app);

    if (!config->locations) {
        show_overlay (win, NO_DATABASE_OVERLAY);
        return FALSE;
    }

    Database *db = fsearch_application_get_db (app);
    if (!db_try_lock (db)) {
        trace ("search: database locked\n");
        return FALSE;
    }
    //g_mutex_lock (&win->mutex);

    const gchar *text = gtk_entry_get_text (GTK_ENTRY (win->search_entry));
    FsearchFilter filter = gtk_combo_box_get_active (GTK_COMBO_BOX (win->filter_combobox));
    uint32_t max_results = config->limit_results ? config->num_results : 0;
    if (win->search) {
        db_search_update (win->search,
                          db_get_entries (db),
                          db_get_num_entries (db),
                          max_results,
                          filter,
                          text,
                          config->hide_results_on_empty_search,
                          config->match_case,
                          config->enable_regex,
                          config->auto_search_in_path,
                          config->search_in_path);
    }
    else {
        win->search = db_search_new (fsearch_application_get_thread_pool (app),
                                     db_get_entries (db),
                                     db_get_num_entries (db),
                                     max_results,
                                     filter,
                                     text,
                                     config->hide_results_on_empty_search,
                                     config->match_case,
                                     config->enable_regex,
                                     config->auto_search_in_path,
                                     config->search_in_path);
    }
    db_perform_search (win->search, fsearch_application_window_update_results, win);
    db_unlock (db);
    return FALSE;
}

typedef struct {
    uint32_t num_folders;
    uint32_t num_files;
} count_results_ctx;

static void
count_results_cb (GtkTreeModel *model,
                  GtkTreePath *path,
                  GtkTreeIter *iter,
                  gpointer data)
{
    count_results_ctx *ctx = (count_results_ctx *)data;
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    if (entry) {
        BTreeNode *node = db_search_entry_get_node (entry);
        if (node->is_dir) {
            ctx->num_folders++;
        }
        else {
            ctx->num_files++;
        }
    }
}

static gboolean
on_listview_popup_menu (GtkWidget *widget,
                        gpointer   user_data)
{
    //printf("popup menu launch\n");
    return FALSE;
}

static gboolean
on_listview_key_press_event (GtkWidget *widget,
                             GdkEventKey  *event,
                             gpointer   user_data)
{
    if (event->state & GDK_CONTROL_MASK) {
        if ((event->keyval == GDK_KEY_Return)
            || (event->keyval == GDK_KEY_KP_Enter)) {
            GActionGroup *group = gtk_widget_get_action_group (GTK_WIDGET (user_data), "win");
            g_action_group_activate_action (group, "open_folder", NULL);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
on_listview_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    //printf("popup menu\n");
    g_return_val_if_fail (user_data != NULL, FALSE);
    g_return_val_if_fail (event != NULL, FALSE);

    if (G_UNLIKELY (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget)))) {
        // clicked outside of list (e.g. column header)
        return FALSE;
    }

    if (event->type == GDK_BUTTON_PRESS)
    {
        if (event->button == GDK_BUTTON_SECONDARY)
        {
            GtkTreeView *view = GTK_TREE_VIEW (widget);

            GtkTreePath *path = NULL;
            GtkTreeSelection *selection = gtk_tree_view_get_selection (view);

            if ((event->state & gtk_accelerator_get_default_mod_mask ()) == 0
                && !gtk_tree_view_get_path_at_pos (view,
                                                   event->x,
                                                   event->y,
                                                   &path,
                                                   NULL,
                                                   NULL,
                                                   NULL))
            {
                // clicked empty area
                gtk_tree_selection_unselect_all (selection);
                return FALSE;
            }

            if (!path) {
                return FALSE;
            }

            if (!gtk_tree_selection_path_is_selected (selection, path)) {
                gtk_tree_selection_unselect_all (selection);
                gtk_tree_selection_select_path (selection, path);
            }

            GtkBuilder *builder = gtk_builder_new_from_resource ("/org/fsearch/fsearch/menus.ui");
            GMenuModel *menu_model = G_MENU_MODEL (gtk_builder_get_object (builder,
                                                                           "fsearch_listview_popup_menu"));
            GtkWidget *menu_widget = gtk_menu_new_from_model (G_MENU_MODEL (menu_model));
            gtk_menu_attach_to_widget (GTK_MENU (menu_widget),
                    GTK_WIDGET (widget),
                    NULL);
            gtk_menu_popup (GTK_MENU (menu_widget), NULL, NULL, NULL, NULL,
                    event->button, event->time);
            g_object_unref (builder);
            return TRUE;
        }
    }
    else if (event->type == GDK_2BUTTON_PRESS) {
        if (event->window == gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget))) {
            GtkTreeViewColumn *column = NULL;
            GtkTreePath *path = NULL;
            gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
                                           event->x,
                                           event->y,
                                           &path,
                                           &column,
                                           NULL,
                                           NULL);
            if (path) {
                gtk_tree_path_free(path);
            }
        }
    }

    return FALSE;
}

static void
on_listview_row_activated (GtkTreeView       *tree_view,
                           GtkTreePath       *path,
                           GtkTreeViewColumn *column,
                           gpointer           user_data)
{

    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter   iter;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter.user_data;
        if (entry) {
            BTreeNode * node = db_search_entry_get_node (entry);
            launch_node (node);
        }
    }
}

static void
on_listview_selection_changed (GtkTreeSelection *sel,
                               gpointer user_data)
{
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));

    fsearch_window_actions_update (self);

    uint32_t num_folders = 0;
    uint32_t num_files = 0;
    if (self->search) {
        num_folders = db_search_get_num_folders (self->search);
        num_files = db_search_get_num_files (self->search);
    }
    if (!num_folders && !num_files) {
        gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer), FALSE);
        return;
    }

    count_results_ctx ctx = {0, 0};
    gtk_tree_selection_selected_foreach (sel,
                                         (GtkTreeSelectionForeachFunc) count_results_cb,
                                         &ctx);

    if (!ctx.num_folders && !ctx.num_files) {
        gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer), FALSE);
    }
    else {
        gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer), TRUE);
        char text[100] = "";
        snprintf (text, sizeof (text), "%d/%d", ctx.num_folders, num_folders);
        gtk_label_set_text (GTK_LABEL (self->num_folders_label), text);
        snprintf (text, sizeof (text), "%d/%d", ctx.num_files, num_files);
        gtk_label_set_text (GTK_LABEL (self->num_files_label), text);
    }
}

static gboolean
toggle_action_on_2button_press (GdkEventButton *event, const char *action, gpointer user_data)
{
    if (event->button == GDK_BUTTON_PRIMARY
        && event->type == GDK_2BUTTON_PRESS) {
        GActionGroup *group = gtk_widget_get_action_group (GTK_WIDGET (user_data), "win");
        GVariant *state = g_action_group_get_action_state (group, action);
        g_action_group_change_action_state (group,
                                            action,
                                            g_variant_new_boolean (!g_variant_get_boolean (state)));
        g_variant_unref (state);
        return TRUE;
    }
    return FALSE;
}

static gboolean
on_search_mode_label_button_press_event (GtkWidget *widget,
                                         GdkEventButton *event,
                                         gpointer user_data)
{
    return toggle_action_on_2button_press (event, "search_mode", user_data);
}

static gboolean
on_search_in_path_label_button_press_event (GtkWidget *widget,
                                            GdkEventButton *event,
                                            gpointer user_data)
{
    return toggle_action_on_2button_press (event, "search_in_path", user_data);
}

static gboolean
on_match_case_label_button_press_event (GtkWidget *widget,
                                        GdkEventButton *event,
                                        gpointer user_data)
{
    return toggle_action_on_2button_press (event, "match_case", user_data);
}

static void
on_search_entry_changed (GtkEntry *entry, gpointer user_data)
{
    FsearchApplicationWindow *win = user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    if (config->search_as_you_type) {
        perform_search (win);
    }
}

static void
create_view_and_model (FsearchApplicationWindow *app)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (app));

    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);

    app->list_model = list_model_new();
    GtkTreeView *list = GTK_TREE_VIEW (app->listview);

    if (!config->restore_column_config) {
        listview_add_default_columns (list);
    }
    else {
        listview_add_column (list,
                             LIST_MODEL_COL_NAME,
                             config->name_column_width,
                             config->name_column_pos);

        if (config->show_path_column) {
            listview_add_column (list, LIST_MODEL_COL_PATH,
                                 config->path_column_width,
                                 config->path_column_pos);
        }
        if (config->show_type_column) {
            listview_add_column (list,
                                 LIST_MODEL_COL_TYPE,
                                 config->type_column_width,
                                 config->type_column_pos);
        }
        if (config->show_size_column) {
            listview_add_column (list,
                                 LIST_MODEL_COL_SIZE,
                                 config->size_column_width,
                                 config->size_column_pos);
        }
        if (config->show_modified_column) {
            listview_add_column (list,
                                 LIST_MODEL_COL_CHANGED,
                                 config->modified_column_width,
                                 config->modified_column_pos);
        }
    }

    gtk_tree_view_set_model (list,
                             GTK_TREE_MODEL(app->list_model));
    g_object_unref(app->list_model); /* destroy store automatically with view */
}

void
set_toggle_button (GtkPopover *popover,
               gpointer    user_data)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON (user_data);
    gtk_toggle_button_set_active (button, FALSE);
}

static GtkWidget *
create_popover (GtkWidget *child)
{
    GtkBuilder *builder = gtk_builder_new_from_resource ("/org/fsearch/fsearch/popover.ui");
    GtkWidget *popover = GTK_WIDGET (gtk_builder_get_object (builder, "DatabasePopover"));
    gtk_popover_set_relative_to (GTK_POPOVER (popover), child);
    return popover;
}

static void
fill_database_popover (GtkWidget *popover)
{
}

void
icon_theme_changed_cb (GtkIconTheme *icon_theme,
                       gpointer      user_data)
{
    iconstore_clear ();
}

static void
updated_database_cb (gpointer data, gpointer user_data)
{
    FsearchApplicationWindow *win = (FsearchApplicationWindow *) user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    hide_overlays (win);

    fsearch_application_window_update_search (win);

    gtk_spinner_stop (GTK_SPINNER (win->database_spinner));

    gtk_stack_set_visible_child (GTK_STACK (win->database_stack), win->database_box2);
    Database *db = fsearch_application_get_db (FSEARCH_APPLICATION_DEFAULT);
    uint32_t num_items = db_get_num_entries (db);
    gchar db_text[100] = "";
    snprintf (db_text, sizeof (db_text), "%'d Items", num_items);
    gtk_label_set_text (GTK_LABEL (win->database_label), db_text);


    time_t timestamp = db_get_timestamp (db);
    strftime (db_text, sizeof(db_text),
             "Last Updated: %Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
             localtime (&timestamp));
    gtk_widget_set_tooltip_text (win->database_toggle_button, db_text);
}

static void
update_database_cb (gpointer data, gpointer user_data)
{
    FsearchApplicationWindow *win = (FsearchApplicationWindow *) user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    show_overlay (win, DATABASE_UPDATING_OVERLAY);

    gtk_stack_set_visible_child (GTK_STACK (win->database_stack), win->database_box1);
    gtk_spinner_start (GTK_SPINNER (win->database_spinner));
    gchar db_text[100] = "";
    snprintf (db_text, sizeof (db_text), "Loading Database...");
    gtk_label_set_text (GTK_LABEL (win->database_label), db_text);
}

static void
fsearch_application_window_init (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));

    gtk_widget_init_template (GTK_WIDGET (self));

    GtkStyleContext *context = gtk_widget_get_style_context (self->scrolledwindow1);
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_resource (provider, "/org/fsearch/fsearch/shared.css");
    gtk_style_context_add_provider (context,
                                    GTK_STYLE_PROVIDER (provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    create_view_and_model (self);
    //self->selection_popover = create_popover (self->selection_toggle_button);
    //g_signal_connect (self->selection_popover,
    //                  "closed",
    //                  G_CALLBACK (set_toggle_button),
    //                  (gpointer)self->selection_toggle_button);
    self->database_popover = create_popover (self->database_toggle_button);
    fill_database_popover (self->database_popover);
    g_signal_connect (self->database_popover,
                      "closed",
                      G_CALLBACK (set_toggle_button),
                      (gpointer)self->database_toggle_button);

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    g_signal_connect (app,
                      "database-update",
                      G_CALLBACK (update_database_cb),
                      self);
    g_signal_connect (app,
                      "database-updated",
                      G_CALLBACK (updated_database_cb),
                      self);

    g_signal_connect (gtk_icon_theme_get_default (),
                      "changed",
                      G_CALLBACK (icon_theme_changed_cb),
                      self);

    GtkBuilder *builder = gtk_builder_new_from_resource ("/org/fsearch/fsearch/overlay.ui");

    // Overlay when no search results are found
    self->no_search_results_overlay = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                          "no_search_results"));
    gtk_overlay_add_overlay (GTK_OVERLAY (self->search_overlay),
                             self->no_search_results_overlay);
    gtk_overlay_set_overlay_pass_through (GTK_OVERLAY (self->search_overlay),
                                          self->no_search_results_overlay,
                                          FALSE);

    // Overlay when database is empty
    self->empty_database_overlay = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                       "empty_database"));
    gtk_overlay_add_overlay (GTK_OVERLAY (self->search_overlay),
                             self->empty_database_overlay);
    gtk_overlay_set_overlay_pass_through (GTK_OVERLAY (self->search_overlay),
                                          self->empty_database_overlay,
                                          FALSE);

    // Overlay when search query is empty
    self->empty_search_query_overlay = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                           "empty_search_query"));
    gtk_overlay_add_overlay (GTK_OVERLAY (self->search_overlay),
                             self->empty_search_query_overlay);
    gtk_overlay_set_overlay_pass_through (GTK_OVERLAY (self->search_overlay),
                                          self->empty_search_query_overlay,
                                          FALSE);

    // Overlay when database is updating
    self->database_updating_overlay = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                           "database_updating"));
    gtk_overlay_add_overlay (GTK_OVERLAY (self->search_overlay),
                             self->database_updating_overlay);
    gtk_overlay_set_overlay_pass_through (GTK_OVERLAY (self->search_overlay),
                                          self->database_updating_overlay,
                                          FALSE);
    self->database_updating_label = GTK_WIDGET (gtk_builder_get_object (builder,
                                                                        "database_updating_label"));

    g_object_unref (builder);
}

void
on_database_toggle_button_toggled (GtkToggleButton *togglebutton,
                                   gpointer         user_data)
{
    FsearchApplicationWindow *self = (FsearchApplicationWindow *)user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    gtk_widget_set_visible (self->database_popover,
                            gtk_toggle_button_get_active (togglebutton));
}

void
on_selection_toggle_button_toggled (GtkToggleButton *togglebutton,
                                    gpointer         user_data)
{
    return;

    FsearchApplicationWindow *self = (FsearchApplicationWindow *)user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    gtk_widget_set_visible (self->selection_popover,
                            gtk_toggle_button_get_active (togglebutton));
}

void
on_filter_combobox_changed (GtkComboBox *widget,
                            gpointer     user_data)
{
    FsearchApplicationWindow *win = user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    perform_search (win);
}

void
on_search_entry_activate (GtkButton *widget,
                    gpointer   user_data)
{
    FsearchApplicationWindow *win = user_data;
    g_assert (FSEARCH_WINDOW_IS_WINDOW (win));

    perform_search (win);
}

gboolean
on_listview_query_tooltip (GtkWidget  *widget,
                           gint        x,
                           gint        y,
                           gboolean    keyboard_mode,
                           GtkTooltip *tooltip,
                           gpointer    user_data)
{
    FsearchApplication *app =FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config (app);
    if (!config->enable_list_tooltips) {
        return FALSE;
    }
    gboolean ret_val = FALSE;

    GtkTreeModel *model = NULL;
    GtkTreePath *path = NULL;
    GtkTreeIter iter = {0};

    if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget),
                                            &x,
                                            &y,
                                            keyboard_mode,
                                            &model,
                                            &path,
                                            &iter))
    {
        return ret_val;
    }

    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter.user_data;
    if (entry) {
        BTreeNode *node = db_search_entry_get_node (entry);
        if (node) {
            char path_name[PATH_MAX] = "";
            btree_node_get_path_full (node, path_name, sizeof (path_name));
            gtk_tree_view_set_tooltip_row (GTK_TREE_VIEW (widget),
                                           tooltip,
                                           path);
            gtk_tooltip_set_text (tooltip, path_name);
            ret_val = TRUE;
        }
    }
    gtk_tree_path_free (path);
    return ret_val;
}

static void
fsearch_application_window_class_init (FsearchApplicationWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->constructed = fsearch_application_window_constructed;
    object_class->finalize = fsearch_application_window_finalize;
    gtk_widget_class_set_template_from_resource (widget_class, "/org/fsearch/fsearch/fsearch.glade");
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_overlay);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, menubar);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, statusbar);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_in_path_revealer);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, match_case_revealer);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_mode_revealer);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_button);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_entry);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, filter_combobox);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, listview);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, listview_selection);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, selection_toggle_button);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, num_folders_label);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, num_files_label);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_toggle_button);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_spinner);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_icon);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_stack);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_box1);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_box2);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_label);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, database_label1);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_label);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, search_icon);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, revealer);
    gtk_widget_class_bind_template_child (widget_class, FsearchApplicationWindow, scrolledwindow1);

    gtk_widget_class_bind_template_callback (widget_class, on_search_entry_changed);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_button_press_event);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_key_press_event);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_popup_menu);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_selection_changed);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_row_activated);
    gtk_widget_class_bind_template_callback (widget_class, on_selection_toggle_button_toggled);
    gtk_widget_class_bind_template_callback (widget_class, on_match_case_label_button_press_event);
    gtk_widget_class_bind_template_callback (widget_class, on_search_in_path_label_button_press_event);
    gtk_widget_class_bind_template_callback (widget_class, on_search_mode_label_button_press_event);
    gtk_widget_class_bind_template_callback (widget_class, on_database_toggle_button_toggled);
    gtk_widget_class_bind_template_callback (widget_class, on_filter_combobox_changed);
    gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activate);
    gtk_widget_class_bind_template_callback (widget_class, on_listview_query_tooltip);
}

GtkWidget *
fsearch_application_window_get_statusbar (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return GTK_WIDGET (self->statusbar);
}

GtkWidget *
fsearch_application_window_get_menubar (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return GTK_WIDGET (self->menubar);
}

GtkWidget *
fsearch_application_window_get_filter_combobox (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->filter_combobox;
}

GtkWidget *
fsearch_application_window_get_search_button (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->search_button;
}

GtkEntry *
fsearch_application_window_get_search_entry (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return GTK_ENTRY (self->search_entry);
}

void
fsearch_application_window_update_database_label (FsearchApplicationWindow *self, const char *text)
{
    //printf("%s\n", text);
    gtk_label_set_text (GTK_LABEL (self->search_label), text);
}

GtkWidget *
fsearch_application_window_get_search_in_path_revealer (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->search_in_path_revealer;
}

GtkWidget *
fsearch_application_window_get_match_case_revealer (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->match_case_revealer;
}

GtkWidget *
fsearch_application_window_get_search_mode_revealer (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->search_mode_revealer;
}

GtkTreeView *
fsearch_application_window_get_listview (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return GTK_TREE_VIEW (self->listview);
}

GtkTreeSelection *
fsearch_application_window_get_listview_selection (FsearchApplicationWindow *self)
{
    g_assert (FSEARCH_WINDOW_IS_WINDOW (self));
    return self->listview_selection;
}


FsearchApplicationWindow *
fsearch_application_window_new (FsearchApplication *app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));
    return g_object_new (FSEARCH_APPLICATION_WINDOW_TYPE, "application", app, NULL);
}


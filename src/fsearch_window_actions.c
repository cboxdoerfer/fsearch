#include <limits.h>
#include <linux/limits.h>

#include "fsearch_window_actions.h"
#include "clipboard.h"
#include "database_search.h"
#include "config.h"

static void
action_set_active_bool (GActionGroup *group, const gchar *action_name, bool value)
{
    g_assert (G_IS_ACTION_GROUP (group));
    g_assert (G_IS_ACTION_MAP (group));

    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (group), action_name);

    if (action) {
        g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (value));
    }
}

static void
action_set_enabled (GActionGroup *group, const gchar *action_name, bool value)
{
}

static void
copy_file (GtkTreeModel *model,
           GtkTreePath  *path,
           GtkTreeIter  *iter,
           gpointer      userdata)
{
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    GList **file_list = (GList **)userdata;
    if (entry) {
        GNode * node = db_search_entry_get_node (entry);
        char path[PATH_MAX] = "";
        bool res = db_node_get_path_full (node, path, sizeof (path));
        if (res) {
            *file_list = g_list_prepend (*file_list, g_strdup (path));
        }
    }
}

static void
fsearch_window_action_delete (GSimpleAction *action,
                              GVariant      *variant,
                              gpointer       user_data)
{
}

static void
fsearch_window_action_cut (GSimpleAction *action,
                           GVariant      *variant,
                           gpointer       user_data)
{
}

static void
fsearch_window_action_copy (GSimpleAction *action,
                            GVariant      *variant,
                            gpointer       user_data)
{
    printf("action: copy file\n");
    FsearchApplicationWindow *self = user_data;
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection (self);
    if (selection) {
        //guint selected_rows = gtk_tree_selection_count_selected_rows (selection);
        GList *file_list = NULL;
        gtk_tree_selection_selected_foreach (selection, copy_file, &file_list);
        file_list = g_list_reverse (file_list);
        clipboard_copy_file_list (file_list, 1);
    }
}

static void
fsearch_window_action_focus_search (GSimpleAction *action,
                                    GVariant      *variant,
                                    gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    GtkWidget *entry = GTK_WIDGET (fsearch_application_window_get_search_entry (self));
    gtk_widget_grab_focus (entry);
}

static void
fsearch_window_action_hide_window (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    gtk_window_iconify (GTK_WINDOW (self));
}

static void
fsearch_window_action_show_filter (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state (action, variant);
    GtkWidget *filter = GTK_WIDGET (fsearch_application_window_get_filter_combobox (self));
    gtk_widget_set_visible (filter, g_variant_get_boolean (variant));
    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    config->show_filter = g_variant_get_boolean (variant);
}

static void
fsearch_window_action_show_search_button (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state (action, variant);
    GtkWidget *button = GTK_WIDGET (fsearch_application_window_get_search_button (self));
    gtk_widget_set_visible (button, g_variant_get_boolean (variant));
    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    config->show_search_button = g_variant_get_boolean (variant);
}

static void
fsearch_window_action_show_menubar (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state (action, variant);
    GtkWidget *menubar = GTK_WIDGET (fsearch_application_window_get_menubar (self));
    gtk_widget_set_visible (menubar, g_variant_get_boolean (variant));
    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    config->show_menubar = g_variant_get_boolean (variant);
}

static void
fsearch_window_action_search_in_path (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    g_simple_action_set_state (action, variant);
    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    config->search_in_path = g_variant_get_boolean (variant);
}

static void
fsearch_window_action_search_mode (GSimpleAction *action,
                                   GVariant      *variant,
                                   gpointer       user_data)
{
    FsearchApplicationWindow *self = user_data;
    g_simple_action_set_state (action, variant);
    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    config->enable_regex = g_variant_get_boolean (variant);
    GtkWidget *button = fsearch_application_window_get_statusbar_search_mode_button (self);
    if (config->enable_regex) {
        gtk_button_set_label (GTK_BUTTON (button), "REGEX");
    }
    else {
        gtk_button_set_label (GTK_BUTTON (button), "NORMAL");
    }
}

static void
fsearch_window_action_update_database (GSimpleAction *action,
                                       GVariant      *variant,
                                       gpointer       user_data)
{
    printf("update database action\n");
}

static void
action_toggle_state_cb (GSimpleAction *saction,
                        GVariant *parameter,
                        gpointer user_data)
{
    GAction *action = G_ACTION (saction);

    GVariant *state = g_action_get_state (action);
    g_action_change_state (action, g_variant_new_boolean (!g_variant_get_boolean (state)));
    g_variant_unref (state);
}


static GActionEntry FsearchWindowActions[] = {
    { "copy_clipboard",     fsearch_window_action_copy },
    { "cut_clipboard",     fsearch_window_action_cut },
    { "delete_selection",     fsearch_window_action_delete },
    { "focus_search",     fsearch_window_action_focus_search },
    { "hide_window",     fsearch_window_action_hide_window },
    //{ "update_database",     fsearch_window_action_update_database },
    // View
    { "show_menubar", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_menubar },
    { "show_filter", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_filter },
    { "show_search_button", action_toggle_state_cb, NULL, "true", fsearch_window_action_show_search_button },
    // Search
    { "search_in_path", action_toggle_state_cb, NULL, "true", fsearch_window_action_search_in_path },
    { "search_mode", action_toggle_state_cb, NULL, "true", fsearch_window_action_search_mode },
};

void
fsearch_window_actions_update   (FsearchApplicationWindow *self)
{
    GtkTreeSelection *selection = fsearch_application_window_get_listview_selection (self);

    GActionGroup *group = gtk_widget_get_action_group (GTK_WIDGET (self), "win");
    g_assert (G_IS_SIMPLE_ACTION_GROUP (group));

    gint num_rows_selected = gtk_tree_selection_count_selected_rows (selection);
    action_set_enabled (group, "copy_clipboard", num_rows_selected);
    action_set_enabled (group, "cut_clipboard", num_rows_selected);
    action_set_enabled (group, "delete_selection", num_rows_selected);
    action_set_enabled (group, "focus_search", TRUE);
    action_set_enabled (group, "hide_window", TRUE);
    action_set_enabled (group, "update_database", TRUE);
    action_set_enabled (group, "show_menubar", TRUE);
    action_set_enabled (group, "show_filter", TRUE);
    action_set_enabled (group, "show_search_button", TRUE);

    FsearchConfig *config = fsearch_application_get_config (FSEARCH_APPLICATION_DEFAULT);
    action_set_active_bool (group, "show_menubar", config->show_menubar);
    action_set_active_bool (group, "show_filter", config->show_filter);
    action_set_active_bool (group, "show_search_button", config->show_search_button);
    action_set_active_bool (group, "search_in_path", config->search_in_path);
    action_set_active_bool (group, "search_mode", config->enable_regex);
}

void
fsearch_window_actions_init   (FsearchApplicationWindow *self)
{
    g_action_map_add_action_entries (G_ACTION_MAP (self),
                                     FsearchWindowActions,
                                     G_N_ELEMENTS (FsearchWindowActions),
                                     self);

    fsearch_window_actions_update (self);
}

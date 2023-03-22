#include "fsearch_preferences_dialog.h"

#include <glib/gi18n.h>

#include "fsearch_database_preferences_widget.h"
#include "fsearch_filter_preferences_widget.h"

struct _FsearchPreferencesDialog {
    GtkDialog parent_instance;

    FsearchConfig *config;
    FsearchConfig *config_old;
    FsearchDatabase2 *db;

    // Interface page
    GtkWidget *help_stack;
    GtkWidget *help_description;
    GtkWidget *help_expander;

    FsearchFilterPreferencesWidget *filter_pref_widget;
    FsearchDatabasePreferencesWidget *database_pref_widget;

    guint help_reset_timeout_id;

    GtkNotebook *main_notebook;

    // Interface page
    GtkToggleButton *enable_dark_theme_button;
    GtkToggleButton *show_menubar_button;
    GtkToggleButton *show_tooltips_button;
    GtkToggleButton *restore_win_size_button;
    GtkToggleButton *exit_on_escape_button;
    GtkToggleButton *restore_sort_order_button;
    GtkToggleButton *restore_column_config_button;
    GtkToggleButton *double_click_path_button;
    GtkToggleButton *single_click_open_button;
    GtkToggleButton *launch_desktop_files_button;
    GtkToggleButton *show_icons_button;
    GtkToggleButton *highlight_search_terms;
    GtkToggleButton *show_base_2_units;
    GtkComboBox *action_after_file_open;
    GtkToggleButton *action_after_file_open_keyboard;
    GtkToggleButton *action_after_file_open_mouse;
    GtkToggleButton *show_indexing_status_button;

    // Search page
    GtkToggleButton *auto_search_in_path_button;
    GtkToggleButton *auto_match_case_button;
    GtkToggleButton *search_as_you_type_button;
    GtkToggleButton *hide_results_button;

    GtkFrame *filter_frame;

    // Dialog page
    GtkToggleButton *show_dialog_failed_opening;
};

enum { PROP_0, PROP_CONFIG, PROP_DATABASE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_TYPE(FsearchPreferencesDialog, fsearch_preferences_dialog, GTK_TYPE_DIALOG)

static gboolean
help_reset(gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    if (self->help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(self->help_stack), GTK_WIDGET(self->help_description));
    }
    g_source_remove(self->help_reset_timeout_id);
    self->help_reset_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean
on_help_reset(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    if (self->help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(self->help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }
    self->help_reset_timeout_id = g_timeout_add(200, help_reset, self);
    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_help_show(GtkWidget *widget, int x, int y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(gtk_widget_get_toplevel(widget));
    g_return_val_if_fail(self, GDK_EVENT_PROPAGATE);

    if (self->help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(self->help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }

    if (self->help_reset_timeout_id != 0) {
        g_source_remove(self->help_reset_timeout_id);
        self->help_reset_timeout_id = 0;
    }
    if (self->help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(self->help_stack), GTK_WIDGET(user_data));
    }
    return GDK_EVENT_PROPAGATE;
}

static void
update_config(FsearchPreferencesDialog *self) {
    self->config->enable_dark_theme = gtk_toggle_button_get_active(self->enable_dark_theme_button);
    self->config->show_menubar = !gtk_toggle_button_get_active(self->show_menubar_button);
    self->config->enable_list_tooltips = gtk_toggle_button_get_active(self->show_tooltips_button);
    self->config->restore_window_size = gtk_toggle_button_get_active(self->restore_win_size_button);
    self->config->restore_column_config = gtk_toggle_button_get_active(self->restore_column_config_button);
    self->config->restore_sort_order = gtk_toggle_button_get_active(self->restore_sort_order_button);
    self->config->exit_on_escape = gtk_toggle_button_get_active(self->exit_on_escape_button);
    self->config->double_click_path = gtk_toggle_button_get_active(self->double_click_path_button);
    self->config->single_click_open = gtk_toggle_button_get_active(self->single_click_open_button);
    self->config->launch_desktop_files = gtk_toggle_button_get_active(self->launch_desktop_files_button);
    self->config->show_listview_icons = gtk_toggle_button_get_active(self->show_icons_button);
    self->config->highlight_search_terms = gtk_toggle_button_get_active(self->highlight_search_terms);
    self->config->show_base_2_units = gtk_toggle_button_get_active(self->show_base_2_units);
    self->config->action_after_file_open_keyboard = gtk_toggle_button_get_active(self->action_after_file_open_keyboard);
    self->config->action_after_file_open_mouse = gtk_toggle_button_get_active(self->action_after_file_open_mouse);
    self->config->show_indexing_status = gtk_toggle_button_get_active(self->show_indexing_status_button);
    self->config->auto_search_in_path = gtk_toggle_button_get_active(self->auto_search_in_path_button);
    self->config->auto_match_case = gtk_toggle_button_get_active(self->auto_match_case_button);
    self->config->search_as_you_type = gtk_toggle_button_get_active(self->search_as_you_type_button);
    self->config->hide_results_on_empty_search = gtk_toggle_button_get_active(self->hide_results_button);
    self->config->show_dialog_failed_opening = gtk_toggle_button_get_active(self->show_dialog_failed_opening);

    self->config_old->action_after_file_open = gtk_combo_box_get_active(self->action_after_file_open);

    g_clear_pointer(&self->config->filters, fsearch_filter_manager_unref);
    self->config->filters = fsearch_filter_preferences_widget_get_filter_manager(self->filter_pref_widget);
}

static void
on_action_after_file_open_changed(GtkComboBox *widget, gpointer user_data) {
    int active = gtk_combo_box_get_active(widget);
    if (active != ACTION_AFTER_OPEN_NOTHING) {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), TRUE);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), FALSE);
    }
}

static void
fsearch_preferences_dialog_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer(value, self->config);
        break;
    case PROP_DATABASE:
        g_value_set_object(value, self->db);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_preferences_dialog_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->config = config_copy(g_value_get_pointer(value));
        self->config_old = config_copy(g_value_get_pointer(value));
        break;
    case PROP_DATABASE:
        g_set_object(&self->db, g_value_get_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_preferences_dialog_dispose(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    g_clear_object(&self->db);
    g_clear_pointer(&self->config, config_free);
    g_clear_pointer(&self->config_old, config_free);

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->dispose(object);
}

static void
fsearch_preferences_dialog_finalize(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    if (self->help_reset_timeout_id) {
        g_source_remove(self->help_reset_timeout_id);
        self->help_reset_timeout_id = 0;
    }

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->finalize(object);
}

static void
fsearch_preferences_dialog_constructed(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->constructed(object);

    self->filter_pref_widget = fsearch_filter_preferences_widget_new(self->config_old->filters);
    gtk_container_add(GTK_CONTAINER(self->filter_frame), GTK_WIDGET(self->filter_pref_widget));
    gtk_widget_show(GTK_WIDGET(self->filter_pref_widget));

    self->database_pref_widget = fsearch_database_preferences_widget_new(self->db);
    gtk_notebook_append_page(self->main_notebook, GTK_WIDGET(self->database_pref_widget), gtk_label_new(_("Database")));
    gtk_widget_show(GTK_WIDGET(self->database_pref_widget));

    gtk_toggle_button_set_active(self->enable_dark_theme_button, self->config_old->enable_dark_theme);
    gtk_toggle_button_set_active(self->show_menubar_button, !self->config_old->show_menubar);
    gtk_toggle_button_set_active(self->show_tooltips_button, self->config_old->enable_list_tooltips);
    gtk_toggle_button_set_active(self->restore_win_size_button, self->config_old->restore_window_size);
    gtk_toggle_button_set_active(self->restore_column_config_button, self->config_old->restore_column_config);
    gtk_toggle_button_set_active(self->restore_sort_order_button, self->config_old->restore_sort_order);
    gtk_toggle_button_set_active(self->exit_on_escape_button, self->config_old->exit_on_escape);
    gtk_toggle_button_set_active(self->double_click_path_button, self->config_old->double_click_path);
    gtk_toggle_button_set_active(self->single_click_open_button, self->config_old->single_click_open);
    gtk_toggle_button_set_active(self->launch_desktop_files_button, self->config_old->launch_desktop_files);
    gtk_toggle_button_set_active(self->show_icons_button, self->config_old->show_listview_icons);
    gtk_toggle_button_set_active(self->highlight_search_terms, self->config_old->highlight_search_terms);
    gtk_toggle_button_set_active(self->show_base_2_units, self->config_old->show_base_2_units);
    gtk_toggle_button_set_active(self->action_after_file_open_keyboard,
                                 self->config_old->action_after_file_open_keyboard);
    gtk_toggle_button_set_active(self->action_after_file_open_mouse, self->config_old->action_after_file_open_mouse);
    gtk_toggle_button_set_active(self->show_indexing_status_button, self->config_old->show_indexing_status);
    gtk_toggle_button_set_active(self->auto_search_in_path_button, self->config_old->auto_search_in_path);
    gtk_toggle_button_set_active(self->auto_match_case_button, self->config_old->auto_match_case);
    gtk_toggle_button_set_active(self->search_as_you_type_button, self->config_old->search_as_you_type);
    gtk_toggle_button_set_active(self->hide_results_button, self->config_old->hide_results_on_empty_search);
    gtk_toggle_button_set_active(self->show_dialog_failed_opening, self->config_old->show_dialog_failed_opening);

    gtk_combo_box_set_active(self->action_after_file_open, self->config_old->action_after_file_open);
}

static void
fsearch_preferences_dialog_class_init(FsearchPreferencesDialogClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = fsearch_preferences_dialog_finalize;
    object_class->dispose = fsearch_preferences_dialog_dispose;
    object_class->constructed = fsearch_preferences_dialog_constructed;
    object_class->set_property = fsearch_preferences_dialog_set_property;
    object_class->get_property = fsearch_preferences_dialog_get_property;

    properties[PROP_CONFIG] = g_param_spec_pointer("config",
                                                   "Configuration",
                                                   "The configuration which will be used to fill the dialog with and "
                                                   "which will be modified by the dialog"
                                                   "default",
                                                   (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
    properties[PROP_DATABASE] = g_param_spec_object("database",
                                                    "Database",
                                                    "The database which will be used fill the database section of the "
                                                    "dialog and which the new database config will be saved to"
                                                    "default",
                                                    FSEARCH_TYPE_DATABASE2,
                                                    (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(widget_class, "/io/github/cboxdoerfer/fsearch/ui/fsearch_preferences.ui");

    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_description);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_expander);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, main_notebook);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, enable_dark_theme_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_menubar_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_tooltips_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_win_size_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exit_on_escape_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_sort_order_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_column_config_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, double_click_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, single_click_open_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, launch_desktop_files_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_icons_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, highlight_search_terms);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_base_2_units);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open_keyboard);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open_mouse);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_indexing_status_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_search_in_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_match_case_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, search_as_you_type_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, hide_results_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, filter_frame);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_dialog_failed_opening);

    gtk_widget_class_bind_template_callback(widget_class, on_help_show);
    gtk_widget_class_bind_template_callback(widget_class, on_help_reset);
    gtk_widget_class_bind_template_callback(widget_class, on_action_after_file_open_changed);
}

static void
fsearch_preferences_dialog_init(FsearchPreferencesDialog *self) {
    g_assert(FSEARCH_IS_PREFERENCES_DIALOG(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    gtk_dialog_add_button(GTK_DIALOG(self), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(self), _("_OK"), GTK_RESPONSE_OK);
}

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config, FsearchDatabase2 *db) {
    FsearchPreferencesDialog *self = g_object_new(FSEARCH_PREFERENCES_DIALOG_TYPE, "config", config, "database", db, NULL);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    }
    return self;
}

FsearchConfig *
fsearch_preferences_dialog_get_config(FsearchPreferencesDialog *self) {
    g_return_val_if_fail(self, NULL);
    update_config(self);
    return config_copy(self->config);
}

FsearchDatabaseIncludeManager *
fsearch_preferences_dialog_get_include_manager(FsearchPreferencesDialog *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_preferences_widget_get_include_manager(self->database_pref_widget);
}

FsearchDatabaseExcludeManager *
fsearch_preferences_dialog_get_exclude_manager(FsearchPreferencesDialog *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_preferences_widget_get_exclude_manager(self->database_pref_widget);
}

void
fsearch_preferences_dialog_set_page(FsearchPreferencesDialog *self, FsearchPreferencesDialogPage page) {
    g_return_if_fail(self);

    switch (page) {
    case FSEARCH_PREFERENCES_DIALOG_PAGE_GENERAL:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 0);
        break;
    case FSEARCH_PREFERENCES_DIALOG_PAGE_SEARCH:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 1);
        break;
    case FSEARCH_PREFERENCES_DIALOG_PAGE_DATABASE:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 2);
        break;
    default:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 0);
    }
}

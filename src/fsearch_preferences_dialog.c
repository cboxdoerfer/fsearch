#include "fsearch_preferences_dialog.h"

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

    guint help_reset_timeout_id;

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
    GtkBox *action_after_file_open_box;
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

    // Database page
    GtkToggleButton *update_db_at_start_button;
    GtkToggleButton *auto_update_checkbox;
    GtkBox *auto_update_spin_box;
    GtkWidget *auto_update_hours_spin_button;
    GtkWidget *auto_update_minutes_spin_button;

    // Dialog page
    GtkToggleButton *show_dialog_failed_opening;

    // Include page
    GtkTreeView *index_list;
    GtkTreeModel *index_model;
    GtkWidget *index_add_button;
    GtkWidget *index_add_path_button;
    GtkWidget *index_path_entry;
    GtkWidget *index_remove_button;
    GtkTreeSelection *sel;

    // Exclude model
    GtkTreeView *exclude_list;
    GtkTreeModel *exclude_model;
    GtkWidget *exclude_add_path_button;
    GtkWidget *exclude_path_entry;
    GtkWidget *exclude_add_button;
    GtkWidget *exclude_remove_button;
    GtkTreeSelection *exclude_selection;
    GtkToggleButton *exclude_hidden_items_button;
    GtkEntry *exclude_files_entry;
    gchar *exclude_files_str;
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
on_auto_update_spin_button_changed(GtkSpinButton *spin_button, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    double minutes = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->auto_update_minutes_spin_button));
    double hours = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->auto_update_hours_spin_button));

    if (hours == 0 && minutes == 0) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->auto_update_minutes_spin_button), 1.0);
    }
}

static void
on_auto_update_checkbox_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    gtk_widget_set_sensitive(GTK_WIDGET(self->auto_update_spin_box), gtk_toggle_button_get_active(togglebutton));
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
on_index_add_button_clicked(GtkButton *button, gpointer user_data) {
    // GtkTreeModel *model = user_data;
    // FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
    // ctx->model = model;
    // ctx->row_add_func = pref_index_treeview_row_add;
    // run_file_chooser_dialog(button, ctx);
}

static void
on_index_add_path_button_clicked(GtkButton *button, gpointer user_data) {
    // FsearchPreferencesInterface *ui = user_data;
    // add_path(GTK_ENTRY(ui->index_path_entry), ui->index_model, pref_index_treeview_row_add);
}

static void
on_index_path_entry_changed(GtkEntry *entry, gpointer user_data) {
    // FsearchPreferencesInterface *ui = user_data;
    // path_entry_changed(entry, ui->index_add_path_button);
}

static void
on_exclude_add_button_clicked(GtkButton *button, gpointer user_data) {
   // GtkTreeModel *model = user_data;
   // FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
   // ctx->model = model;
   // ctx->row_add_func = pref_exclude_treeview_row_add;
   // run_file_chooser_dialog(button, ctx);
}

static void
on_exclude_add_path_button_clicked(GtkButton *button, gpointer user_data) {
    //    FsearchPreferencesInterface *ui = user_data;
    //    add_path(GTK_ENTRY(ui->exclude_path_entry), ui->exclude_model, pref_exclude_treeview_row_add);
}

static void
on_exclude_path_entry_changed(GtkEntry *entry, gpointer user_data) {
    //    FsearchPreferencesInterface *ui = user_data;
    //    path_entry_changed(entry, ui->exclude_add_path_button);
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
    gtk_toggle_button_set_active(self->update_db_at_start_button, self->config_old->update_database_on_launch);

    gtk_toggle_button_set_active(self->auto_update_checkbox, self->config_old->update_database_every);
    gtk_widget_set_sensitive(GTK_WIDGET(self->auto_update_spin_box), self->config_old->update_database_every);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->auto_update_hours_spin_button),
                              (double)self->config_old->update_database_every_hours);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->auto_update_minutes_spin_button),
                              (double)self->config_old->update_database_every_minutes);

    gtk_toggle_button_set_active(self->show_dialog_failed_opening, self->config_old->show_dialog_failed_opening);
    gtk_toggle_button_set_active(self->exclude_hidden_items_button, self->config_old->exclude_hidden_items);

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
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, update_db_at_start_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_update_checkbox);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_update_spin_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_update_hours_spin_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_update_minutes_spin_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_dialog_failed_opening);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, index_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, index_add_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, index_add_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, index_path_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, index_remove_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_add_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_path_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_add_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_remove_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_hidden_items_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exclude_files_entry);

    gtk_widget_class_bind_template_callback(widget_class, on_help_show);
    gtk_widget_class_bind_template_callback(widget_class, on_help_reset);
    gtk_widget_class_bind_template_callback(widget_class, on_action_after_file_open_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_auto_update_checkbox_toggled);
    gtk_widget_class_bind_template_callback(widget_class, on_auto_update_spin_button_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_index_add_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_index_path_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_index_add_path_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_add_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_path_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_add_path_button_clicked);
}

static void
fsearch_preferences_dialog_init(FsearchPreferencesDialog *self) {
    g_assert(FSEARCH_IS_PREFERENCES_DIALOG(self));

    gtk_widget_init_template(GTK_WIDGET(self));
}

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config, FsearchDatabase2 *db) {
    FsearchPreferencesDialog *self =
        g_object_new(FSEARCH_PREFERENCES_DIALOG_TYPE, "config", config, "database", db, NULL);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    }
    return self;
}

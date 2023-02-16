#include "fsearch_preferences_dialog.h"

struct _FsearchPreferencesDialog {
    GtkDialog parent_instance;

    FsearchConfig *config;
    FsearchConfig *config_old;
    FsearchDatabase2 *db;

    // Interface page
    GtkWidget *help_stack;
    GtkWidget *help_description;
    GtkWidget *help_expander;

    guint help_reset_timeout_id;
};

enum { PROP_0, PROP_CONFIG, PROP_DATABASE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_TYPE(FsearchPreferencesDialog, fsearch_preferences_dialog, GTK_TYPE_DIALOG)

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
fsearch_preferences_dialog_class_init(FsearchPreferencesDialogClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = fsearch_preferences_dialog_finalize;
    object_class->dispose = fsearch_preferences_dialog_dispose;
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
    gtk_widget_class_bind_template_callback(widget_class, on_help_show);
    gtk_widget_class_bind_template_callback(widget_class, on_help_reset);
}

static void
fsearch_preferences_dialog_init(FsearchPreferencesDialog *self) {
    g_assert(FSEARCH_IS_PREFERENCES_DIALOG(self));

    gtk_widget_init_template(GTK_WIDGET(self));
}

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(FsearchConfig *config, FsearchDatabase2 *db) {
    return g_object_new(FSEARCH_PREFERENCES_DIALOG_TYPE, "config", config, "database", db, NULL);
}

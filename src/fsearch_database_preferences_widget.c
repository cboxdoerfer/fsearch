#include "fsearch_database_preferences_widget.h"

#include <glib/gi18n.h>

struct _FsearchDatabasePreferencesWidget {
    GtkBox parent_instance;

    FsearchDatabase2 *db;

    FsearchDatabaseInfo *info;

    // Include page
    GtkTreeView *include_list;
    GtkListStore *include_model;
    GtkWidget *include_add_button;
    GtkWidget *include_add_path_button;
    GtkWidget *include_path_entry;
    GtkWidget *include_remove_button;
    GtkTreeSelection *include_selection;

    // Exclude model
    GtkTreeView *exclude_list;
    GtkListStore *exclude_model;
    GtkWidget *exclude_add_path_button;
    GtkWidget *exclude_path_entry;
    GtkWidget *exclude_add_button;
    GtkWidget *exclude_remove_button;
    GtkTreeSelection *exclude_selection;
    GtkToggleButton *exclude_hidden_items_button;
    GtkEntry *exclude_files_entry;

    gchar *exclude_files_str;
};

enum { COL_INCLUDE_ACTIVE, COL_INCLUDE_PATH, COL_INCLUDE_ONE_FS, NUM_INCLUDE_COLUMNS };
enum { COL_EXCLUDE_ACTIVE, COL_EXCLUDE_PATH, NUM_EXCLUDE_COLUMNS };

enum { PROP_0, PROP_DATABASE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_FINAL_TYPE(FsearchDatabasePreferencesWidget, fsearch_database_preferences_widget, GTK_TYPE_BOX)

static void
column_text_append(GtkTreeView *view, const char *name, gboolean expand, int id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(name, renderer, "text", id, NULL);
    gtk_tree_view_column_set_expand(col, expand);
    gtk_tree_view_column_set_sort_column_id(col, id);
    gtk_tree_view_append_column(view, col);
}

static bool
on_column_toggled(gchar *path_str, GtkTreeModel *model, int col) {
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    gboolean val;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, col, &val, -1);

    val ^= 1;

    gtk_list_store_set(GTK_LIST_STORE(model), &iter, col, val, -1);
    g_clear_pointer(&path, gtk_tree_path_free);

    return val;
}

static void
on_column_exclude_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *exclude_model = data;
    on_column_toggled(path_str, exclude_model, COL_EXCLUDE_ACTIVE);
}

static void
on_column_index_enable_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *index_model = data;
    on_column_toggled(path_str, index_model, COL_INCLUDE_ACTIVE);
}

static void
on_column_index_one_fs_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *index_model = data;
    on_column_toggled(path_str, index_model, COL_INCLUDE_ONE_FS);
}

static void
column_toggle_append(GtkTreeView *view, GtkTreeModel *model, const char *name, int id, GCallback cb, gpointer user_data) {
    GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "xalign", 0.0, NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(name, renderer, "active", id, NULL);
    gtk_tree_view_column_set_sort_column_id(col, id);
    gtk_tree_view_append_column(view, col);
    g_signal_connect(renderer, "toggled", cb, user_data);
}

static void
init_exclude_page(FsearchDatabasePreferencesWidget *self) {

    self->exclude_model = gtk_list_store_new(NUM_EXCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING);
    gtk_tree_view_set_model(self->exclude_list, GTK_TREE_MODEL(self->exclude_model));
    column_toggle_append(self->exclude_list,
                         GTK_TREE_MODEL(self->exclude_model),
                         _("Active"),
                         COL_EXCLUDE_ACTIVE,
                         G_CALLBACK(on_column_exclude_toggled),
                         self->exclude_model);
    column_text_append(self->exclude_list, _("Path"), TRUE, COL_EXCLUDE_PATH);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(self->exclude_list, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

static void
init_include_page(FsearchDatabasePreferencesWidget *self) {
    self->include_model =
        gtk_list_store_new(NUM_INCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    gtk_tree_view_set_model(self->include_list, GTK_TREE_MODEL(self->include_model));

    column_toggle_append(self->include_list,
                         GTK_TREE_MODEL(self->include_model),
                         _("Active"),
                         COL_INCLUDE_ACTIVE,
                         G_CALLBACK(on_column_index_enable_toggled),
                         self);
    column_text_append(self->include_list, _("Path"), TRUE, COL_INCLUDE_PATH);
    column_toggle_append(self->include_list,
                         GTK_TREE_MODEL(self->include_model),
                         _("One Filesystem"),
                         COL_INCLUDE_ONE_FS,
                         G_CALLBACK(on_column_index_one_fs_toggled),
                         self);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(self->include_list, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

static void
populate_include_page(FsearchDatabasePreferencesWidget *self) {
    FsearchDatabaseIncludeManager *include_manager = fsearch_database_info_get_include_manager(self->info);
    if (!include_manager) {
        return;
    }
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    if (!includes || includes->len == 0) {
        return;
    }

    for (uint32_t i = 0; i < includes->len; ++i) {
        GtkTreeIter iter = {};
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        gtk_list_store_append(self->exclude_model, &iter);
        gtk_list_store_set(self->exclude_model,
                           &iter,
                           COL_INCLUDE_ACTIVE,
                           fsearch_database_include_get_active(include),
                           COL_INCLUDE_PATH,
                           fsearch_database_include_get_path(include),
                           COL_INCLUDE_ONE_FS,
                           fsearch_database_include_get_one_file_system(include),
                           -1);
    }
}

static void
populate_exclude_page(FsearchDatabasePreferencesWidget *self) {
    FsearchDatabaseExcludeManager *exclude_manager = fsearch_database_info_get_exclude_manager(self->info);
    if (!exclude_manager) {
        return;
    }
    g_autoptr(GPtrArray) excludes = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    if (!excludes || excludes->len == 0) {
        return;
    }

    for (uint32_t i = 0; i < excludes->len; ++i) {
        GtkTreeIter iter = {};
        FsearchDatabaseExclude *exclude = g_ptr_array_index(excludes, i);
        gtk_list_store_append(self->exclude_model, &iter);
        gtk_list_store_set(self->exclude_model,
                           &iter,
                           COL_EXCLUDE_ACTIVE,
                           fsearch_database_exclude_get_active(exclude),
                           COL_EXCLUDE_PATH,
                           fsearch_database_exclude_get_path(exclude),
                           -1);
    }
}

static void
fsearch_database_preferences_widget_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(object);

    switch (prop_id) {
    case PROP_DATABASE:
        g_value_set_object(value, self->db);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_preferences_widget_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(object);

    switch (prop_id) {
    case PROP_DATABASE:
        g_set_object(&self->db, g_value_get_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_preferences_widget_dispose(GObject *object) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(object);

    g_clear_object(&self->db);

    G_OBJECT_CLASS(fsearch_database_preferences_widget_parent_class)->dispose(object);
}

static void
fsearch_database_preferences_widget_constructed(GObject *object) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(object);

    if (fsearch_database2_try_get_database_info(self->db, &self->info) == FSEARCH_RESULT_SUCCESS) {
        populate_include_page(self);
        populate_exclude_page(self);
    }

    G_OBJECT_CLASS(fsearch_database_preferences_widget_parent_class)->constructed(object);
}

static void
fsearch_database_preferences_widget_class_init(FsearchDatabasePreferencesWidgetClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = fsearch_database_preferences_widget_dispose;
    object_class->constructed = fsearch_database_preferences_widget_constructed;
    object_class->set_property = fsearch_database_preferences_widget_set_property;
    object_class->get_property = fsearch_database_preferences_widget_get_property;

    properties[PROP_DATABASE] = g_param_spec_object("database",
                                                    "Database",
                                                    "The database which will be represented and edited in this "
                                                    "widget",
                                                    FSEARCH_TYPE_DATABASE2,
                                                    (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/io/github/cboxdoerfer/fsearch/ui/"
                                                "fsearch_database_preferences_widget.ui");

    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_add_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_add_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_remove_button);

    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_add_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_add_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_remove_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_hidden_items_button);
}

static void
fsearch_database_preferences_widget_init(FsearchDatabasePreferencesWidget *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    init_include_page(self);
    init_exclude_page(self);
}

FsearchDatabasePreferencesWidget *
fsearch_database_preferences_widget_new(FsearchDatabase2 *db) {
    return g_object_new(FSEARCH_DATABASE_PREFERENCES_WIDGET_TYPE, "database", db, NULL);
}

FsearchDatabaseIncludeManager *
fsearch_database_preferences_widget_get_include_manager(FsearchDatabasePreferencesWidget *self) {
    g_return_val_if_fail(self, NULL);

    GtkTreeModel *model = GTK_TREE_MODEL(self->include_model);
    GtkTreeIter iter = {};
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();

    while (valid) {
        g_autofree gchar *path = NULL;
        gboolean active = FALSE;
        gboolean one_file_system = FALSE;
        gtk_tree_model_get(model,
                           &iter,
                           COL_INCLUDE_PATH,
                           &path,
                           COL_INCLUDE_ACTIVE,
                           &active,
                           COL_INCLUDE_ONE_FS,
                           &one_file_system,
                           -1);

        if (path) {
            fsearch_database_include_manager_add(
                include_manager,
                fsearch_database_include_new(path, active, one_file_system, FALSE, FALSE, 0));
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return g_steal_pointer(&include_manager);
}

FsearchDatabaseExcludeManager *
fsearch_database_preferences_widget_get_exclude_manager(FsearchDatabasePreferencesWidget *self) {
    g_return_val_if_fail(self, NULL);

    GtkTreeModel *model = GTK_TREE_MODEL(self->exclude_model);
    GtkTreeIter iter = {};
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    while (valid) {
        g_autofree gchar *path = NULL;
        gboolean active = FALSE;
        gtk_tree_model_get(model, &iter, COL_EXCLUDE_PATH, &path, COL_EXCLUDE_ACTIVE, &active, -1);

        if (path) {
            fsearch_database_exclude_manager_add(exclude_manager, fsearch_database_exclude_new(path, active));
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return g_steal_pointer(&exclude_manager);
}

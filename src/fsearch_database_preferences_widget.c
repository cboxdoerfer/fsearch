#define G_LOG_DOMAIN "fsearch-database-preferences-widget"

#include "fsearch_database_preferences_widget.h"

#include <config.h>
#include <glib/gi18n.h>

struct _FsearchDatabasePreferencesWidget {
    GtkBox parent_instance;

    FsearchDatabase *db;

    FsearchDatabaseInfo *info;

    // Include page
    GtkTreeView *include_list;
    GtkListStore *include_model;
    GtkWidget *include_path_entry;
    GtkTreeSelection *include_selection;

    // Exclude model
    GtkTreeView *exclude_list;
    GtkListStore *exclude_model;
    GtkWidget *exclude_path_entry;
    GtkTreeSelection *exclude_selection;
    GtkToggleButton *exclude_hidden_items_button;
    GtkEntry *exclude_files_entry;

    gchar *exclude_files_str;
};
enum {
    COL_INCLUDE_ACTIVE,
    COL_INCLUDE_PATH,
    COL_INCLUDE_ONE_FS,
    COL_INCLUDE_MONITOR,
    COL_INCLUDE_ID,
    NUM_INCLUDE_COLUMNS
};
enum { COL_EXCLUDE_ACTIVE, COL_EXCLUDE_PATH, NUM_EXCLUDE_COLUMNS };

enum { PROP_0, PROP_DATABASE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_FINAL_TYPE(FsearchDatabasePreferencesWidget, fsearch_database_preferences_widget, GTK_TYPE_BOX)

typedef void (*RowAddFunc)(GtkListStore *, const char *);

typedef struct {
    GtkListStore *model;
    RowAddFunc row_add_func;
} FsearchPreferencesFileChooserContext;

#if !GTK_CHECK_VERSION(3, 20, 0)
static void
on_file_chooser_dialog_response(GtkFileChooserDialog *dialog, GtkResponseType response, gpointer user_data) {
#else
static void
on_file_chooser_native_dialog_response(GtkNativeDialog *dialog, GtkResponseType response, gpointer user_data) {
#endif
    FsearchPreferencesFileChooserContext *ctx = user_data;
    g_assert(ctx);
    g_assert(ctx->row_add_func);

    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GSList *filenames = gtk_file_chooser_get_filenames(chooser);
        if (filenames) {
            for (GSList *f = filenames; f != NULL; f = f->next) {
                gchar *filename = f->data;
                if (filename) {
                    ctx->row_add_func(ctx->model, filename);
                }
            }
            g_slist_free_full(g_steal_pointer(&filenames), g_free);
        }
    }

#if !GTK_CHECK_VERSION(3, 20, 0)
    gtk_widget_destroy(GTK_WIDGET(dialog));
#else
    g_clear_object(&dialog);
#endif

    g_slice_free(FsearchPreferencesFileChooserContext, g_steal_pointer(&ctx));
}

static void
run_file_chooser_dialog(GtkButton *button, FsearchPreferencesFileChooserContext *ctx) {
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

    GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(button));

#if !GTK_CHECK_VERSION(3, 20, 0)
    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select folder"),
                                                    GTK_WINDOW(window),
                                                    action,
                                                    _("_Cancel"),
                                                    GTK_RESPONSE_CANCEL,
                                                    _("_Select"),
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);

    g_signal_connect(dialog, "response", G_CALLBACK(on_file_chooser_dialog_response), ctx);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_widget_show(dialog);
#else
    GtkFileChooserNative *dialog =
        gtk_file_chooser_native_new(_("Select folder"), GTK_WINDOW(window), action, _("_Select"), _("_Cancel"));

    g_signal_connect(dialog, "response", G_CALLBACK(on_file_chooser_native_dialog_response), ctx);
    gtk_native_dialog_set_transient_for(GTK_NATIVE_DIALOG(dialog), GTK_WINDOW(window));
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), true);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
#endif
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
}

static int
compare_int(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

static gint
get_unique_include_id(GtkListStore *store) {
    g_assert(store);
    GtkTreeModel *model = GTK_TREE_MODEL(store);

    // We want to have the smallest possible unique ID >= 0:
    // 1. First we fetch all ids currently present in the model
    g_autoptr(GArray) model_ids = g_array_new(FALSE, FALSE, sizeof(gint));
    GtkTreeIter iter = {};
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gint id = -1;
        gtk_tree_model_get(model, &iter, COL_INCLUDE_ID, &id, -1);
        g_array_append_val(model_ids, id);

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    // 2. Then we sort them ascendingly
    g_array_sort(model_ids, compare_int);

    // 3. Then we find the first ID, starting with 0, which isn't in the array. That's the unique ID we want.
    for (gint i = 0; i < model_ids->len; ++i) {
        gint model_id = g_array_index(model_ids, gint, i);
        if (i != model_id) {
            // Found the smallest unique ID
            return i;
        }
    }

    // The model is either empty or has no gaps in between IDs, 0, 1, 2, ..., model_idx->len - 1
    // Our new unique ID therefore is the length of the array, which is either 0 or 1 greater than the largest ID in the
    // model:
    return (gint)model_ids->len;
}

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
exclude_append_row(GtkListStore *store, gboolean active, const char *path) {
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, COL_EXCLUDE_ACTIVE, active, COL_EXCLUDE_PATH, path, -1);
}

static void
on_exclude_append_new_row(GtkListStore *store, const char *path) {
    exclude_append_row(store, TRUE, path);
}

void
remove_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata) {
    gtk_list_store_remove(GTK_LIST_STORE(model), iter);
}

static bool
include_path_is_unique(GtkListStore *store, const char *new_path) {
    g_return_val_if_fail(store, false);
    g_return_val_if_fail(new_path, false);

    GtkTreeModel *model = GTK_TREE_MODEL(store);

    GtkTreeIter iter = {};
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        g_autofree char *path = NULL;
        gtk_tree_model_get(model, &iter, COL_INCLUDE_PATH, &path, -1);
        if (g_strcmp0(path, new_path) == 0) {
            return false;
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }
    return true;
}

static void
include_append_row(GtkListStore *store,
                   gboolean active,
                   const char *path,
                   gboolean one_file_system,
                   gboolean monitor,
                   gint id) {
    if (!include_path_is_unique(store, path)) {
        return;
    }
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store,
                       &iter,
                       COL_INCLUDE_ACTIVE,
                       active,
                       COL_INCLUDE_PATH,
                       path,
                       COL_INCLUDE_ONE_FS,
                       one_file_system,
                       COL_INCLUDE_MONITOR,
                       monitor,
                       COL_INCLUDE_ID,
                       id,
                       -1);
}

static void
on_include_append_new_row(GtkListStore *store, const char *path) {
    include_append_row(store, TRUE, path, FALSE, FALSE, get_unique_include_id(store));
}

static void
on_exclude_add_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
    ctx->model = self->exclude_model;
    ctx->row_add_func = on_exclude_append_new_row;
    run_file_chooser_dialog(button, ctx);
}

static void
on_exclude_remove_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    gtk_tree_selection_selected_foreach(self->exclude_selection, remove_row, NULL);
}

static void
on_include_add_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    FsearchPreferencesFileChooserContext *ctx = g_slice_new0(FsearchPreferencesFileChooserContext);
    ctx->model = self->include_model;
    ctx->row_add_func = on_include_append_new_row;
    run_file_chooser_dialog(button, ctx);
}

static void
on_include_remove_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    gtk_tree_selection_selected_foreach(self->include_selection, remove_row, NULL);
}

static void
on_column_exclude_active_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *exclude_model = data;
    on_column_toggled(path_str, exclude_model, COL_EXCLUDE_ACTIVE);
}

static void
on_column_include_active_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *include_model = data;
    on_column_toggled(path_str, include_model, COL_INCLUDE_ACTIVE);
}

static void
on_column_include_one_fs_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *include_model = data;
    on_column_toggled(path_str, include_model, COL_INCLUDE_ONE_FS);
}

static void
on_column_include_monitor_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer data) {
    GtkTreeModel *include_model = data;
    on_column_toggled(path_str, include_model, COL_INCLUDE_MONITOR);
}

static void
on_path_entry_changed(GtkEntry *entry, gpointer user_data) {
    GtkWidget *add_path_button = GTK_WIDGET(user_data);
    const char *path = gtk_entry_get_text(entry);
    if (path && g_file_test(path, G_FILE_TEST_IS_DIR)) {
        gtk_widget_set_sensitive(add_path_button, TRUE);
    }
    else {
        gtk_widget_set_sensitive(add_path_button, FALSE);
    }
}

static void
add_path(GtkEntry *entry, GtkListStore *model, RowAddFunc row_add_func) {
    const char *path = gtk_entry_get_text(entry);
    if (path && g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_autoptr(GFile) file = g_file_new_for_path(path);
        g_autofree char *file_path = g_file_get_path(file);
        row_add_func(model, file_path);
    }
}

static void
on_exclude_add_path_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    add_path(GTK_ENTRY(self->exclude_path_entry), self->exclude_model, on_exclude_append_new_row);
}

static void
on_include_add_path_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchDatabasePreferencesWidget *self = FSEARCH_DATABASE_PREFERENCES_WIDGET(user_data);
    add_path(GTK_ENTRY(self->include_path_entry), self->include_model, on_include_append_new_row);
}

static void
on_selection_changed(GtkTreeSelection *selection, gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);
    if (gtk_tree_selection_count_selected_rows(selection) > 0) {
        gtk_widget_set_sensitive(widget, TRUE);
    }
    else {
        gtk_widget_set_sensitive(widget, FALSE);
    }
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
                         G_CALLBACK(on_column_exclude_active_toggled),
                         self->exclude_model);
    column_text_append(self->exclude_list, _("Path"), TRUE, COL_EXCLUDE_PATH);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(self->exclude_list, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

static void
init_include_page(FsearchDatabasePreferencesWidget *self) {
    self->include_model =
        gtk_list_store_new(NUM_INCLUDE_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_INT);
    gtk_tree_view_set_model(self->include_list, GTK_TREE_MODEL(self->include_model));

    column_toggle_append(self->include_list,
                         GTK_TREE_MODEL(self->include_model),
                         _("Active"),
                         COL_INCLUDE_ACTIVE,
                         G_CALLBACK(on_column_include_active_toggled),
                         self->include_model);
    column_text_append(self->include_list, _("Path"), TRUE, COL_INCLUDE_PATH);
    column_toggle_append(self->include_list,
                         GTK_TREE_MODEL(self->include_model),
                         _("One Filesystem"),
                         COL_INCLUDE_ONE_FS,
                         G_CALLBACK(on_column_include_one_fs_toggled),
                         self->include_model);
#if (defined HAVE_INOTIFY) || (defined HAVE_FANOTIFY)
    column_toggle_append(self->include_list,
                         GTK_TREE_MODEL(self->include_model),
                         _("Monitor"),
                         COL_INCLUDE_MONITOR,
                         G_CALLBACK(on_column_include_monitor_toggled),
                         self->include_model);
#endif

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
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        include_append_row(self->include_model,
                           fsearch_database_include_get_active(include),
                           fsearch_database_include_get_path(include),
                           fsearch_database_include_get_one_file_system(include),
                           fsearch_database_include_get_monitored(include),
                           fsearch_database_include_get_id(include));
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
        FsearchDatabaseExclude *exclude = g_ptr_array_index(excludes, i);
        exclude_append_row(self->exclude_model,
                           fsearch_database_exclude_get_active(exclude),
                           fsearch_database_exclude_get_path(exclude));
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

    if (fsearch_database_try_get_database_info(self->db, &self->info) == FSEARCH_RESULT_SUCCESS) {
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
                                                    FSEARCH_TYPE_DATABASE,
                                                    (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/io/github/cboxdoerfer/fsearch/ui/"
                                                "fsearch_database_preferences_widget.ui");

    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, include_path_entry);

    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_path_entry);

    gtk_widget_class_bind_template_child(widget_class, FsearchDatabasePreferencesWidget, exclude_hidden_items_button);

    gtk_widget_class_bind_template_callback(widget_class, on_include_add_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_include_add_path_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_include_remove_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_add_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_add_path_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_exclude_remove_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_path_entry_changed);
    gtk_widget_class_bind_template_callback(widget_class, on_selection_changed);
}

static void
fsearch_database_preferences_widget_init(FsearchDatabasePreferencesWidget *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    init_include_page(self);
    init_exclude_page(self);
}

FsearchDatabasePreferencesWidget *
fsearch_database_preferences_widget_new(FsearchDatabase *db) {
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
        gboolean monitor = FALSE;
        gint id = -1;
        gtk_tree_model_get(model,
                           &iter,
                           COL_INCLUDE_PATH,
                           &path,
                           COL_INCLUDE_ACTIVE,
                           &active,
                           COL_INCLUDE_ONE_FS,
                           &one_file_system,
                           COL_INCLUDE_MONITOR,
                           &monitor,
                           COL_INCLUDE_ID,
                           &id,
                           -1);

        if (path) {
            fsearch_database_include_manager_add(
                include_manager,
                fsearch_database_include_new(path, active, one_file_system, monitor, FALSE, id));
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

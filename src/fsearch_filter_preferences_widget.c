#include "fsearch_filter_preferences_widget.h"

#include <glib/gi18n.h>

#include "fsearch_filter_editor.h"

struct _FsearchFilterPreferencesWidget {
    GtkBox parent_instance;

    FsearchFilterManager *filters;

    GtkTreeView *filter_list;
    GtkTreeSelection *filter_list_selection;
    GtkListStore *filter_model;
    GtkWidget *filter_add_button;
    GtkWidget *filter_edit_button;
    GtkWidget *filter_remove_button;
    GtkWidget *filter_reset_to_defaults_button;
};

enum { PROP_0, PROP_FILTER_MANAGER, NUM_PROPERTIES };

enum { COL_FILTER_NAME, COL_FILTER_MACRO, COL_FILTER_QUERY, NUM_FILTER_COLUMNS };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_FINAL_TYPE(FsearchFilterPreferencesWidget, fsearch_filter_preferences_widget, GTK_TYPE_BOX)

static void
column_text_append(GtkTreeView *view, const char *name, gboolean expand, int id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(name, renderer, "text", id, NULL);
    gtk_tree_view_column_set_expand(col, expand);
    gtk_tree_view_column_set_sort_column_id(col, id);
    gtk_tree_view_append_column(view, col);
}

static FsearchFilter *
filter_get_selected(FsearchFilterPreferencesWidget *self) {
    GtkTreeIter iter = {0};
    GtkTreeModel *model = NULL;
    gboolean selected = gtk_tree_selection_get_selected(self->filter_list_selection, &model, &iter);
    if (!selected) {
        return NULL;
    }

    g_autofree char *name = NULL;
    gtk_tree_model_get(model, &iter, 0, &name, -1);
    g_assert(name);

    return fsearch_filter_manager_get_filter_for_name(self->filters, name);
}

static void
filter_row_add(GtkListStore *filter_list_model, FsearchFilter *filter) {
    g_return_if_fail(filter);

    GtkTreeIter iter;
    gtk_list_store_append(filter_list_model, &iter);
    gtk_list_store_set(filter_list_model,
                       &iter,
                       COL_FILTER_NAME,
                       filter->name,
                       COL_FILTER_MACRO,
                       filter->macro,
                       COL_FILTER_QUERY,
                       filter->query,
                       -1);
}

static void
filter_list_update(GtkListStore *filter_list_model, FsearchFilterManager *filters) {
    gtk_list_store_clear(filter_list_model);
    for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(filters); ++i) {
        FsearchFilter *filter = fsearch_filter_manager_get_filter(filters, i);
        filter_row_add(filter_list_model, filter);
        g_clear_pointer(&filter, fsearch_filter_unref);
    }
}

static void
on_filter_editor_edit_finished(FsearchFilter *old_filter,
                               char *name,
                               char *macro,
                               char *query,
                               FsearchQueryFlags flags,
                               gpointer data) {
    FsearchFilterPreferencesWidget *self = data;
    fsearch_filter_manager_edit(self->filters, old_filter, name, macro, query, flags);
    g_clear_pointer(&name, g_free);
    g_clear_pointer(&macro, g_free);
    g_clear_pointer(&query, g_free);
    filter_list_update(self->filter_model, self->filters);
}

static void
on_filter_editor_add_finished(FsearchFilter *old_filter,
                              char *name,
                              char *macro,
                              char *query,
                              FsearchQueryFlags flags,
                              gpointer data) {
    FsearchFilterPreferencesWidget *self = data;
    if (!name) {
        return;
    }

    FsearchFilter *filter = fsearch_filter_new(name, macro, query, flags);
    g_clear_pointer(&name, g_free);
    g_clear_pointer(&macro, g_free);
    g_clear_pointer(&query, g_free);

    fsearch_filter_manager_append_filter(self->filters, filter);
    filter_row_add(self->filter_model, filter);
    g_clear_pointer(&filter, fsearch_filter_unref);
}

static void
on_filter_add_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(user_data);
    GtkWidget *top_level = gtk_widget_get_toplevel(GTK_WIDGET(self));
    fsearch_filter_editor_run(_("Add filter"), GTK_WINDOW(top_level), NULL, on_filter_editor_add_finished, self);
}

static void
remove_list_store_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer userdata) {
    gtk_list_store_remove(GTK_LIST_STORE(model), iter);
}

static void
on_filter_remove_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(user_data);
    FsearchFilter *filter = filter_get_selected(self);
    if (filter) {
        fsearch_filter_manager_remove(self->filters, filter);
        g_clear_pointer(&filter, fsearch_filter_unref);
        gtk_tree_selection_selected_foreach(self->filter_list_selection, remove_list_store_row, NULL);
    }
}

static void
on_filter_edit_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(user_data);
    GtkWidget *top_level = gtk_widget_get_toplevel(GTK_WIDGET(self));
    fsearch_filter_editor_run(_("Edit filter"),
                              GTK_WINDOW(top_level),
                              filter_get_selected(self),
                              on_filter_editor_edit_finished,
                              self);
}

static void
on_filter_reset_to_defaults_button_clicked(GtkButton *button, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(user_data);

    g_clear_pointer(&self->filters, fsearch_filter_manager_unref);
    self->filters = fsearch_filter_manager_new_with_defaults();

    filter_list_update(self->filter_model, self->filters);
}

static void
on_filter_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(user_data);
    gboolean selected = gtk_tree_selection_get_selected(self->filter_list_selection, NULL, NULL);
    if (!selected) {
        return;
    }
    // Activating a filter row should open the filter for editing
    on_filter_edit_button_clicked(GTK_BUTTON(self->filter_edit_button), self);
}

static void
on_filter_model_reordered(GtkTreeModel *tree_model,
                          GtkTreePath *path,
                          GtkTreeIter *iter,
                          gpointer new_order,
                          gpointer user_data) {
    FsearchFilterPreferencesWidget *self = user_data;
    // Tell the filter manager about the new order
    guint n_filters = gtk_tree_model_iter_n_children(tree_model, NULL);
    fsearch_filter_manager_reorder(self->filters, new_order, n_filters);
}

static void
on_filter_list_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
    FsearchFilterPreferencesWidget *self = user_data;
    // The filter remove and edit buttons may only be sensitive if one filter is selected
    gboolean selected = gtk_tree_selection_get_selected(sel, NULL, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(self->filter_remove_button), selected);
    gtk_widget_set_sensitive(GTK_WIDGET(self->filter_edit_button), selected);
    return;
}

static void
fsearch_filter_preferences_widget_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(object);

    switch (prop_id) {
    case PROP_FILTER_MANAGER:
        g_value_set_boxed(value, self->filters);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_filter_preferences_widget_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(object);

    switch (prop_id) {
    case PROP_FILTER_MANAGER:
        // Ensure that we have our own copy of the filter manager to work with
        self->filters = fsearch_filter_manager_copy(g_value_get_boxed(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_filter_preferences_widget_dispose(GObject *object) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(object);

    g_clear_pointer(&self->filters, fsearch_filter_manager_unref);

    G_OBJECT_CLASS(fsearch_filter_preferences_widget_parent_class)->dispose(object);
}

static void
fsearch_filter_preferences_widget_constructed(GObject *object) {
    FsearchFilterPreferencesWidget *self = FSEARCH_FILTER_PREFERENCES_WIDGET(object);

    // Fill the list store with filters
    for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(self->filters); ++i) {
        GtkTreeIter iter = {};
        FsearchFilter *filter = fsearch_filter_manager_get_filter(self->filters, i);
        gtk_list_store_append(self->filter_model, &iter);
        gtk_list_store_set(self->filter_model,
                           &iter,
                           COL_FILTER_NAME,
                           filter->name,
                           COL_FILTER_MACRO,
                           filter->macro,
                           COL_FILTER_QUERY,
                           filter->query,
                           -1);
        g_clear_pointer(&filter, fsearch_filter_unref);
    }

    G_OBJECT_CLASS(fsearch_filter_preferences_widget_parent_class)->constructed(object);
}

static void
fsearch_filter_preferences_widget_class_init(FsearchFilterPreferencesWidgetClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = fsearch_filter_preferences_widget_dispose;
    object_class->constructed = fsearch_filter_preferences_widget_constructed;
    object_class->set_property = fsearch_filter_preferences_widget_set_property;
    object_class->get_property = fsearch_filter_preferences_widget_get_property;

    properties[PROP_FILTER_MANAGER] =
        g_param_spec_boxed("filter-manager",
                           "Filter Manager",
                           "The filter manager which will be represented and edited in this widget",
                           FSEARCH_TYPE_FILTER_MANAGER,
                           (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/io/github/cboxdoerfer/fsearch/ui/"
                                                "fsearch_filter_preferences_widget.ui");

    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_list);
    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_list_selection);
    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_add_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_remove_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_edit_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchFilterPreferencesWidget, filter_reset_to_defaults_button);

    gtk_widget_class_bind_template_callback(widget_class, on_filter_add_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_remove_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_edit_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_reset_to_defaults_button_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_list_row_activated);
    gtk_widget_class_bind_template_callback(widget_class, on_filter_list_selection_changed);
}

static void
fsearch_filter_preferences_widget_init(FsearchFilterPreferencesWidget *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    self->filter_model = gtk_list_store_new(NUM_FILTER_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    g_signal_connect(self->filter_model, "rows-reordered", G_CALLBACK(on_filter_model_reordered), self);

    gtk_tree_view_set_model(self->filter_list, GTK_TREE_MODEL(self->filter_model));

    column_text_append(self->filter_list, _("Name"), FALSE, COL_FILTER_NAME);
    column_text_append(self->filter_list, _("Macro"), TRUE, COL_FILTER_MACRO);
    column_text_append(self->filter_list, _("Query"), TRUE, COL_FILTER_QUERY);

    // Workaround for GTK bug: https://gitlab.gnome.org/GNOME/gtk/-/issues/3084
    g_signal_connect(self->filter_list, "realize", G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
}

FsearchFilterPreferencesWidget *
fsearch_filter_preferences_widget_new(FsearchFilterManager *filters) {
    return g_object_new(FSEARCH_FILTER_PREFERENCES_WIDGET_TYPE, "filter-manager", filters, NULL);
}

FsearchFilterManager *
fsearch_filter_preferences_widget_get_filter_manager(FsearchFilterPreferencesWidget *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_filter_manager_copy(self->filters);
}

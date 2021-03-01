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

#define _GNU_SOURCE
#include "list_model.h"
#include <ctype.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "btree.h"
#include "database_search.h"
#include "debug.h"
#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_timer.h"

/* boring declarations of local functions */

static void
list_model_init(ListModel *pkg_tree);

static void
list_model_class_init(ListModelClass *klass);

static void
list_model_tree_model_init(GtkTreeModelIface *iface);

static void
list_model_model_sortable_init(GtkTreeSortableIface *iface);

static void
list_model_finalize(GObject *object);

static GtkTreeModelFlags
list_model_get_flags(GtkTreeModel *tree_model);

static gint
list_model_get_n_columns(GtkTreeModel *tree_model);

static GType
list_model_get_column_type(GtkTreeModel *tree_model, gint index);

static gboolean
list_model_get_iter(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreePath *path);

static GtkTreePath *
list_model_get_path(GtkTreeModel *tree_model, GtkTreeIter *iter);

static void
list_model_get_value(GtkTreeModel *tree_model, GtkTreeIter *iter, gint column, GValue *value);

static gboolean
list_model_iter_next(GtkTreeModel *tree_model, GtkTreeIter *iter);

static gboolean
list_model_iter_children(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent);

static gboolean
list_model_iter_has_child(GtkTreeModel *tree_model, GtkTreeIter *iter);

static gint
list_model_iter_n_children(GtkTreeModel *tree_model, GtkTreeIter *iter);

static gboolean
list_model_iter_nth_child(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent, gint n);

static gboolean
list_model_iter_parent(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *child);

static gboolean
list_model_sortable_get_sort_column_id(GtkTreeSortable *sortable, gint *sort_col_id, GtkSortType *order);

static void
list_model_sortable_set_sort_column_id(GtkTreeSortable *sortable, gint sort_col_id, GtkSortType order);

static void
list_model_sortable_set_sort_func(GtkTreeSortable *sortable,
                                  gint sort_col_id,
                                  GtkTreeIterCompareFunc sort_func,
                                  gpointer user_data,
                                  GDestroyNotify destroy_func);

static void
list_model_sortable_set_default_sort_func(GtkTreeSortable *sortable,
                                          GtkTreeIterCompareFunc sort_func,
                                          gpointer user_data,
                                          GDestroyNotify destroy_func);

static gboolean
list_model_sortable_has_default_sort_func(GtkTreeSortable *sortable);

static GObjectClass *parent_class = NULL; /* GObject stuff - nothing to worry about */

static gchar *
get_mimetype(const gchar *path) {
    if (!path) {
        return NULL;
    }
    gchar *content_type = g_content_type_guess(path, NULL, 1, NULL);
    if (content_type) {
        gchar *mimetype = g_content_type_get_description(content_type);
        g_free(content_type);
        content_type = NULL;
        return mimetype;
    }
    return NULL;
}

static gchar *
get_file_type(DatabaseSearchEntry *entry, const gchar *path) {
    gchar *type = NULL;
    BTreeNode *node = db_search_entry_get_node(entry);
    if (node->is_dir) {
        type = strdup("Folder");
    }
    else {
        type = get_mimetype(path);
    }
    if (type == NULL) {
        type = strdup("Unknown Type");
    }
    return type;
}

static void
list_model_clear(ListModel *list_model) {
    if (list_model->results) {
        g_ptr_array_free(list_model->results, TRUE);
        list_model->results = NULL;
    }
    if (list_model->node_path) {
        g_string_free(list_model->node_path, TRUE);
        list_model->node_path = NULL;
    }
    if (list_model->parent_path) {
        g_string_free(list_model->parent_path, TRUE);
        list_model->parent_path = NULL;
    }
}

/*****************************************************************************
 *
 *  list_model_get_type: here we register our new type and its interfaces
 *                        with the type system. If you want to implement
 *                        additional interfaces like GtkTreeSortable, you
 *                        will need to do it here.
 *
 *****************************************************************************/

GType
list_model_get_type(void) {
    static GType list_model_type = 0;

    /* Some boilerplate type registration stuff */
    if (list_model_type == 0) {
        static const GTypeInfo list_model_info = {sizeof(ListModelClass),
                                                  NULL, /* base_init */
                                                  NULL, /* base_finalize */
                                                  (GClassInitFunc)list_model_class_init,
                                                  NULL, /* class finalize */
                                                  NULL, /* class_data */
                                                  sizeof(ListModel),
                                                  0, /* n_preallocs */
                                                  (GInstanceInitFunc)list_model_init,
                                                  NULL};
        static const GInterfaceInfo tree_model_info = {(GInterfaceInitFunc)list_model_tree_model_init, NULL, NULL};
        static const GInterfaceInfo tree_model_sortable_info = {(GInterfaceInitFunc)list_model_model_sortable_init,
                                                                NULL,
                                                                NULL};

        /* First register the new derived type with the GObject type system */
        list_model_type = g_type_register_static(G_TYPE_OBJECT, "ListModel", &list_model_info, (GTypeFlags)0);

        /* Now register our GtkTreeModel interface with the type system */
        g_type_add_interface_static(list_model_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
        g_type_add_interface_static(list_model_type, GTK_TYPE_TREE_SORTABLE, &tree_model_sortable_info);
    }

    return list_model_type;
}

/*****************************************************************************
 *
 *  list_model_class_init: more boilerplate GObject/GType stuff.
 *                          Init callback for the type system,
 *                          called once when our new class is created.
 *
 *****************************************************************************/

static void
list_model_class_init(ListModelClass *klass) {
    GObjectClass *object_class;

    parent_class = (GObjectClass *)g_type_class_peek_parent(klass);
    object_class = (GObjectClass *)klass;

    object_class->finalize = list_model_finalize;
}

/*****************************************************************************
 *
 *  list_model_tree_model_init: init callback for the interface registration
 *                               in list_model_get_type. Here we override
 *                               the GtkTreeModel interface functions that
 *                               we implement.
 *
 *****************************************************************************/

static void
list_model_tree_model_init(GtkTreeModelIface *iface) {
    iface->get_flags = list_model_get_flags;
    iface->get_n_columns = list_model_get_n_columns;
    iface->get_column_type = list_model_get_column_type;
    iface->get_iter = list_model_get_iter;
    iface->get_path = list_model_get_path;
    iface->get_value = list_model_get_value;
    iface->iter_next = list_model_iter_next;
    iface->iter_children = list_model_iter_children;
    iface->iter_has_child = list_model_iter_has_child;
    iface->iter_n_children = list_model_iter_n_children;
    iface->iter_nth_child = list_model_iter_nth_child;
    iface->iter_parent = list_model_iter_parent;
}

static void
list_model_model_sortable_init(GtkTreeSortableIface *iface) {
    iface->get_sort_column_id = list_model_sortable_get_sort_column_id;
    iface->set_sort_column_id = list_model_sortable_set_sort_column_id;
    iface->set_sort_func = list_model_sortable_set_sort_func;
    iface->set_default_sort_func = list_model_sortable_set_default_sort_func;
    iface->has_default_sort_func = list_model_sortable_has_default_sort_func;
}

/*****************************************************************************
 *
 *  list_model_init: this is called every time a new custom list object
 *                    instance is created (we do that in list_model_new).
 *                    Initialise the list structure's fields here.
 *
 *****************************************************************************/

static void
list_model_init(ListModel *list_model) {
    list_model->n_columns = LIST_MODEL_N_COLUMNS;

    list_model->column_types[0] = G_TYPE_POINTER; /* LIST_MODEL_COL_RECORD    */
    list_model->column_types[1] = G_TYPE_ICON;    /* LIST_MODEL_ICON      */
    list_model->column_types[2] = G_TYPE_STRING;  /* LIST_MODEL_COL_NAME      */
    list_model->column_types[3] = G_TYPE_STRING;  /* LIST_MODEL_COL_PATH */
    list_model->column_types[4] = G_TYPE_STRING;  /* LIST_MODEL_COL_TYPE */
    list_model->column_types[5] = G_TYPE_STRING;  /* LIST_MODEL_COL_SIZE */
    list_model->column_types[6] = G_TYPE_STRING;  /* LIST_MODEL_COL_CHANGED */

    g_assert(LIST_MODEL_N_COLUMNS == 7);

    list_model->results = NULL;
    list_model->node_path = g_string_new(NULL);
    list_model->parent_path = g_string_new(NULL);

    list_model->sort_id = SORT_ID_NONE;
    list_model->sort_order = GTK_SORT_ASCENDING;

    list_model->stamp = g_random_int(); /* Random int to check whether an iter
                                           belongs to our model */
}

/*****************************************************************************
 *
 *  list_model_finalize: this is called just before a custom list is
 *                        destroyed. Free dynamically allocated memory here.
 *
 *****************************************************************************/

static void
list_model_finalize(GObject *object) {
    ListModel *list_model = LIST_MODEL(object);
    list_model_clear(list_model);

    /* must chain up - finalize parent */
    (*parent_class->finalize)(object);
}

/*****************************************************************************
 *
 *  list_model_get_flags: tells the rest of the world whether our tree model
 *                         has any special characteristics. In our case,
 *                         we have a list model (instead of a tree), and each
 *                         tree iter is valid as long as the row in question
 *                         exists, as it only contains a pointer to our struct.
 *
 *****************************************************************************/

static GtkTreeModelFlags
list_model_get_flags(GtkTreeModel *tree_model) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), (GtkTreeModelFlags)0);

    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

/*****************************************************************************
 *
 *  list_model_get_n_columns: tells the rest of the world how many data
 *                             columns we export via the tree model interface
 *
 *****************************************************************************/

static gint
list_model_get_n_columns(GtkTreeModel *tree_model) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), 0);

    return LIST_MODEL(tree_model)->n_columns;
}

/*****************************************************************************
 *
 *  list_model_get_column_type: tells the rest of the world which type of
 *                               data an exported model column contains
 *
 *****************************************************************************/

static GType
list_model_get_column_type(GtkTreeModel *tree_model, gint index) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), G_TYPE_INVALID);
    g_return_val_if_fail(index < LIST_MODEL(tree_model)->n_columns && index >= 0, G_TYPE_INVALID);

    return LIST_MODEL(tree_model)->column_types[index];
}

/*****************************************************************************
 *
 *  list_model_get_iter: converts a tree path (physical position) into a
 *                        tree iter structure (the content of the iter
 *                        fields will only be used internally by our model).
 *                        We simply store a pointer to our DatabaseEntry
 *                        structure that represents that row in the tree iter.
 *
 *****************************************************************************/

static gboolean
list_model_get_iter(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreePath *path) {
    g_assert(IS_LIST_MODEL(tree_model));
    g_assert(path != NULL);

    ListModel *list_model = LIST_MODEL(tree_model);

    gint *indices = gtk_tree_path_get_indices(path);
    const gint depth = gtk_tree_path_get_depth(path);

    /* we do not allow children */
    g_assert(depth == 1); /* depth 1 = top level; a list only has top level
                             nodes and no children */

    const gint n = indices[0]; /* the n-th top level row */

    if (!list_model->results || n >= list_model->results->len || n < 0)
        return FALSE;

    DatabaseSearchEntry *entry = g_ptr_array_index(list_model->results, n);

    g_assert(entry != NULL);
    g_assert(db_search_entry_get_pos(entry) == n);

    /* We simply store a pointer to our custom entry in the iter */
    iter->stamp = list_model->stamp;
    iter->user_data = entry;
    iter->user_data2 = NULL; /* unused */
    iter->user_data3 = NULL; /* unused */

    return TRUE;
}

/*****************************************************************************
 *
 *  list_model_get_path: converts a tree iter into a tree path (ie. the
 *                        physical position of that row in the list).
 *
 *****************************************************************************/

static GtkTreePath *
list_model_get_path(GtkTreeModel *tree_model, GtkTreeIter *iter) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), NULL);
    g_return_val_if_fail(iter != NULL, NULL);
    g_return_val_if_fail(iter->user_data != NULL, NULL);

    GtkTreePath *path = gtk_tree_path_new();
    DatabaseSearchEntry *entry = (DatabaseSearchEntry *)iter->user_data;
    gtk_tree_path_append_index(path, db_search_entry_get_pos(entry));
    return path;
}

/*****************************************************************************
 *
 *  list_model_get_value: Returns a row's exported data columns
 *                         (_get_value is what gtk_tree_model_get uses)
 *
 *****************************************************************************/

static void
list_model_get_value(GtkTreeModel *tree_model, GtkTreeIter *iter, gint column, GValue *value) {
    g_return_if_fail(IS_LIST_MODEL(tree_model));
    g_return_if_fail(iter != NULL);
    g_return_if_fail(column < LIST_MODEL(tree_model)->n_columns);

    DatabaseSearchEntry *record = (DatabaseSearchEntry *)iter->user_data;
    g_return_if_fail(record != NULL);

    ListModel *list_model = LIST_MODEL(tree_model);

    if (db_search_entry_get_pos(record) >= list_model->results->len)
        g_return_if_reached();

    char output[100] = "";

    BTreeNode *node = db_search_entry_get_node(record);

    GString *node_path = list_model->node_path;
    GString *parent_path = list_model->parent_path;
    if (!list_model->node_cached || node->parent != list_model->node_cached->parent) {
        g_string_erase(parent_path, 0, -1);
        char path[PATH_MAX] = "";
        btree_node_get_path(node, path, sizeof(path));
        g_string_append(parent_path, path);
    }
    if (node != list_model->node_cached) {
        g_string_erase(node_path, 0, -1);
        g_string_append(node_path, parent_path->str);
        g_string_append_c(node_path, '/');
        g_string_append(node_path, node->name);
    }

    list_model->node_cached = node;

    time_t mtime = node->mtime;

    g_value_init(value, list_model->column_types[column]);
    switch (column) {
    case LIST_MODEL_COL_RECORD:
        g_value_set_pointer(value, record);
        break;

    case LIST_MODEL_COL_ICON: {
        GIcon *icon = NULL;
        GFile *g_file = g_file_new_for_path(node_path->str);
        if (g_file) {
            GFileInfo *file_info = g_file_query_info(g_file, "standard::icon", 0, NULL, NULL);
            if (file_info) {
                icon = g_file_info_get_icon(file_info);
                if (icon) {
                    g_value_set_object(value, icon);
                }
                g_object_unref(file_info);
                file_info = NULL;
            }
            g_object_unref(g_file);
            g_file = NULL;
        }

        if (!icon) {
            icon = g_icon_new_for_string("image-missing", NULL);
            if (icon) {
                g_value_take_object(value, icon);
                icon = NULL;
            }
        }
    } break;

    case LIST_MODEL_COL_NAME:
        g_value_take_string(value, g_filename_display_name(node->name));
        break;

    case LIST_MODEL_COL_PATH:
        g_value_take_string(value, g_filename_display_name(parent_path->str));
        break;

    case LIST_MODEL_COL_SIZE:
        if (node->is_dir) {
            uint32_t num_children = btree_node_n_children(node);
            if (num_children == 1) {
                snprintf(output, sizeof(output), "%d Item", num_children);
            }
            else {
                snprintf(output, sizeof(output), "%d Items", num_children);
            }
            g_value_take_string(value, strdup(output));
        }
        else {
            FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
            char *formatted_size = NULL;
            if (config->show_base_2_units) {
                formatted_size = g_format_size_full(node->size, G_FORMAT_SIZE_IEC_UNITS);
            }
            else {
                formatted_size = g_format_size_full(node->size, G_FORMAT_SIZE_DEFAULT);
            }
            g_value_take_string(value, formatted_size);
        }
        break;

    case LIST_MODEL_COL_TYPE: {
        char *mime_type = get_file_type(record, node_path->str);
        g_value_take_string(value, mime_type);
    } break;

    case LIST_MODEL_COL_CHANGED:
        strftime(output,
                 sizeof(output),
                 "%Y-%m-%d %H:%M", //"%Y-%m-%d %H:%M",
                 localtime(&mtime));
        g_value_take_string(value, strdup(output));
        break;
    }
}

/*****************************************************************************
 *
 *  list_model_iter_next: Takes an iter structure and sets it to point
 *                         to the next row.
 *
 *****************************************************************************/

static gboolean
list_model_iter_next(GtkTreeModel *tree_model, GtkTreeIter *iter) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), FALSE);

    if (iter == NULL || iter->user_data == NULL)
        return FALSE;

    ListModel *list_model = LIST_MODEL(tree_model);
    DatabaseSearchEntry *record = (DatabaseSearchEntry *)iter->user_data;

    const uint32_t new_results_pos = db_search_entry_get_pos(record) + 1;
    /* Is this the last record in the list? */
    if (new_results_pos >= list_model->results->len)
        return FALSE;

    DatabaseSearchEntry *nextrecord = g_ptr_array_index(list_model->results, new_results_pos);

    g_assert(nextrecord != NULL);
    g_assert(db_search_entry_get_pos(nextrecord) == new_results_pos);

    iter->stamp = list_model->stamp;
    iter->user_data = nextrecord;

    return TRUE;
}

/*****************************************************************************
 *
 *  list_model_iter_children: Returns TRUE or FALSE depending on whether
 *                             the row specified by 'parent' has any children.
 *                             If it has children, then 'iter' is set to
 *                             point to the first child. Special case: if
 *                             'parent' is NULL, then the first top-level
 *                             row should be returned if it exists.
 *
 *****************************************************************************/

static gboolean
list_model_iter_children(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent) {
    g_return_val_if_fail(parent == NULL || parent->user_data != NULL, FALSE);

    /* this is a list, nodes have no children */
    if (parent)
        return FALSE;

    /* parent == NULL is a special case; we need to return the first top-level
     * row */

    g_return_val_if_fail(IS_LIST_MODEL(tree_model), FALSE);

    ListModel *list_model = LIST_MODEL(tree_model);

    /* No rows => no first row */
    if (list_model->results->len == 0)
        return FALSE;

    /* Set iter to first item in list */
    iter->stamp = list_model->stamp;
    iter->user_data = g_ptr_array_index(list_model->results, 0);

    return TRUE;
}

/*****************************************************************************
 *
 *  list_model_iter_has_child: Returns TRUE or FALSE depending on whether
 *                              the row specified by 'iter' has any children.
 *                              We only have a list and thus no children.
 *
 *****************************************************************************/

static gboolean
list_model_iter_has_child(GtkTreeModel *tree_model, GtkTreeIter *iter) {
    return FALSE;
}

/*****************************************************************************
 *
 *  list_model_iter_n_children: Returns the number of children the row
 *                               specified by 'iter' has. This is usually 0,
 *                               as we only have a list and thus do not have
 *                               any children to any rows. A special case is
 *                               when 'iter' is NULL, in which case we need
 *                               to return the number of top-level nodes,
 *                               ie. the number of rows in our list.
 *
 *****************************************************************************/

static gint
list_model_iter_n_children(GtkTreeModel *tree_model, GtkTreeIter *iter) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), -1);
    g_return_val_if_fail(iter == NULL || iter->user_data != NULL, FALSE);

    ListModel *list_model = LIST_MODEL(tree_model);

    /* special case: if iter == NULL, return number of top-level rows */
    if (!iter && list_model->results)
        return list_model->results->len;

    return 0; /* otherwise, this is easy again for a list */
}

/*****************************************************************************
 *
 *  list_model_iter_nth_child: If the row specified by 'parent' has any
 *                              children, set 'iter' to the n-th child and
 *                              return TRUE if it exists, otherwise FALSE.
 *                              A special case is when 'parent' is NULL, in
 *                              which case we need to set 'iter' to the n-th
 *                              row if it exists.
 *
 *****************************************************************************/

static gboolean
list_model_iter_nth_child(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent, gint n) {
    g_return_val_if_fail(IS_LIST_MODEL(tree_model), FALSE);

    /* a list has only top-level rows */
    if (parent)
        return FALSE;

    /* special case: if parent == NULL, set iter to n-th top-level row */

    ListModel *list_model = LIST_MODEL(tree_model);
    if (n >= list_model->results->len)
        return FALSE;

    DatabaseSearchEntry *record = g_ptr_array_index(list_model->results, n);

    g_assert(record != NULL);
    g_assert(db_search_entry_get_pos(record) == n);

    iter->stamp = list_model->stamp;
    iter->user_data = record;

    return TRUE;
}

/*****************************************************************************
 *
 *  list_model_iter_parent: Point 'iter' to the parent node of 'child'. As
 *                           we have a list and thus no children and no
 *                           parents of children, we can just return FALSE.
 *
 *****************************************************************************/

static gboolean
list_model_iter_parent(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *child) {
    return FALSE;
}

void
list_model_remove_entry(ListModel *list, DatabaseSearch *search, DatabaseSearchEntry *entry) {
    guint row = db_search_entry_get_pos(entry);
    db_search_remove_entry(search, entry);
    GtkTreePath *path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, row);
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(list), path);
    gtk_tree_path_free(path);
}

/*****************************************************************************
 *
 *  list_model_new:  This is what you use in your own code to create a
 *                    new custom list tree model for you to use.
 *
 *****************************************************************************/

ListModel *
list_model_new(void) {
    ListModel *new_list_model;

    new_list_model = (ListModel *)g_object_new(LIST_MODEL_TYPE, NULL);

    g_assert(new_list_model != NULL);

    return new_list_model;
}

/*****************************************************************************
 *
 *  list_model_append_record:  Empty lists are boring. This function can
 *                              be used in your own code to add rows to the
 *                              list. Note how we emit the "row-inserted"
 *                              signal after we have appended the row
 *                              internally, so the tree view and other
 *                              interested objects know about the new row.
 *
 *****************************************************************************/

static gboolean
list_model_sortable_get_sort_column_id(GtkTreeSortable *sortable, gint *sort_col_id, GtkSortType *order) {
    ListModel *list_model;

    g_return_val_if_fail(sortable != NULL, FALSE);
    g_return_val_if_fail(IS_LIST_MODEL(sortable), FALSE);

    list_model = LIST_MODEL(sortable);

    if (sort_col_id)
        *sort_col_id = list_model->sort_id;

    if (order)
        *order = list_model->sort_order;

    return TRUE;
}

static void
list_model_sortable_set_sort_column_id(GtkTreeSortable *sortable, gint sort_col_id, GtkSortType order) {
    ListModel *list_model;

    g_return_if_fail(sortable != NULL);
    g_return_if_fail(IS_LIST_MODEL(sortable));

    list_model = LIST_MODEL(sortable);

    const gint prev_sort_id = list_model->sort_id;
    const gint prev_order = list_model->sort_order;

    if (prev_sort_id == sort_col_id && prev_order == order) {
        return;
    }

    list_model->sort_id = sort_col_id;
    list_model->sort_order = order;

    list_model_sort(list_model);

    /* emit "sort-column-changed" signal to tell any tree views
     *  that the sort column has changed (so the little arrow
     *  in the column header of the sort column is drawn
     *  in the right column)                                     */

    gtk_tree_sortable_sort_column_changed(sortable);
}

static void
list_model_sortable_set_sort_func(GtkTreeSortable *sortable,
                                  gint sort_col_id,
                                  GtkTreeIterCompareFunc sort_func,
                                  gpointer user_data,
                                  GDestroyNotify destroy_func) {
    g_warning("%s is not supported by the ListModel model.\n", __FUNCTION__);
}

static void
list_model_sortable_set_default_sort_func(GtkTreeSortable *sortable,
                                          GtkTreeIterCompareFunc sort_func,
                                          gpointer user_data,
                                          GDestroyNotify destroy_func) {
    g_warning("%s is not supported by the ListModel model.\n", __FUNCTION__);
}

static gboolean
list_model_sortable_has_default_sort_func(GtkTreeSortable *sortable) {
    return FALSE;
}

static gint
list_model_compare_path(BTreeNode *a, BTreeNode *b) {
    if (!a || !b) {
        return 0;
    }
    const int32_t a_depth = btree_node_depth(a);
    const int32_t b_depth = btree_node_depth(b);
    char *a_parents[a_depth + 1];
    char *b_parents[b_depth + 1];
    a_parents[a_depth] = NULL;
    b_parents[b_depth] = NULL;

    BTreeNode *temp = a;
    for (int32_t i = a_depth - 1; i >= 0 && temp; i--) {
        a_parents[i] = temp->name;
        temp = temp->parent;
    }
    temp = b;
    for (int32_t i = b_depth - 1; i >= 0 && temp; i--) {
        b_parents[i] = temp->name;
        temp = temp->parent;
    }

    uint32_t i = 0;
    char *a_name = a_parents[i];
    char *b_name = b_parents[i];

    while (a_name && b_name) {
        int res = strverscmp(a_name, b_name);
        if (res != 0) {
            return res;
        }
        i++;
        a_name = a_parents[i];
        b_name = b_parents[i];
    }
    return a_depth - b_depth;
}

static gint
list_model_compare_records(gint sort_id, DatabaseSearchEntry *a, DatabaseSearchEntry *b) {
    BTreeNode *node_a = db_search_entry_get_node(a);
    BTreeNode *node_b = db_search_entry_get_node(b);

    const bool is_dir_a = node_a->is_dir;
    const bool is_dir_b = node_b->is_dir;

    const gchar *name_a = node_a->name;
    const gchar *name_b = node_b->name;

    gchar *type_a = NULL;
    gchar *type_b = NULL;

    gchar path_a[PATH_MAX] = "";
    gchar path_b[PATH_MAX] = "";

    gint return_val = 0;

    switch (sort_id) {
    case SORT_ID_NONE:
        return 0;

    case SORT_ID_NAME: {
        if (is_dir_a != is_dir_b) {
            return is_dir_b - is_dir_a;
        }

        if ((name_a) && (name_b)) {
            return strverscmp(name_a, name_b);
        }

        if (name_a == name_b) {
            return 0; /* both are NULL */
        }
        else {
            return (name_a == NULL) ? -1 : 1;
        }
    }
    case SORT_ID_PATH: {
        if (is_dir_a != is_dir_b) {
            return is_dir_b - is_dir_a;
        }

        return list_model_compare_path(node_a->parent, node_b->parent);
    }
    case SORT_ID_TYPE: {
        if (is_dir_a != is_dir_b) {
            return is_dir_b - is_dir_a;
        }
        if (is_dir_a && is_dir_b) {
            return 0;
        }

        btree_node_get_path_full(node_a, path_a, sizeof(path_a));
        type_a = get_file_type(a, path_a);
        btree_node_get_path_full(node_b, path_b, sizeof(path_b));
        type_b = get_file_type(b, path_b);

        if (type_a && type_b) {
            return_val = strverscmp(type_a, type_b);
        }
        if (type_a) {
            g_free(type_a);
            type_a = NULL;
        }
        if (type_b) {
            g_free(type_b);
            type_b = NULL;
        }
        return return_val;
    }
    case SORT_ID_SIZE: {
        if (is_dir_a != is_dir_b) {
            return is_dir_b - is_dir_a;
        }
        if (is_dir_a && is_dir_b) {
            uint32_t n_a = btree_node_n_children(node_a);
            uint32_t n_b = btree_node_n_children(node_b);
            return n_a - n_b;
        }

        if (node_a->size == node_b->size)
            return 0;

        return (node_a->size > node_b->size) ? 1 : -1;
    }
    case SORT_ID_CHANGED: {
        if (node_a->mtime == node_b->mtime)
            return 0;

        return (node_a->mtime > node_b->mtime) ? 1 : -1;
    }
    default:
        return 0;
    }

    g_return_val_if_reached(0);
}

static gint
list_model_qsort_compare_func(DatabaseSearchEntry **a, DatabaseSearchEntry **b, ListModel *list_model) {
    g_assert((a) && (b) && (list_model));

    gint ret = list_model_compare_records(list_model->sort_id, *a, *b);

    /* Swap -1 and 1 if sort order is reverse */
    if (ret != 0 && list_model->sort_order == GTK_SORT_DESCENDING)
        ret = (ret < 0) ? 1 : -1;

    return ret;
}

void
list_model_sort_init(ListModel *list_model, char *sort_by, bool sort_ascending) {
    g_return_if_fail(list_model);
    g_return_if_fail(IS_LIST_MODEL(list_model));

    if (sort_by) {
        if (!strcmp(sort_by, "Name")) {
            list_model->sort_id = SORT_ID_NAME;
        }
        else if (!strcmp(sort_by, "Path")) {
            list_model->sort_id = SORT_ID_PATH;
        }
        else if (!strcmp(sort_by, "Type")) {
            list_model->sort_id = SORT_ID_TYPE;
        }
        else if (!strcmp(sort_by, "Size")) {
            list_model->sort_id = SORT_ID_SIZE;
        }
        else if (!strcmp(sort_by, "Date Modified")) {
            list_model->sort_id = SORT_ID_CHANGED;
        }

        if (sort_ascending) {
            list_model->sort_order = GTK_SORT_ASCENDING;
        }
        else {
            list_model->sort_order = GTK_SORT_DESCENDING;
        }
    }
}

static void
list_model_apply_sort(ListModel *list_model) {
    /* let other objects know about the new order */
    gint *neworder = g_new0(gint, list_model->results->len);

    for (uint32_t i = 0; i < list_model->results->len; ++i) {
        /* Note that the API reference might be wrong about
         * this, see bug number 124790 on bugs.gnome.org.
         * Both will work, but one will give you 'jumpy'
         * selections after row reordering. */
        /* neworder[(list_model->rows[i])->pos] = i; */
        DatabaseSearchEntry *entry = g_ptr_array_index(list_model->results, i);
        neworder[i] = db_search_entry_get_pos(entry);
        db_search_entry_set_pos(entry, i);
    }

    GtkTreePath *path = gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(list_model), path, NULL, neworder);

    gtk_tree_path_free(path);
    path = NULL;

    g_free(neworder);
    neworder = NULL;
}

void
list_model_sort(ListModel *list_model) {
    g_return_if_fail(list_model);
    g_return_if_fail(IS_LIST_MODEL(list_model));
    g_return_if_fail(list_model->results);

    if (list_model->sort_id == SORT_ID_NONE) {
        return;
    }

    if (list_model->results->len <= 1) {
        return;
    }

    trace("[list_model] sort started\n");
    GTimer *timer = fsearch_timer_start();
    /* resort */
    g_ptr_array_sort_with_data(list_model->results, (GCompareDataFunc)list_model_qsort_compare_func, list_model);

    list_model_apply_sort(list_model);

    fsearch_timer_stop(timer, "[list_model] sort finished in %.2f ms\n");
    timer = NULL;
}

void
list_model_update_sort(ListModel *list_model) {
    if (list_model->sort_id == SORT_ID_NAME && list_model->sort_order == GTK_SORT_ASCENDING) {
        return;
    }
    list_model_sort(list_model);
}

void
list_model_set_results(ListModel *list, GPtrArray *results) {
    list->node_cached = NULL;
    list->results = results;
}


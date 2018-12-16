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

#pragma once

#include <gtk/gtk.h>
#include "database.h"
#include "database_search.h"

/* Some boilerplate GObject defines. 'klass' is used
 *   instead of 'class', because 'class' is a C++ keyword */

#define LIST_MODEL_TYPE            (list_model_get_type ())
#define LIST_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIST_MODEL_TYPE, ListModel))
#define LIST_MODEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  LIST_MODEL_TYPE, ListModelClass))
#define IS_LIST_MODEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIST_MODEL_TYPE))
#define IS_LIST_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  LIST_MODEL_TYPE))
#define LIST_MODEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  LIST_MODEL_TYPE, ListModelClass))

/* The data columns that we export via the tree model interface */

enum
{
    LIST_MODEL_COL_RECORD = 0,
    LIST_MODEL_COL_ICON,
    LIST_MODEL_COL_NAME,
    LIST_MODEL_COL_PATH,
    LIST_MODEL_COL_TYPE,
    LIST_MODEL_COL_SIZE,
    LIST_MODEL_COL_CHANGED,
    LIST_MODEL_COL_TAGS,
    LIST_MODEL_N_COLUMNS,
};

enum
{
    SORT_ID_NONE = 0,
    SORT_ID_NAME,
    SORT_ID_PATH,
    SORT_ID_TYPE,
    SORT_ID_SIZE,
    SORT_ID_CHANGED,
    SORT_ID_TAGS,
};


typedef struct _ListModelRecord ListModelRecord;
typedef struct _ListModel ListModel;
typedef struct _ListModelClass ListModelClass;

/* ListModel: this structure contains everything we need for our
 *             model implementation. You can add extra fields to
 *             this structure, e.g. hashtables to quickly lookup
 *             rows or whatever else you might need, but it is
 *             crucial that 'parent' is the first member of the
 *             structure.                                          */

struct _ListModel
{
    GObject parent;      /* this MUST be the first member */

    GPtrArray *results;

    /* These two fields are not absolutely necessary, but they    */
    /*   speed things up a bit in our get_value implementation    */
    gint n_columns;
    GType column_types[LIST_MODEL_N_COLUMNS];

    gint sort_id;
    GtkSortType sort_order;

    gint stamp;       /* Random integer to check whether an iter belongs to our model */
};



/* ListModelClass: more boilerplate GObject stuff */

struct _ListModelClass
{
    GObjectClass parent_class;
};


GType
list_model_get_type (void);

ListModel *
list_model_new (void);

void
list_model_sort (ListModel *list_model);

void
list_set_results (ListModel *list, GPtrArray *results);

void
list_model_remove_entry (ListModel *list, DatabaseSearch *search, DatabaseSearchEntry *entry);

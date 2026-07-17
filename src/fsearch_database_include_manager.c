/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#include "fsearch_database_include_manager.h"

#include "fsearch_database_include.h"

#include <glib.h>

struct _FsearchDatabaseIncludeManager {
    GObject parent_instance;

    GPtrArray *includes;
};

G_DEFINE_TYPE(FsearchDatabaseIncludeManager, fsearch_database_include_manager, G_TYPE_OBJECT)

static void
fsearch_database_include_manager_finalize(GObject *object) {
    FsearchDatabaseIncludeManager *self = (FsearchDatabaseIncludeManager *)object;
    g_clear_pointer(&self->includes, g_ptr_array_unref);

    G_OBJECT_CLASS(fsearch_database_include_manager_parent_class)->finalize(object);
}

static void
fsearch_database_include_manager_class_init(FsearchDatabaseIncludeManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = fsearch_database_include_manager_finalize;
}

static void
fsearch_database_include_manager_init(FsearchDatabaseIncludeManager *self) {
    self->includes = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_include_unref);
}

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_new(void) {
    return g_object_new(FSEARCH_TYPE_DATABASE_INCLUDE_MANAGER, NULL);
}

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_new_with_defaults(void) {
    // NOTE: Do we want to have some directories included by default?
    return fsearch_database_include_manager_new();
}

void
fsearch_database_include_manager_add(FsearchDatabaseIncludeManager *self, FsearchDatabaseInclude *include) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_INCLUDE_MANAGER(self));
    if (!g_ptr_array_find_with_equal_func(self->includes, include, (GEqualFunc)fsearch_database_include_equal_path, NULL)) {
        g_ptr_array_add(self->includes, fsearch_database_include_ref(include));
        g_ptr_array_sort(self->includes, fsearch_database_include_compare);
    }
}

void
fsearch_database_include_manager_remove(FsearchDatabaseIncludeManager *self, FsearchDatabaseInclude *include) {
    g_return_if_fail(include);
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_INCLUDE_MANAGER(self));

    g_ptr_array_remove(self->includes, include);
}

gboolean
fsearch_database_include_manager_equal(FsearchDatabaseIncludeManager *m1, FsearchDatabaseIncludeManager *m2) {
    g_return_val_if_fail(m1, FALSE);
    g_return_val_if_fail(m2, FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_INCLUDE_MANAGER(m1), FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_INCLUDE_MANAGER(m2), FALSE);

    if (m1->includes->len != m2->includes->len) {
        return FALSE;
    }

    for (guint i = 0; i < m1->includes->len; ++i) {
        FsearchDatabaseInclude *i1 = g_ptr_array_index(m1->includes, i);
        FsearchDatabaseInclude *i2 = g_ptr_array_index(m2->includes, i);
        if (!fsearch_database_include_equal(i1, i2)) {
            return FALSE;
        }
    }
    return TRUE;
}

GPtrArray *
fsearch_database_include_manager_get_includes(FsearchDatabaseIncludeManager *self) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_INCLUDE_MANAGER(self), NULL);
    return g_ptr_array_ref(self->includes);
}

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_copy(FsearchDatabaseIncludeManager *self) {
    g_return_val_if_fail(self, NULL);
    FsearchDatabaseIncludeManager *copy = fsearch_database_include_manager_new();
    g_clear_pointer(&copy->includes, g_ptr_array_unref);
    copy->includes = g_ptr_array_copy(self->includes, (GCopyFunc)fsearch_database_include_copy, NULL);

    return copy;
}
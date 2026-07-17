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

#include "fsearch_database_info.h"

struct _FsearchDatabaseInfo {
    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;
    uint32_t num_files;
    uint32_t num_folders;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseInfo, fsearch_database_info, fsearch_database_info_ref, fsearch_database_info_unref)

FsearchDatabaseInfo *
fsearch_database_info_ref(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_info_unref(FsearchDatabaseInfo *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(g_atomic_int_get(&self->ref_count) > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_object(&self->include_manager);
        g_clear_object(&self->exclude_manager);
        g_clear_pointer(&self, g_free);
    }
}

FsearchDatabaseInfo *
fsearch_database_info_new(FsearchDatabaseIncludeManager *include_manager,
                          FsearchDatabaseExcludeManager *exclude_manager,
                          uint32_t num_files,
                          uint32_t num_folders) {
    FsearchDatabaseInfo *self = calloc(1, sizeof(FsearchDatabaseInfo));
    g_assert(self);

    if (include_manager) {
        self->include_manager = fsearch_database_include_manager_copy(include_manager);
    }
    if (exclude_manager) {
        self->exclude_manager = fsearch_database_exclude_manager_copy(exclude_manager);
    }
    self->num_files = num_files;
    self->num_folders = num_folders;

    self->ref_count = 1;

    return self;
}

uint32_t
fsearch_database_info_get_num_files(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self, 0);
    return self->num_files;
}

uint32_t
fsearch_database_info_get_num_folders(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self, 0);
    return self->num_folders;
}

uint32_t
fsearch_database_info_get_num_entries(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self, 0);
    return self->num_files + self->num_folders;
}

FsearchDatabaseIncludeManager *
fsearch_database_info_get_include_manager(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self, NULL);
    return self->include_manager ? g_object_ref(self->include_manager) : NULL;
}

FsearchDatabaseExcludeManager *
fsearch_database_info_get_exclude_manager(FsearchDatabaseInfo *self) {
    g_return_val_if_fail(self, NULL);
    return self->exclude_manager ? g_object_ref(self->exclude_manager) : NULL;
}
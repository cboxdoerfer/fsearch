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

#include "fsearch_database_include.h"

#include <glib.h>
#include <stdint.h>

#include "fsearch_database_scan_reason.h"
#include "fsearch_file_utils.h"

struct _FsearchDatabaseInclude {
    char *path;
    gboolean active;
    gboolean monitor;
    gboolean one_file_system;
    gboolean scan_after_launch;

    int64_t rescan_after;
    int64_t last_scan_time;
    uint32_t last_scan_duration;
    uint32_t last_error_code;
    uint32_t last_scanned_file_count;
    uint32_t last_scanned_folder_count;
    FsearchDatabaseScanReason last_scan_reason;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseInclude,
                    fsearch_database_include,
                    fsearch_database_include_ref,
                    fsearch_database_include_unref)

FsearchDatabaseInclude *
fsearch_database_include_new(const char *path,
                             gboolean active,
                             gboolean one_file_system,
                             gboolean monitor,
                             gboolean scan_after_load,
                             int64_t rescan_after) {
    FsearchDatabaseInclude *self;

    g_return_val_if_fail(path, NULL);

    self = g_new0(FsearchDatabaseInclude, 1);

    self->path = g_strdup(path);
    self->active = active;
    self->one_file_system = one_file_system;
    self->monitor = monitor;
    self->scan_after_launch = scan_after_load;
    self->rescan_after = rescan_after;
    self->ref_count = 1;

    return self;
}

FsearchDatabaseInclude *
fsearch_database_include_ref(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_include_unref(FsearchDatabaseInclude *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(g_atomic_int_get(&self->ref_count) > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self->path, g_free);
        g_free(self);
    }
}

gboolean
fsearch_database_include_equal_path(FsearchDatabaseInclude *i1, FsearchDatabaseInclude *i2) {
    g_return_val_if_fail(i1 != NULL, FALSE);
    g_return_val_if_fail(i2 != NULL, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&i1->ref_count) > 0, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&i2->ref_count) > 0, FALSE);

    if (g_strcmp0(i1->path, i2->path) != 0) {
        return FALSE;
    }
    return TRUE;
}

gboolean
fsearch_database_include_equal(FsearchDatabaseInclude *i1, FsearchDatabaseInclude *i2) {
    g_return_val_if_fail(i1 != NULL, FALSE);
    g_return_val_if_fail(i2 != NULL, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&i1->ref_count) > 0, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&i2->ref_count) > 0, FALSE);

    if (i1->active != i2->active || i1->monitor != i2->monitor || i1->one_file_system != i2->one_file_system
        || i1->rescan_after != i2->rescan_after || i1->scan_after_launch != i2->scan_after_launch
        || g_strcmp0(i1->path, i2->path) != 0) {
        return FALSE;
    }
    return TRUE;
}

gint
fsearch_database_include_compare(gconstpointer i1, gconstpointer i2) {
    FsearchDatabaseInclude *include1 = *(FsearchDatabaseInclude **)i1;
    FsearchDatabaseInclude *include2 = *(FsearchDatabaseInclude **)i2;

    return fsearch_file_utils_cmp_paths(include1->path, include2->path);
}

FsearchDatabaseInclude *
fsearch_database_include_copy(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_include_new(self->path,
                                        self->active,
                                        self->one_file_system,
                                        self->monitor,
                                        self->scan_after_launch,
                                        self->rescan_after);
}

void
fsearch_database_include_set_last_scan_time(FsearchDatabaseInclude *self, int64_t time) {
    g_return_if_fail(self);
    self->last_scan_time = time;
}

void
fsearch_database_include_set_last_scan_duration(FsearchDatabaseInclude *self, uint32_t duration) {
    g_return_if_fail(self);
    self->last_scan_duration = duration;
}

void
fsearch_database_include_set_last_error_code(FsearchDatabaseInclude *self, uint32_t error_code) {
    g_return_if_fail(self);
    self->last_error_code = error_code;
}

void
fsearch_database_include_set_last_scanned_file_count(FsearchDatabaseInclude *self, uint32_t count) {
    g_return_if_fail(self);
    self->last_scanned_file_count = count;
}

void
fsearch_database_include_set_last_scanned_folder_count(FsearchDatabaseInclude *self, uint32_t count) {
    g_return_if_fail(self);
    self->last_scanned_folder_count = count;
}

void
fsearch_database_include_set_last_scan_reason(FsearchDatabaseInclude *self, FsearchDatabaseScanReason reason) {
    g_return_if_fail(self);

    g_return_if_fail(reason >= FSEARCH_DATABASE_SCAN_REASON_UNKNOWN);
    g_return_if_fail(reason < NUM_FSEARCH_DATABASE_SCAN_REASONS);

    self->last_scan_reason = reason;
}

gboolean
fsearch_database_include_get_active(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, 0);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, 0);

    return self->active;
}

const char *
fsearch_database_include_get_path(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, NULL);

    return self->path;
}

gboolean
fsearch_database_include_get_one_file_system(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, FALSE);

    return self->one_file_system;
}

gboolean
fsearch_database_include_get_monitored(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, FALSE);

    return self->monitor;
}

gboolean
fsearch_database_include_get_scan_after_launch(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(g_atomic_int_get(&self->ref_count) > 0, FALSE);

    return self->scan_after_launch;
}

int64_t
fsearch_database_include_get_rescan_after(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->rescan_after;
}

int64_t
fsearch_database_include_get_last_scan_time(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->last_scan_time;
}

uint32_t
fsearch_database_include_get_last_scan_duration(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->last_scan_duration;
}

uint32_t
fsearch_database_include_get_last_error_code(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->last_error_code;
}

uint32_t
fsearch_database_include_get_last_scanned_file_count(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->last_scanned_file_count;
}

uint32_t
fsearch_database_include_get_last_scanned_folder_count(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, 0);
    return self->last_scanned_folder_count;
}

FsearchDatabaseScanReason
fsearch_database_include_get_last_scan_reason(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, FSEARCH_DATABASE_SCAN_REASON_UNKNOWN);
    return self->last_scan_reason;
}
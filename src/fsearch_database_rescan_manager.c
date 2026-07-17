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

#define G_LOG_DOMAIN "fsearch-database-rescan-manager"

#include "fsearch_database_rescan_manager.h"

#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"

#include <glib.h>

struct _FsearchDatabaseRescanManager {
    FsearchDatabaseIncludeManager *include_manager;

    FsearchDatabaseRescanIndexFunc index_cb;
    FsearchDatabaseRescanFullFunc full_cb;
    gpointer cb_data;

    GMainContext *context;

    GSource *current_timer_source;
    GSource *offline_poll_source;

    GHashTable *active_scans;
    GHashTable *offline_indices;

    gboolean global_scan_active;
};

FsearchDatabaseRescanManager *
fsearch_database_rescan_manager_new(FsearchDatabaseIncludeManager *include_manager,
                                    FsearchDatabaseRescanIndexFunc index_cb,
                                    FsearchDatabaseRescanFullFunc full_cb,
                                    gpointer cb_data,
                                    GMainContext *context) {
    FsearchDatabaseRescanManager *self = g_new0(FsearchDatabaseRescanManager, 1);
    self->include_manager = include_manager ? g_object_ref(include_manager) : NULL;
    self->index_cb = index_cb;
    self->full_cb = full_cb;
    self->cb_data = cb_data;
    self->context = context ? g_main_context_ref(context) : NULL;

    self->active_scans = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    self->offline_indices = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    self->global_scan_active = FALSE;

    fsearch_database_rescan_manager_reschedule(self);
    return self;
}

void
fsearch_database_rescan_manager_free(FsearchDatabaseRescanManager *self) {
    g_return_if_fail(self != NULL);

    if (self->current_timer_source) {
        g_source_destroy(self->current_timer_source);
        g_clear_pointer(&self->current_timer_source, g_source_unref);
    }

    if (self->offline_poll_source) {
        g_source_destroy(self->offline_poll_source);
        g_clear_pointer(&self->offline_poll_source, g_source_unref);
    }

    g_clear_pointer(&self->active_scans, g_hash_table_unref);
    g_clear_pointer(&self->offline_indices, g_hash_table_unref);
    g_clear_object(&self->include_manager);
    g_clear_pointer(&self->context, g_main_context_unref);
    g_free(self);
}

static guint
get_num_active_includes(FsearchDatabaseRescanManager *self) {
    if (!self->include_manager) {
        return 0;
    }

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(self->include_manager);
    guint active_count = 0;

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *inc = g_ptr_array_index(includes, i);
        if (fsearch_database_include_get_active(inc)) {
            active_count++;
        }
    }

    return active_count;
}

static void
request_scans(FsearchDatabaseRescanManager *self, GPtrArray *due_includes) {
    if (due_includes && due_includes->len > 0) {
        const guint num_active_includes = get_num_active_includes(self);
        if (due_includes->len == num_active_includes) {
            g_debug("[rescan_manager] all active indices need to be scanned. Perform a full scan.");
            fsearch_database_rescan_manager_request_full_scan(self);
        }
        else {
            for (uint32_t i = 0; i < due_includes->len; i++) {
                FsearchDatabaseInclude *include = g_ptr_array_index(due_includes, i);
                const char *path = fsearch_database_include_get_path(include);

                g_debug("[rescan-manager] Queuing scan for index %s", path);
                fsearch_database_rescan_manager_request_index_scan(self, path);
            }
        }
    }
}

static guint
calculate_next_timeout_ms(FsearchDatabaseRescanManager *self, GPtrArray **due_indices_out) {
    if (self->global_scan_active || !self->include_manager) {
        return 0;
    }

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(self->include_manager);
    const int64_t current_time_sec = g_get_real_time() / G_USEC_PER_SEC;
    int64_t min_wait_sec = G_MAXINT64;
    gboolean needs_timer = FALSE;

    if (due_indices_out) {
        *due_indices_out = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_include_unref);
    }

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        const char *path = fsearch_database_include_get_path(include);

        if (!fsearch_database_include_get_active(include) || g_hash_table_contains(self->active_scans, path)
            || g_hash_table_contains(self->offline_indices, path)) {
            continue;
        }

        const int64_t rescan_after = fsearch_database_include_get_rescan_after(include);
        if (rescan_after <= 0) {
            continue;
        }

        const int64_t last_scan_time = fsearch_database_include_get_last_scan_time(include);
        const int64_t next_scan_time = last_scan_time + rescan_after;
        const int64_t diff_sec = next_scan_time - current_time_sec;

        if (diff_sec <= 0) {
            if (due_indices_out) {
                g_ptr_array_add(*due_indices_out, fsearch_database_include_ref(include));
            }
            min_wait_sec = 0;
            needs_timer = TRUE;
        }
        else if (diff_sec < min_wait_sec) {
            min_wait_sec = diff_sec;
            needs_timer = TRUE;
        }
    }

    if (!needs_timer) {
        return 0;
    }
    if (min_wait_sec > (G_MAXUINT / 1000)) {
        return G_MAXUINT - 1;
    }
    return (guint)(min_wait_sec * 1000);
}

static gboolean
rescan_timer_fired_cb(gpointer user_data) {
    FsearchDatabaseRescanManager *self = user_data;
    g_autoptr(GPtrArray) due_indices = NULL;

    calculate_next_timeout_ms(self, &due_indices);

    request_scans(self, due_indices);

    g_clear_pointer(&self->current_timer_source, g_source_unref);
    fsearch_database_rescan_manager_reschedule(self);
    return G_SOURCE_REMOVE;
}

static gboolean
offline_poll_fired_cb(gpointer user_data) {
    FsearchDatabaseRescanManager *self = user_data;

    if (self->global_scan_active || !self->include_manager) {
        return G_SOURCE_CONTINUE;
    }

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(self->include_manager);
    gboolean still_has_offline = FALSE;
    g_autoptr(GPtrArray) reappeared = g_ptr_array_new();

    GHashTableIter iter = {};
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, self->offline_indices);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const char *path = key;

        if (path && g_file_test(path, G_FILE_TEST_IS_DIR)) {
            g_ptr_array_add(reappeared, (gpointer)path);
        }
        else {
            still_has_offline = TRUE;
        }
    }

    for (uint32_t i = 0; i < reappeared->len; i++) {
        const char *path = g_ptr_array_index(reappeared, i);
        g_debug("[rescan-manager] Offline root reappeared for index %s", path);

        fsearch_database_rescan_manager_request_index_scan(self, path);

        // Don't use path after removing it from the hash table, since it will be a freed along with it
        g_hash_table_remove(self->offline_indices, g_steal_pointer(&path));
    }

    if (!still_has_offline) {
        g_clear_pointer(&self->offline_poll_source, g_source_unref);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void
fsearch_database_rescan_manager_reschedule(FsearchDatabaseRescanManager *self) {
    g_return_if_fail(self != NULL);

    if (self->current_timer_source) {
        g_source_destroy(self->current_timer_source);
        g_clear_pointer(&self->current_timer_source, g_source_unref);
    }

    const guint next_timeout_ms = calculate_next_timeout_ms(self, NULL);
    if (next_timeout_ms > 0) {
        self->current_timer_source = g_timeout_source_new(next_timeout_ms);
        g_debug("[rescan-manager] set timeout for next scan: %ds", next_timeout_ms / 1000);
        g_source_set_callback(self->current_timer_source, rescan_timer_fired_cb, self, NULL);
        g_source_attach(self->current_timer_source, self->context);
    }
}

void
fsearch_database_rescan_manager_request_index_scan(FsearchDatabaseRescanManager *self, const char *path) {
    g_return_if_fail(self != NULL);

    if (self->global_scan_active || g_hash_table_contains(self->active_scans, path)) {
        return;
    }

    g_hash_table_add(self->active_scans, (gpointer)g_strdup(path));
    if (self->index_cb) {
        self->index_cb(path, self->cb_data);
    }
}

void
fsearch_database_rescan_manager_request_full_scan(FsearchDatabaseRescanManager *self) {
    g_return_if_fail(self != NULL);

    if (self->global_scan_active) {
        return;
    }

    self->global_scan_active = TRUE;

    if (self->current_timer_source) {
        g_source_destroy(self->current_timer_source);
        g_clear_pointer(&self->current_timer_source, g_source_unref);
    }

    if (self->full_cb) {
        self->full_cb(self->cb_data);
    }
}

void
fsearch_database_rescan_manager_trigger_startup_scans(FsearchDatabaseRescanManager *self) {
    g_return_if_fail(self != NULL);

    if (!self->include_manager || self->global_scan_active) {
        return;
    }

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(self->include_manager);
    const int64_t current_time_sec = g_get_real_time() / G_USEC_PER_SEC;

    g_debug("[rescan-manager] Evaluating startup scans...");

    const guint num_active_includes = get_num_active_includes(self);

    g_autoptr(GPtrArray) startup_indices = g_ptr_array_new_full(num_active_includes, NULL);

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);

        // Inactive indexes don't need to be rescanned -> skip
        if (!fsearch_database_include_get_active(include)) {
            continue;
        }

        gboolean needs_startup_scan = FALSE;

        // 1: Folder is being actively monitored for FS events
        if (fsearch_database_include_get_monitored(include)) {
            needs_startup_scan = TRUE;
        }
        // 2: Explicitly marked to scan after launch
        else if (fsearch_database_include_get_scan_after_launch(include)) {
            needs_startup_scan = TRUE;
        }
        // 3: Schedule is already due
        else {
            const int64_t rescan_after = fsearch_database_include_get_rescan_after(include);
            if (rescan_after > 0) {
                const int64_t last_scan_time = fsearch_database_include_get_last_scan_time(include);
                if (last_scan_time + rescan_after <= current_time_sec) {
                    needs_startup_scan = TRUE;
                }
            }
        }

        if (needs_startup_scan) {
            g_ptr_array_add(startup_indices, include);
        }
    }

    request_scans(self, startup_indices);
}

void
fsearch_database_rescan_manager_notify_index_finished(FsearchDatabaseRescanManager *self, const char *path) {
    g_return_if_fail(self != NULL);
    if (g_hash_table_remove(self->active_scans, path)) {
        fsearch_database_rescan_manager_reschedule(self);
    }
}

void
fsearch_database_rescan_manager_notify_index_offline(FsearchDatabaseRescanManager *self, const char *path) {
    g_return_if_fail(self != NULL);

    g_hash_table_remove(self->active_scans, path);

    if (!g_hash_table_contains(self->offline_indices, path)) {
        g_debug("[rescan-manager] Index marked offline. Starting reappear poll: %s", path);
        g_hash_table_add(self->offline_indices, (gpointer)g_strdup(path));

        if (!self->offline_poll_source) {
            self->offline_poll_source = g_timeout_source_new_seconds(5);
            g_source_set_callback(self->offline_poll_source, offline_poll_fired_cb, self, NULL);
            g_source_attach(self->offline_poll_source, self->context);
        }
    }
}

void
fsearch_database_rescan_manager_notify_new_config(FsearchDatabaseRescanManager *self,
                                                  FsearchDatabaseIncludeManager *include_manager) {
    g_return_if_fail(self != NULL);

    if (include_manager != self->include_manager) {
        // Swap the reference when there's a new include manager
        g_set_object(&self->include_manager, include_manager);
    }

    // Always reset the rescan manager state, even if the include manager is the same
    // It becomes invalid after a scan finished or the config changed
    g_hash_table_remove_all(self->offline_indices);
    if (self->offline_poll_source) {
        g_source_destroy(self->offline_poll_source);
        self->offline_poll_source = NULL;
    }

    self->global_scan_active = FALSE;
    g_hash_table_remove_all(self->active_scans);

    g_debug("[rescan-manager] Include manager updated. Rescheduling.");
    fsearch_database_rescan_manager_reschedule(self);
}
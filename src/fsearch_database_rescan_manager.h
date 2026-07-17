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

#pragma once

#include "fsearch_database_include_manager.h"

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct _FsearchDatabaseRescanManager FsearchDatabaseRescanManager;

typedef void (*FsearchDatabaseRescanIndexFunc)(const char *path, gpointer user_data);
typedef void (*FsearchDatabaseRescanFullFunc)(gpointer user_data);

FsearchDatabaseRescanManager *
fsearch_database_rescan_manager_new(FsearchDatabaseIncludeManager *include_manager,
                                    FsearchDatabaseRescanIndexFunc index_cb,
                                    FsearchDatabaseRescanFullFunc full_cb,
                                    gpointer user_data,
                                    GMainContext *context);

void
fsearch_database_rescan_manager_free(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_reschedule(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_request_index_scan(FsearchDatabaseRescanManager *self, const char *path);
void
fsearch_database_rescan_manager_request_full_scan(FsearchDatabaseRescanManager *self);
void
fsearch_database_rescan_manager_trigger_startup_scans(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_notify_index_finished(FsearchDatabaseRescanManager *self, const char *path);
void
fsearch_database_rescan_manager_notify_new_config(FsearchDatabaseRescanManager *self,
                                                  FsearchDatabaseIncludeManager *include_manager);

void
fsearch_database_rescan_manager_notify_index_offline(FsearchDatabaseRescanManager *self, const char *path);

G_END_DECLS
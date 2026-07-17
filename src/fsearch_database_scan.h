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

#include "fsearch_database_exclude_manager.h"
#include "fsearch_folder_monitor_fanotify.h"
#include "fsearch_folder_monitor_inotify.h"

bool
db_scan_folder(const char *path,
               FsearchDatabaseEntry *parent,
               DynamicArray *folders,
               DynamicArray *files,
               FsearchDatabaseExcludeManager *exclude_manager,
               FsearchFolderMonitorFanotify *fanotify_monitor,
               FsearchFolderMonitorInotify *inotify_monitor,
               bool one_file_system,
               GCancellable *cancellable,
               void (*status_cb)(const char *, gpointer),
               gpointer status_cb_data);
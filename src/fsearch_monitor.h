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

#pragma once

#include "fsearch_database.h"

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchMonitor FsearchMonitor;

// Callback for when database changes are applied
typedef void (*FsearchMonitorCallback)(gpointer user_data);

// Create a new file system monitor
// db: Database to update (takes a reference)
// index_paths: List of FsearchIndex paths to monitor
FsearchMonitor *
fsearch_monitor_new(FsearchDatabase *db, GList *index_paths);

// Free the monitor and release resources
void
fsearch_monitor_free(FsearchMonitor *monitor);

// Start monitoring for file system changes
// Returns true on success, false if inotify initialization failed
bool
fsearch_monitor_start(FsearchMonitor *monitor);

// Stop monitoring
void
fsearch_monitor_stop(FsearchMonitor *monitor);

// Check if monitor is running
bool
fsearch_monitor_is_running(FsearchMonitor *monitor);

// Set the coalesce interval (time to batch events before applying)
// Default is 1500ms
void
fsearch_monitor_set_coalesce_interval_ms(FsearchMonitor *monitor, uint32_t ms);

// Set excluded paths (directories to skip)
void
fsearch_monitor_set_excluded_paths(FsearchMonitor *monitor, GList *excludes);

// Set exclude patterns (fnmatch patterns for files to ignore)
void
fsearch_monitor_set_exclude_patterns(FsearchMonitor *monitor, char **patterns);

// Set whether to exclude hidden files
void
fsearch_monitor_set_exclude_hidden(FsearchMonitor *monitor, bool exclude);

// Set callback for when changes are applied
void
fsearch_monitor_set_callback(FsearchMonitor *monitor,
                             FsearchMonitorCallback callback,
                             gpointer user_data);

// Get the number of active watches
uint32_t
fsearch_monitor_get_num_watches(FsearchMonitor *monitor);

// Check if watch limit was reached
bool
fsearch_monitor_watch_limit_reached(FsearchMonitor *monitor);

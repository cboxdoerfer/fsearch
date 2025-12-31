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

// Error types that can occur during monitoring
typedef enum {
    FSEARCH_MONITOR_ERROR_QUEUE_OVERFLOW,  // inotify queue overflow - events lost
    FSEARCH_MONITOR_ERROR_THREAD_CRASHED,  // watch thread exited unexpectedly
} FsearchMonitorError;

// Callback for monitor errors
// error: type of error that occurred
// user_data: user-provided callback data
typedef void (*FsearchMonitorErrorCallback)(FsearchMonitorError error, gpointer user_data);

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

// Set callback that fires BEFORE changes are applied
// Use this to invalidate caches that hold entry pointers
void
fsearch_monitor_set_prepare_callback(FsearchMonitor *monitor,
                                     FsearchMonitorCallback callback,
                                     gpointer user_data);

// Get the number of active watches
uint32_t
fsearch_monitor_get_num_watches(FsearchMonitor *monitor);

// Check if watch limit was reached
bool
fsearch_monitor_watch_limit_reached(FsearchMonitor *monitor);

// Enable/disable batching mode
// When batching, events are queued but not processed until flush is called
// Use this during database scans to accumulate changes
void
fsearch_monitor_set_batching(FsearchMonitor *monitor, bool batching);

// Check if batching mode is active
bool
fsearch_monitor_is_batching(FsearchMonitor *monitor);

// Immediately process all queued events
// Call this after scan completes and database is swapped
void
fsearch_monitor_flush_events(FsearchMonitor *monitor);

// Update the database reference
// Call this after scan completes to point to the new database
void
fsearch_monitor_set_database(FsearchMonitor *monitor, FsearchDatabase *db);

// Set callback for error conditions (overflow, thread crash)
// The callback may be invoked from a background thread context
void
fsearch_monitor_set_error_callback(FsearchMonitor *monitor,
                                   FsearchMonitorErrorCallback callback,
                                   gpointer user_data);

// Check if an overflow occurred (events may have been lost)
bool
fsearch_monitor_overflow_occurred(FsearchMonitor *monitor);

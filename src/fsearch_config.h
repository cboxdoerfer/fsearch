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
   */

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_filter_manager.h"

typedef struct _FsearchConfig FsearchConfig;

typedef enum FsearchConfigActionAfterOpen {
    ACTION_AFTER_OPEN_NOTHING = 0,
    ACTION_AFTER_OPEN_MINIMIZE,
    ACTION_AFTER_OPEN_CLOSE,
    N_ACTIONS_AFTER_OPEN,
} FsearchConfigActionAfterOpen;

typedef struct {
    bool database_config_changed;
    bool listview_config_changed;
    bool search_config_changed;
} FsearchConfigCompareResult;

struct _FsearchConfig {
    // Search
    bool hide_results_on_empty_search;
    bool search_in_path;
    bool enable_regex;
    bool match_case;
    bool auto_search_in_path;
    bool auto_match_case;
    bool search_as_you_type;

    // Applications
    char *folder_open_cmd;

    // Window
    bool restore_window_size;
    int32_t window_width;
    int32_t window_height;

    // Interface
    bool show_base_2_units;
    bool highlight_search_terms;
    bool single_click_open;
    bool launch_desktop_files;
    bool enable_dark_theme;
    bool enable_list_tooltips;
    bool restore_column_config;
    bool restore_sort_order;
    bool double_click_path;
    FsearchConfigActionAfterOpen action_after_file_open;
    bool action_after_file_open_keyboard;
    bool action_after_file_open_mouse;
    bool exit_on_escape;
    bool show_indexing_status;

    // Warning Dialogs
    bool show_dialog_failed_opening;

    // View menu
    bool show_menubar;
    bool show_statusbar;
    bool show_filter;
    bool show_search_button;

    // Columns
    bool show_listview_icons;
    bool show_path_column;
    bool show_type_column;
    bool show_extension_column;
    bool show_size_column;
    bool show_modified_column;

    char *sort_by;
    bool sort_ascending;

    uint32_t name_column_width;
    uint32_t path_column_width;
    uint32_t type_column_width;
    uint32_t extension_column_width;
    uint32_t size_column_width;
    uint32_t modified_column_width;

    uint32_t name_column_pos;
    uint32_t path_column_pos;
    uint32_t type_column_pos;
    uint32_t size_column_pos;
    uint32_t modified_column_pos;

    FsearchFilterManager *filters;

    FsearchDatabaseIncludeManager *includes;
    FsearchDatabaseExcludeManager *excludes;

    // NTFS fast scan
    bool ntfs_fast_scan_enabled;
    bool ntfs_auto_polkit;
    GPtrArray *ntfs_partitions; /* GPtrArray of FsearchNtfsPartitionConfig* */
};

/**
 * FsearchNtfsPartitionConfig:
 * @mountpoint: Mount point path (e.g., "/mnt/data")
 * @include: Whether to include this partition in the database
 * @monitor: Whether to monitor this partition for changes
 *
 * Holds persistent configuration for an NTFS partition.
 */
typedef struct {
    char *mountpoint;
    bool include;
    bool monitor;
} FsearchNtfsPartitionConfig;

/**
 * fsearch_ntfs_partition_config_new:
 * @mountpoint: Mount point path
 * @include: Include flag
 * @monitor: Monitor flag
 *
 * Creates a new NTFS partition config.
 *
 * Returns: (transfer full): A newly allocated #FsearchNtfsPartitionConfig
 */
FsearchNtfsPartitionConfig *
fsearch_ntfs_partition_config_new(const char *mountpoint, bool include, bool monitor);

/**
 * fsearch_ntfs_partition_config_free:
 * @config: A #FsearchNtfsPartitionConfig
 *
 * Frees an NTFS partition config.
 */
void
fsearch_ntfs_partition_config_free(FsearchNtfsPartitionConfig *config);

/**
 * fsearch_ntfs_partition_configs_free:
 * @array: A #GPtrArray of #FsearchNtfsPartitionConfig
 *
 * Frees a GPtrArray and all its NTFS partition configs.
 */
void
fsearch_ntfs_partition_configs_free(GPtrArray *array);

/**
 * fsearch_ntfs_get_partition_config:
 * @partitions: GPtrArray of #FsearchNtfsPartitionConfig (from FsearchConfig)
 * @mountpoint: Mount point path to look up
 *
 * Gets the NTFS partition config for a given mount point.
 *
 * Returns: (transfer none): A #FsearchNtfsPartitionConfig, or %NULL if not found.
 */
const FsearchNtfsPartitionConfig *
fsearch_ntfs_get_partition_config(GPtrArray *partitions, const char *mountpoint);

/**
 * fsearch_ntfs_partition_configs_equal:
 * @a1: First GPtrArray of #FsearchNtfsPartitionConfig
 * @a2: Second GPtrArray of #FsearchNtfsPartitionConfig
 *
 * Compares two NTFS partition config arrays for equality.
 *
 * Returns: %true if both arrays are equal
 */
bool
fsearch_ntfs_partition_configs_equal(GPtrArray *a1, GPtrArray *a2);

bool
config_make_dir(void);

bool
config_load(FsearchConfig *config);

bool
config_load_default(FsearchConfig *config);

bool
config_save(FsearchConfig *config);

void
config_build_dir(char *path, size_t len);

FsearchConfigCompareResult
config_cmp(FsearchConfig *c1, FsearchConfig *c2);

FsearchConfig *
config_copy(FsearchConfig *config);

void
config_free(FsearchConfig *config);
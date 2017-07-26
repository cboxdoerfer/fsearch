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

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

typedef struct _FsearchConfig FsearchConfig;

struct _FsearchConfig
{
    // Search
    bool limit_results;
    bool hide_results_on_empty_search;
    bool search_in_path;
    bool enable_regex;
    bool match_case;
    bool auto_search_in_path;
    bool search_as_you_type;

    // Window
    bool restore_window_size;
    int32_t window_width;
    int32_t window_height;

    // Interface
    bool enable_dark_theme;
    bool enable_list_tooltips;
    bool restore_column_config;

    // View menu
    bool show_menubar;
    bool show_statusbar;
    bool show_filter;
    bool show_search_button;

    // Columns
    bool show_listview_icons;
    bool show_path_column;
    bool show_type_column;
    bool show_size_column;
    bool show_modified_column;

    uint32_t name_column_width;
    uint32_t path_column_width;
    uint32_t type_column_width;
    uint32_t size_column_width;
    uint32_t modified_column_width;

    uint32_t name_column_pos;
    uint32_t path_column_pos;
    uint32_t type_column_pos;
    uint32_t size_column_pos;
    uint32_t modified_column_pos;

    // database
    bool update_database_on_launch;
    bool exclude_hidden_items;
    bool follow_symlinks;

    uint32_t num_results;

    GList *locations;
    GList *exclude_locations;
    char **exclude_files;
};


bool
config_make_dir (void);

bool
config_load (FsearchConfig *config);

bool
config_load_default (FsearchConfig *config);

bool
config_save (FsearchConfig *config);

void
config_build_dir (char *path, size_t len);

void
config_free (FsearchConfig *config);

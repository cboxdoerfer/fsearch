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
    bool search_in_path;
    bool enable_regex;
    bool match_case;

    // Interface
    bool enable_dark_theme;
    bool enable_list_tooltips;

    // View menu
    bool show_menubar;
    bool show_statusbar;
    bool show_filter;
    bool show_search_button;

    // database
    bool update_database_on_launch;

    uint32_t num_results;

    GList *locations;
};


bool
make_config_dir (void);

bool
load_config (FsearchConfig *config);

bool
load_default_config (FsearchConfig *config);

bool
save_config (FsearchConfig *config);

void
build_config_dir (char *path, size_t len);

void
config_free (FsearchConfig *config);

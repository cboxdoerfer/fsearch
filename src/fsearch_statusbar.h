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
#include <gtk/gtk.h>
#include <stdint.h>

G_BEGIN_DECLS

#define FSEARCH_STATUSBAR_TYPE (fsearch_statusbar_get_type())

G_DECLARE_FINAL_TYPE(FsearchStatusbar, fsearch_statusbar, FSEARCH, STATUSBAR, GtkRevealer)

FsearchStatusbar *
fsearch_statusbar_new(void);

G_END_DECLS

typedef enum {
    FSEARCH_STATUSBAR_REVEALER_MATCH_CASE,
    FSEARCH_STATUSBAR_REVEALER_SMART_MATCH_CASE,
    FSEARCH_STATUSBAR_REVEALER_SEARCH_IN_PATH,
    FSEARCH_STATUSBAR_REVEALER_SMART_SEARCH_IN_PATH,
    FSEARCH_STATUSBAR_REVEALER_REGEX,
    FSEARCH_STATUSBAR_REVEALER_PARTIAL_RESULTS,
    NUM_FSEARCH_STATUSBAR_REVEALERS,
} FsearchStatusbarRevealer;

typedef enum {
    FSEARCH_STATUSBAR_DATABASE_STATE_IDLE,
    FSEARCH_STATUSBAR_DATABASE_STATE_LOADING,
    FSEARCH_STATUSBAR_DATABASE_STATE_SCANNING,
    NUM_FSEARCH_STATUSBAR_DATABASE_STATES,
} FsearchStatusbarState;

void
fsearch_statusbar_set_query_text(FsearchStatusbar *sb, const char *text);

void
fsearch_statusbar_set_num_search_results(FsearchStatusbar *sb, uint32_t num_results);

void
fsearch_statusbar_set_query_status_delayed(FsearchStatusbar *sb);

void
fsearch_statusbar_set_sort_status_delayed(FsearchStatusbar *sb);

void
fsearch_statusbar_set_revealer_visibility(FsearchStatusbar *sb, FsearchStatusbarRevealer revealer, gboolean visible);

void
fsearch_statusbar_set_filter(FsearchStatusbar *sb, const char *filter_name);

void
fsearch_statusbar_set_database_index_text(FsearchStatusbar *sb, const char *text);

void
fsearch_statusbar_set_selection(FsearchStatusbar *sb,
                                uint32_t num_files_selected,
                                uint32_t num_folders_selected,
                                uint32_t num_files,
                                uint32_t num_folders);
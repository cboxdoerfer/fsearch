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

#include <glib.h>
#include <gtk/gtk.h>
#include <stdbool.h>

#include "fsearch.h"
#include "fsearch_database.h"
#include "fsearch_list_view.h"
#include "fsearch_query.h"
#include "fsearch_statusbar.h"

G_BEGIN_DECLS

#define FSEARCH_APPLICATION_WINDOW_TYPE (fsearch_application_window_get_type())

G_DECLARE_FINAL_TYPE(FsearchApplicationWindow, fsearch_application_window, FSEARCH, APPLICATION_WINDOW, GtkApplicationWindow)

FsearchApplicationWindow *
fsearch_application_window_new(FsearchApplication *app);

void
fsearch_application_window_prepare_shutdown(gpointer self);

FsearchListView *
fsearch_application_window_get_listview(FsearchApplicationWindow *self);

void
fsearch_application_window_update_listview_config(FsearchApplicationWindow *self);

void
fsearch_application_window_apply_statusbar_revealer_config(FsearchApplicationWindow *win);

void
fsearch_application_window_focus_search_entry(FsearchApplicationWindow *win);

GtkEntry *
fsearch_application_window_get_search_entry(FsearchApplicationWindow *self);

FsearchStatusbar *
fsearch_application_window_get_statusbar(FsearchApplicationWindow *self);

void
fsearch_application_window_update_query_flags(FsearchApplicationWindow *self);

void
fsearch_application_window_remove_model(FsearchApplicationWindow *self);

void
fsearch_application_window_set_database_index_text(FsearchApplicationWindow *self, const char *text);

uint32_t
fsearch_application_window_get_num_results(FsearchApplicationWindow *self);

gint
fsearch_application_window_get_active_filter(FsearchApplicationWindow *self);

void
fsearch_application_window_set_active_filter(FsearchApplicationWindow *self, guint active_filter);

void
fsearch_application_window_apply_search_revealer_config(FsearchApplicationWindow *win);

void
fsearch_application_window_added(FsearchApplicationWindow *win, FsearchApplication *app);

void
fsearch_application_window_removed(FsearchApplicationWindow *win, FsearchApplication *app);

void
fsearch_application_window_invert_selection(FsearchApplicationWindow *self);

void
fsearch_application_window_unselect_all(FsearchApplicationWindow *self);

void
fsearch_application_window_select_all(FsearchApplicationWindow *self);

uint32_t
fsearch_application_window_get_num_selected(FsearchApplicationWindow *self);

void
fsearch_application_window_selection_for_each(FsearchApplicationWindow *self, GHFunc func, gpointer user_data);

G_END_DECLS

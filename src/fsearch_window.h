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

#include "fsearch.h"
#include "query.h"

G_BEGIN_DECLS

#define FSEARCH_APPLICATION_WINDOW_TYPE (fsearch_application_window_get_type())

G_DECLARE_FINAL_TYPE(FsearchApplicationWindow,
                     fsearch_application_window,
                     FSEARCH_WINDOW,
                     WINDOW,
                     GtkApplicationWindow)

FsearchApplicationWindow *
fsearch_application_window_new(FsearchApplication *app);

void
fsearch_application_window_prepare_shutdown(gpointer self);

GtkTreeView *
fsearch_application_window_get_listview(FsearchApplicationWindow *self);

GtkTreeSelection *
fsearch_application_window_get_listview_selection(FsearchApplicationWindow *self);

void
fsearch_application_window_update_listview_config(FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_statusbar_revealer(FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_search_mode_revealer(FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_match_case_revealer(FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_search_in_path_revealer(FsearchApplicationWindow *self);

GtkEntry *
fsearch_application_window_get_search_entry(FsearchApplicationWindow *self);

gboolean
fsearch_application_window_update_search(FsearchApplicationWindow *self);

void
fsearch_application_window_apply_model(FsearchApplicationWindow *self);

void
fsearch_application_window_remove_model(FsearchApplicationWindow *self);

void
fsearch_application_window_update_database_label(FsearchApplicationWindow *self, const char *text);

FsearchQueryHighlight *
fsearch_application_window_get_query_highlight(FsearchApplicationWindow *self);

void
fsearch_application_window_update_results(void *data);

void
fsearch_window_apply_search_revealer_config(FsearchApplicationWindow *win);
G_END_DECLS

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

#include <glib.h>
#include <gtk/gtk.h>

#include "fsearch.h"

G_BEGIN_DECLS

#define FSEARCH_APPLICATION_WINDOW_TYPE (fsearch_application_window_get_type())

G_DECLARE_FINAL_TYPE (FsearchApplicationWindow, fsearch_application_window, FSEARCH_WINDOW, WINDOW, GtkApplicationWindow)

FsearchApplicationWindow *
fsearch_application_window_new (FsearchApplication *app);

GtkTreeSelection *
fsearch_application_window_get_listview_selection (FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_menubar (FsearchApplicationWindow *self);

GtkEntry *
fsearch_application_window_get_search_entry (FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_filter_combobox (FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_statusbar_search_mode_button (FsearchApplicationWindow *self);

GtkWidget *
fsearch_application_window_get_search_button (FsearchApplicationWindow *self);

void
fsearch_application_window_update_search (gpointer window);

void
fsearch_application_window_apply_model (gpointer window);

void
fsearch_application_window_remove_model (gpointer window);
G_END_DECLS


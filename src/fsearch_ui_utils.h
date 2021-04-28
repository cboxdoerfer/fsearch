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

#include <gtk/gtk.h>

void
ui_utils_run_gtk_dialog_async(GtkWidget *parent,
                              GtkMessageType type,
                              GtkButtonsType buttons,
                              const gchar *primary_text,
                              const gchar *sec_text,
                              GCallback response_cb,
                              gpointer response_cb_data);

gint
ui_utils_run_gtk_dialog(GtkWidget *parent,
                        GtkMessageType type,
                        GtkButtonsType buttons,
                        const gchar *primary_text,
                        const gchar *sec_text);

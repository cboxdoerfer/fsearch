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

#include "ui_utils.h"

gint
ui_utils_run_gtk_dialog(GtkWidget *parent,
                        GtkMessageType type,
                        GtkButtonsType buttons,
                        const gchar *primary_text,
                        const gchar *sec_text) {
    if (!parent || !primary_text) {
        return GTK_RESPONSE_CANCEL;
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT, type, buttons, primary_text, NULL);

    if (sec_text) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), sec_text, NULL);
    }

    gtk_window_set_title(GTK_WINDOW(dialog), "");

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response;
}


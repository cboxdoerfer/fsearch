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

#include "ui_utils.h"

gint
ui_utils_run_gtk_dialog (GtkWidget * window, const gchar *primary_text, const gchar *sec_text)
{
    if (!window || !primary_text) {
        return GTK_RESPONSE_CANCEL;
    }

    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                primary_text, NULL);

    if (sec_text) {
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                  sec_text, NULL);
    }

    gtk_window_set_title (GTK_WINDOW (dialog), "");
    //gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
    //gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 14);

    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

    gint response = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    //g_free (primary_text);
    //g_free (sec_text);

    return response;
}



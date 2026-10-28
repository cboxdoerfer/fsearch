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

#include "fsearch_ui_utils.h"

void
ui_utils_run_gtk_dialog_async(GtkWidget *parent,
                              GtkMessageType type,
                              GtkButtonsType buttons,
                              const gchar *primary_text,
                              const gchar *sec_text,
                              GCallback response_cb,
                              gpointer response_cb_data,
                              GDestroyNotify response_cb_data_free) {
    if (!primary_text) {
        if (response_cb_data_free) {
            response_cb_data_free(response_cb_data);
        }
        g_return_if_reached();
    }

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               type,
                                               buttons,
                                               primary_text,
                                               NULL);

    if (sec_text) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), sec_text, NULL);
    }

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), "");

    // connect_data so the dialog owns response_cb_data
    g_signal_connect_data(dialog,
                          "response",
                          G_CALLBACK(response_cb),
                          response_cb_data,
                          (GClosureNotify)response_cb_data_free,
                          0);
    gtk_widget_show(dialog);
}

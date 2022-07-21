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

#define G_LOG_DOMAIN "fsearch-clipboard"

#include "fsearch_clipboard.h"
#include <gtk/gtk.h>
#include <stdbool.h>
#include <string.h>

static GdkDragAction clipboard_action = GDK_ACTION_DEFAULT;
static GList *clipboard_file_list = NULL;

enum { URI_LIST = 1, NAUTILUS_WORKAROUND, GNOME_COPIED_FILES, KDE_CUT_SELECTION, N_CLIPBOARD_TARGETS };

static GtkTargetEntry targets[] = {{"text/uri-list", 0, URI_LIST},
                                   {"text/plain;charset=utf-8", 0, NAUTILUS_WORKAROUND},
                                   {"application/x-kde-cutselection", 0, KDE_CUT_SELECTION},
                                   {"x-special/gnome-copied-files", 0, GNOME_COPIED_FILES}};

static void
clipboard_clean_data(GtkClipboard *clipboard, gpointer user_data) {
    /* g_debug("clean clipboard!"); */
    if (clipboard_file_list) {
        g_list_free_full(g_steal_pointer(&clipboard_file_list), (GDestroyNotify)g_free);
    }
    clipboard_action = GDK_ACTION_DEFAULT;
}

static void
clipboard_get_data(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data) {
    if (!clipboard_file_list) {
        return;
    }

    if (info == KDE_CUT_SELECTION) {
        // Tell KDE that the selection data should be cut
        g_debug("[get_data] KDE_CUT_SELECTION");
        if (clipboard_action == GDK_ACTION_MOVE)
            gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8, (guchar *)"1", 2);
        return;
    }

    g_autoptr(GString) list = g_string_sized_new(8192);

    if (info == GNOME_COPIED_FILES) {
        g_debug("[get_data] GNOME_COPIED_FILES");
        const gchar *action = clipboard_action == GDK_ACTION_MOVE ? "cut\n" : "copy\n";
        g_string_append(list, action);
    }
    else if (info == URI_LIST) {
        g_debug("[get_data] URI_LIST");
    }
    else if (info == NAUTILUS_WORKAROUND) {
        g_debug("[get_data] NAUTILUS_WORKAROUND");
        g_string_append(list, "x-special/nautilus-clipboard\n");
        const gchar *action = clipboard_action == GDK_ACTION_MOVE ? "cut\n" : "copy\n";
        g_string_append(list, action);
    }
    else {
        g_debug("[get_data] unknown format: %d", info);
        return;
    }

    for (GList *l = clipboard_file_list; l; l = l->next) {
        g_autofree gchar *file_name = g_filename_to_uri((char *)l->data, NULL, NULL);
        g_string_append(list, file_name);

        if (l->next != NULL) {
            if (info == URI_LIST) {
                g_string_append(list, "\r\n");
            }
            else {
                g_string_append_c(list, '\n');
            }
        }
    }
    if (info == NAUTILUS_WORKAROUND) {
        g_string_append_c(list, '\n');
    }

    gtk_selection_data_set(selection_data,
                           gtk_selection_data_get_target(selection_data),
                           8,
                           (guchar *)list->str,
                           (gint)list->len + 1);
}

void
clipboard_copy_file_list(GList *file_list, bool copy) {
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_with_data(clip, targets, G_N_ELEMENTS(targets), clipboard_get_data, clipboard_clean_data, NULL);

    clipboard_file_list = file_list;
    clipboard_action = copy ? GDK_ACTION_COPY : GDK_ACTION_MOVE;
}

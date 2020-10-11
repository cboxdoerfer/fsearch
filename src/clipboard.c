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

#include "clipboard.h"
#include <gtk/gtk.h>
#include <string.h>

static GdkDragAction clipboard_action = GDK_ACTION_DEFAULT;
static GList *clipboard_file_list = NULL;

enum { URI_LIST = 1, NAUTILUS_WORKAROUND, GNOME_COPIED_FILES, N_CLIPBOARD_TARGETS };

static GtkTargetEntry targets[] = {{"text/uri-list", 0, URI_LIST},
                                   {"text/plain;charset=utf-8", 0, NAUTILUS_WORKAROUND},
                                   {"x-special/gnome-copied-files", 0, GNOME_COPIED_FILES}};

static void
clipboard_clean_data(GtkClipboard *clipboard, gpointer user_data) {
    /* g_debug("clean clipboard!\n"); */
    if (clipboard_file_list) {
        g_list_foreach(clipboard_file_list, (GFunc)g_free, NULL);
        g_list_free(clipboard_file_list);
        clipboard_file_list = NULL;
    }
    clipboard_action = GDK_ACTION_DEFAULT;
}

static void
clipboard_get_data(GtkClipboard *clipboard,
                   GtkSelectionData *selection_data,
                   guint info,
                   gpointer user_data) {
    if (!clipboard_file_list) {
        return;
    }

    GString *list = g_string_sized_new(8192);
    gboolean use_uri = FALSE;

    if (info == GNOME_COPIED_FILES) {
        const gchar *action = clipboard_action == GDK_ACTION_MOVE ? "cut\n" : "copy\n";
        g_string_append(list, action);
        use_uri = TRUE;
    }
    else if (info == URI_LIST) {
        use_uri = TRUE;
    }
    else if (info == NAUTILUS_WORKAROUND) {
        g_string_append(list, "x-special/nautilus-clipboard\n");
        const gchar *action = clipboard_action == GDK_ACTION_MOVE ? "cut\n" : "copy\n";
        g_string_append(list, action);
        use_uri = TRUE;
    }
    else {
        goto out;
    }

    GList *l = NULL;
    for (l = clipboard_file_list; l; l = l->next) {
        gchar *file_name = NULL;
        if (use_uri) {
            file_name = g_filename_to_uri((char *)l->data, NULL, NULL);
        }
        else {
            file_name = g_filename_display_name((char *)l->data);
        }
        g_string_append(list, file_name);
        g_free(file_name);

        if (l->next != NULL) {
            if (info == GNOME_COPIED_FILES) {
                g_string_append_c(list, '\n');
            }
            else if (info == URI_LIST) {
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
                           list->len + 1);

out:
    g_string_free(list, TRUE);
}

void
clipboard_copy_file_list(GList *file_list, guint32 copy) {
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_with_data(
        clip, targets, G_N_ELEMENTS(targets), clipboard_get_data, clipboard_clean_data, NULL);

    clipboard_file_list = file_list;
    clipboard_action = copy ? GDK_ACTION_COPY : GDK_ACTION_MOVE;
}

void
clipboard_copy_filepath_list(GList *file_list) {
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    GString *filepathlist = NULL;
    if (g_list_length(file_list) == 1) {
        gchar *file_name = NULL;
        file_name = (char *)(g_list_first(file_list))->data;
        filepathlist = g_string_new(file_name);
    }
    else {
        filepathlist = g_string_sized_new(8192);
        for (GList *file = file_list; file != NULL; file = file->next) {
            gchar *file_name = NULL;
            file_name = (char *)file->data;

            g_string_append(filepathlist, file_name);
            g_string_append(filepathlist, "\n");
        }
    }
    gtk_clipboard_set_text(clip, filepathlist->str, filepathlist->len);
    g_string_free(filepathlist, TRUE);
}

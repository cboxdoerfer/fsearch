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

#include <string.h>
#include <gtk/gtk.h>
#include "clipboard.h"

static GdkDragAction clipboard_action = GDK_ACTION_DEFAULT;
static GList* clipboard_file_list = NULL;

static void
clipboard_clean_data (GtkClipboard *clipboard,
                      gpointer user_data)
{
    /* g_debug("clean clipboard!\n"); */
    if (clipboard_file_list) {
        g_list_foreach(clipboard_file_list, (GFunc) g_free, NULL);
        g_list_free(clipboard_file_list);
        clipboard_file_list = NULL;
    }
    clipboard_action = GDK_ACTION_DEFAULT;
}

static void
clipboard_get_data (GtkClipboard *clipboard,
                    GtkSelectionData *selection_data,
                    guint info,
                    gpointer user_data)
{
    if (!clipboard_file_list)
        return ;

    GString *list = g_string_sized_new (8192);
    gboolean use_uri = FALSE;
    GdkAtom uri_list_target = gdk_atom_intern("text/uri-list", FALSE);
    GdkAtom gnome_target = gdk_atom_intern("x-special/gnome-copied-files", FALSE);

    if (gtk_selection_data_get_target (selection_data) == gnome_target)
    {
        const gchar *action = clipboard_action == GDK_ACTION_MOVE ? "cut\n" : "copy\n";
        g_string_append(list, action);
        use_uri = TRUE;
    }
    else if (gtk_selection_data_get_target (selection_data) == uri_list_target) {
        use_uri = TRUE;
    }


    GList* l = NULL;
    for (l = clipboard_file_list; l; l = l->next)
    {
        gchar* file_name = NULL;
        if (use_uri) {
            file_name = g_filename_to_uri((char*) l->data, NULL, NULL);
        }
        else {
            file_name = g_filename_display_name((char*) l->data);
        }
        g_string_append(list, file_name);
        g_free(file_name);

        if (l->next != NULL) {
            if (gtk_selection_data_get_target (selection_data) != uri_list_target) {
                g_string_append_c(list, '\n');
            }
            else {
                g_string_append(list, "\r\n");
            }
        }
    }

    gtk_selection_data_set (selection_data,
                            gtk_selection_data_get_target (selection_data),
                            8,
                            (guchar*) list->str, list->len + 1);
    //g_printf ("clipboard data:\n\"%s\"\n\n", list->str);
    g_string_free(list, TRUE);
}

void
clipboard_copy_file_list (GList* file_list, guint32 copy)
{
    GtkTargetList* target_list = gtk_target_list_new (NULL, 0);

    gtk_target_list_add_text_targets (target_list, 0);
    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    n_targets += 2;
    targets = g_renew (GtkTargetEntry, targets, n_targets);

    GtkTargetEntry *new_target = g_new0 (GtkTargetEntry, 1);
    new_target->target = "x-special/gnome-copied-files";
    g_memmove(&(targets[ n_targets - 2 ]), new_target, sizeof (GtkTargetEntry));

    new_target = g_new0(GtkTargetEntry, 1);
    new_target->target = "text/uri-list";
    g_memmove (&(targets[ n_targets - 1 ]), new_target, sizeof (GtkTargetEntry));

    gtk_target_list_unref (target_list);

    GtkClipboard * clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_with_data (clip, targets, n_targets,
                                 clipboard_get_data,
                                 clipboard_clean_data,
                                 NULL);
    g_free(targets);

    clipboard_file_list = file_list;
    clipboard_action = copy ? GDK_ACTION_COPY : GDK_ACTION_MOVE;
}

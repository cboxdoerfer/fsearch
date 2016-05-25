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

#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include "iconstore.h"

#define ICON_SIZE 24

GHashTable * pixbuf_hash_table = NULL;

static GdkPixbuf *
get_themed_icon_pixbuf (GThemedIcon * icon, int size, GtkIconTheme * icon_theme)
{
    char ** icon_names;
    GtkIconInfo * icon_info;
    GdkPixbuf * pixbuf;
    GError * error = NULL;

    g_object_get (icon, "names", &icon_names, NULL);

    icon_info = gtk_icon_theme_choose_icon (icon_theme, (const char **)icon_names, size, 0);
    if (icon_info == NULL) {
        icon_info = gtk_icon_theme_lookup_icon (icon_theme, "text-x-generic", size, GTK_ICON_LOOKUP_USE_BUILTIN);
    }
    pixbuf = gtk_icon_info_load_icon (icon_info, &error);
    if (pixbuf == NULL) {
        g_warning ("Could not load icon pixbuf: %s\n", error->message);
        g_clear_error (&error);
    }

#if GTK_CHECK_VERSION (3, 8, 0)
    g_object_unref (icon_info);
#else
    gtk_icon_info_free (icon_info);
#endif
    g_strfreev (icon_names);

    return pixbuf;
}

GdkPixbuf *
iconstore_get_pixbuf (GFileInfo * file_info)
{
    GdkPixbuf * pixbuf;
    GIcon * icon = NULL;

    if (file_info == NULL) {
        return NULL;
    }

    icon = g_file_info_get_icon (file_info);

    // create new hash table
    if (pixbuf_hash_table == NULL) {
        pixbuf_hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    gchar * icon_string;

    icon_string = g_icon_to_string (icon);
    pixbuf = (GdkPixbuf *) g_hash_table_lookup (pixbuf_hash_table, icon_string);

    if (pixbuf == NULL) {
        pixbuf = get_themed_icon_pixbuf (G_THEMED_ICON (icon), ICON_SIZE, gtk_icon_theme_get_default ());
        g_hash_table_insert (pixbuf_hash_table, g_strdup (icon_string), pixbuf);
    }
    g_free (icon_string);
    return pixbuf;
}

void
iconstore_clear ()
{
    if (pixbuf_hash_table) {
        g_hash_table_destroy (pixbuf_hash_table);
        pixbuf_hash_table = NULL;
    }
}

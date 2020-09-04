/*
   FSearch - A fast file search utility
   Copyright © 2017 Christian Boxdörfer

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

#if !GTK_CHECK_VERSION(3, 16, 0)
// Copied from gtk/gtkcssprovider.c
void
gtk_css_provider_load_from_resource(GtkCssProvider *css_provider, const gchar *resource_path) {
    GFile *file;
    gchar *uri, *escaped;

    g_return_if_fail(GTK_IS_CSS_PROVIDER(css_provider));
    g_return_if_fail(resource_path != NULL);

    escaped = g_uri_escape_string(resource_path, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
    uri = g_strconcat("resource://", escaped, NULL);
    g_free(escaped);

    file = g_file_new_for_uri(uri);
    g_free(uri);

    gtk_css_provider_load_from_file(css_provider, file, NULL);

    g_object_unref(file);
}
#endif

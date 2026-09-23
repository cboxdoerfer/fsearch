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

#pragma once

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _FsearchTrayIcon FsearchTrayIcon;

FsearchTrayIcon *
fsearch_tray_icon_new (GtkApplication *app);

void
fsearch_tray_icon_free (FsearchTrayIcon *self);

G_END_DECLS

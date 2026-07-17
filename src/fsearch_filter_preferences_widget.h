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

#include <gtk/gtk.h>

#include "fsearch_filter_manager.h"

G_BEGIN_DECLS

#define FSEARCH_FILTER_PREFERENCES_WIDGET_TYPE (fsearch_filter_preferences_widget_get_type())

G_DECLARE_FINAL_TYPE(FsearchFilterPreferencesWidget,
                     fsearch_filter_preferences_widget,
                     FSEARCH,
                     FILTER_PREFERENCES_WIDGET,
                     GtkBox)

FsearchFilterPreferencesWidget *
fsearch_filter_preferences_widget_new(FsearchFilterManager *filters);

FsearchFilterManager *
fsearch_filter_preferences_widget_get_filter_manager(FsearchFilterPreferencesWidget *widget);

G_END_DECLS
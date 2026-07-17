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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include "fsearch_config.h"

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum FsearchPreferencesDialogPage {
    FSEARCH_PREFERENCES_DIALOG_PAGE_GENERAL = 0,
    FSEARCH_PREFERENCES_DIALOG_PAGE_SEARCH,
    FSEARCH_PREFERENCES_DIALOG_PAGE_DATABASE,
    NUM_FSEARCH_PREFERENCES_DIALOG_PAGES,
} FsearchPreferencesDialogPage;

#define FSEARCH_PREFERENCES_DIALOG_TYPE (fsearch_preferences_dialog_get_type())

G_DECLARE_FINAL_TYPE(FsearchPreferencesDialog, fsearch_preferences_dialog, FSEARCH, PREFERENCES_DIALOG, GtkDialog)

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config);

void
fsearch_preferences_dialog_set_page(FsearchPreferencesDialog *self, FsearchPreferencesDialogPage page);

void
fsearch_preferences_dialog_bind_help(FsearchPreferencesDialog *self, GtkWidget *control, const char *help_page_name);

FsearchConfig *
fsearch_preferences_dialog_get_config(FsearchPreferencesDialog *self);

G_END_DECLS

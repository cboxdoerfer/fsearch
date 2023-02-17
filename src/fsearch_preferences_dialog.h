#pragma once

#include <gtk/gtk.h>

#include "fsearch_config.h"
#include "fsearch_database2.h"

G_BEGIN_DECLS

#define FSEARCH_PREFERENCES_DIALOG_TYPE (fsearch_preferences_dialog_get_type())

G_DECLARE_FINAL_TYPE(FsearchPreferencesDialog, fsearch_preferences_dialog, FSEARCH, PREFERENCES_DIALOG, GtkDialog)

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config, FsearchDatabase2 *db);

FsearchConfig *
fsearch_preferences_dialog_get_config(FsearchPreferencesDialog *self);

G_END_DECLS

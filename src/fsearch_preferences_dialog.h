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

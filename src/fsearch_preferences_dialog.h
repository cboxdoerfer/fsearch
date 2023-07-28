#pragma once

#include <gtk/gtk.h>

#include "fsearch_config.h"
#include "fsearch_database.h"

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
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config, FsearchDatabase *db);

void
fsearch_preferences_dialog_set_page(FsearchPreferencesDialog *self, FsearchPreferencesDialogPage page);

FsearchConfig *
fsearch_preferences_dialog_get_config(FsearchPreferencesDialog *self);

FsearchDatabaseIncludeManager *
fsearch_preferences_dialog_get_include_manager(FsearchPreferencesDialog *self);

FsearchDatabaseExcludeManager *
fsearch_preferences_dialog_get_exclude_manager(FsearchPreferencesDialog *self);

G_END_DECLS

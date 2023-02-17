#pragma once

#include <gtk/gtk.h>

#include "fsearch_database2.h"

G_BEGIN_DECLS

#define FSEARCH_DATABASE_PREFERENCES_WIDGET_TYPE (fsearch_database_preferences_widget_get_type())

G_DECLARE_FINAL_TYPE(FsearchDatabasePreferencesWidget,
                     fsearch_database_preferences_widget,
                     FSEARCH,
                     DATABASE_PREFERENCES_WIDGET,
                     GtkBox)

FsearchDatabasePreferencesWidget *
fsearch_database_preferences_widget_new(FsearchDatabase2 *db);

G_END_DECLS

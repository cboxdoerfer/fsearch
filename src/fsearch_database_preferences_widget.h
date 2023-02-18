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

FsearchDatabaseIncludeManager *
fsearch_database_preferences_widget_get_include_manager(FsearchDatabasePreferencesWidget *widget);

FsearchDatabaseExcludeManager *
fsearch_database_preferences_widget_get_exclude_manager(FsearchDatabasePreferencesWidget *widget);

G_END_DECLS

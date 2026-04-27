#pragma once

#include "fsearch_database.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_exclude_manager.h"

#include <glibconfig.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdint.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_PREFERENCES_WIDGET_TYPE (fsearch_database_preferences_widget_get_type())

G_DECLARE_FINAL_TYPE(FsearchDatabasePreferencesWidget,
                     fsearch_database_preferences_widget,
                     FSEARCH,
                     DATABASE_PREFERENCES_WIDGET,
                     GtkBox)

FsearchDatabasePreferencesWidget *
fsearch_database_preferences_widget_new(FsearchDatabase *db);

FsearchDatabaseIncludeManager *
fsearch_database_preferences_widget_get_include_manager(FsearchDatabasePreferencesWidget *widget);

FsearchDatabaseExcludeManager *
fsearch_database_preferences_widget_get_exclude_manager(FsearchDatabasePreferencesWidget *widget);

void
fsearch_database_preferences_widget_set_update_database_on_launch(FsearchDatabasePreferencesWidget *widget, bool enabled);

bool
fsearch_database_preferences_widget_get_update_database_on_launch(FsearchDatabasePreferencesWidget *widget);

void
fsearch_database_preferences_widget_set_update_database_every(FsearchDatabasePreferencesWidget *widget, bool enabled, uint32_t hours, uint32_t minutes);

bool
fsearch_database_preferences_widget_get_update_database_every(FsearchDatabasePreferencesWidget *widget);

uint32_t
fsearch_database_preferences_widget_get_update_database_every_hours(FsearchDatabasePreferencesWidget *widget);

uint32_t
fsearch_database_preferences_widget_get_update_database_every_minutes(FsearchDatabasePreferencesWidget *widget);

G_END_DECLS
#pragma once

#include "fsearch_database_include_manager.h"
#include "fsearch_database_exclude_manager.h"

#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdbool.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_PREFERENCES_WIDGET_TYPE (fsearch_database_preferences_widget_get_type())

G_DECLARE_FINAL_TYPE(FsearchDatabasePreferencesWidget,
                     fsearch_database_preferences_widget,
                     FSEARCH,
                     DATABASE_PREFERENCES_WIDGET,
                     GtkBox)

FsearchDatabasePreferencesWidget *
fsearch_database_preferences_widget_new(FsearchDatabaseIncludeManager *include_manager,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        bool ntfs_fast_scan_enabled);

FsearchDatabaseIncludeManager *
fsearch_database_preferences_widget_get_include_manager(FsearchDatabasePreferencesWidget *widget);

FsearchDatabaseExcludeManager *
fsearch_database_preferences_widget_get_exclude_manager(FsearchDatabasePreferencesWidget *widget);

/**
 * fsearch_database_preferences_widget_get_ntfs_config:
 * @widget: the widget
 * @ntfs_fast_scan_enabled: output parameter for fast scan enabled state
 *
 * Gets the current NTFS configuration from the widget.
 */
void
fsearch_database_preferences_widget_get_ntfs_config(FsearchDatabasePreferencesWidget *widget,
                                                    bool *ntfs_fast_scan_enabled);

/**
 * fsearch_database_preferences_widget_set_ntfs_partitions:
 * @widget: the widget
 * @ntfs_partitions: (element-type FsearchNtfsPartitionConfig): saved partition config from FsearchConfig
 *
 * Loads saved NTFS partition Include/Monitor state into the UI.
 * Call this after constructing the widget to restore persisted settings.
 */
void
fsearch_database_preferences_widget_set_ntfs_partitions(FsearchDatabasePreferencesWidget *widget,
                                                        GPtrArray *ntfs_partitions);

/**
 * fsearch_database_preferences_widget_get_ntfs_partitions:
 * @widget: the widget
 *
 * Reads the current NTFS partition Include/Monitor state from the UI.
 *
 * Returns: (element-type FsearchNtfsPartitionConfig) (transfer full): A newly
 *   allocated #GPtrArray of #FsearchNtfsPartitionConfig, or %NULL if no partitions.
 */
GPtrArray *
fsearch_database_preferences_widget_get_ntfs_partitions(FsearchDatabasePreferencesWidget *widget);

G_END_DECLS

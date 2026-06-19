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
                                        bool ntfs_fast_scan_enabled,
                                        bool ntfs_auto_polkit);

FsearchDatabaseIncludeManager *
fsearch_database_preferences_widget_get_include_manager(FsearchDatabasePreferencesWidget *widget);

FsearchDatabaseExcludeManager *
fsearch_database_preferences_widget_get_exclude_manager(FsearchDatabasePreferencesWidget *widget);

/**
 * fsearch_database_preferences_widget_get_ntfs_config:
 * @widget: the widget
 * @ntfs_fast_scan_enabled: output parameter for fast scan enabled state
 * @ntfs_auto_polkit: output parameter for auto polkit state
 *
 * Gets the current NTFS configuration from the widget.
 */
void
fsearch_database_preferences_widget_get_ntfs_config(FsearchDatabasePreferencesWidget *widget,
                                                    bool *ntfs_fast_scan_enabled,
                                                    bool *ntfs_auto_polkit);

/**
 * fsearch_database_preferences_widget_update_ntfs_status:
 * @widget: the widget
 * @is_root: whether running as root
 * @is_authorized: whether Polkit authorization is granted
 *
 * Updates the NTFS status labels (libntfs-3g and root permission status).
 */
void
fsearch_database_preferences_widget_update_ntfs_status(FsearchDatabasePreferencesWidget *widget,
                                                       bool is_root,
                                                       bool is_authorized);

G_END_DECLS

/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_config.h"
#include "fsearch_limits.h"
#include "debug.h"

const char *config_file_name = "fsearch.conf";
const char *config_folder_name = "fsearch";

void
config_build_dir (char *path, size_t len)
{
    g_assert (path != NULL);
    g_assert (len >= 0);

    const gchar *xdg_conf_dir = g_get_user_config_dir ();
    snprintf (path, len, "%s/%s", xdg_conf_dir, config_folder_name);
    return;
}

static void
config_build_path (char *path, size_t len)
{
    g_assert (path != NULL);
    g_assert (len >= 0);

    const gchar *xdg_conf_dir = g_get_user_config_dir ();
    snprintf (path, len, "%s/%s/%s", xdg_conf_dir, config_folder_name, config_file_name);
    return;
}

bool
config_make_dir (void)
{
    gchar config_dir[PATH_MAX] = "";
    config_build_dir (config_dir, sizeof (config_dir));
    return !g_mkdir_with_parents (config_dir, 0700);
}

static void
config_load_handle_error (GError *error)
{
    if (!error) {
        return;
    }
    switch (error->code) {
        case G_KEY_FILE_ERROR_INVALID_VALUE:
            fprintf(stderr, "load_config: invalid value: %s\n", error->message);
            break;
        case G_KEY_FILE_ERROR_KEY_NOT_FOUND:
        case G_KEY_FILE_ERROR_GROUP_NOT_FOUND:
            // new config, use default value and don't report anything
            break;
        default:
            fprintf(stderr, "load_config: unknown error: %s\n", error->message);
    }
    g_error_free (error);
}

static uint32_t
config_load_integer (GKeyFile *key_file,
                     const char *group_name,
                     const char *key,
                     uint32_t default_value)
{
    GError *error = NULL;
    uint32_t result = g_key_file_get_integer (key_file, group_name, key, &error);
    if (error != NULL) {
        result = default_value;
        config_load_handle_error (error);
    }
    return result;
}

static bool
config_load_boolean (GKeyFile *key_file,
                     const char *group_name,
                     const char *key,
                     bool default_value)
{
    GError *error = NULL;
    bool result = g_key_file_get_boolean (key_file, group_name, key, &error);
    if (error != NULL) {
        result = default_value;
        config_load_handle_error (error);
    }
    return result;
}

static char *
config_load_string (GKeyFile *key_file,
                    const char *group_name,
                    const char *key,
                    char *default_value)
{
    GError *error = NULL;
    char *result = g_key_file_get_string (key_file, group_name, key, &error);
    if (error != NULL) {
        result = default_value;
        config_load_handle_error (error);
    }
    return result;
}

bool
config_load (FsearchConfig *config)
{
    g_assert (config != NULL);

    bool result = false;
    GKeyFile *key_file = g_key_file_new ();
    g_assert (key_file != NULL);

    gchar config_path[PATH_MAX] = "";
    config_build_path (config_path, sizeof (config_path));

    GError *error = NULL;
    if (g_key_file_load_from_file (key_file, config_path, G_KEY_FILE_NONE, &error)) {
        trace ("loaded config file\n");
        // Interface
        config->restore_column_config = config_load_boolean (key_file,
                                                            "Interface",
                                                            "restore_column_configuration",
                                                            false);
        config->enable_list_tooltips = config_load_boolean (key_file,
                                                            "Interface",
                                                            "enable_list_tooltips",
                                                            true);
        config->enable_dark_theme = config_load_boolean (key_file,
                                                         "Interface",
                                                         "enable_dark_theme",
                                                         false);
        config->show_menubar = config_load_boolean (key_file,
                                                    "Interface",
                                                    "show_menubar",
                                                    true);
        config->show_statusbar = config_load_boolean (key_file,
                                                      "Interface",
                                                      "show_statusbar",
                                                      true);
        config->show_filter = config_load_boolean (key_file,
                                                   "Interface",
                                                   "show_filter",
                                                   true);
        config->show_search_button = config_load_boolean (key_file,
                                                          "Interface",
                                                          "show_search_button",
                                                          true);
        config->show_base_2_units = config_load_boolean (key_file,
                                                         "Interface",
                                                         "show_base_2_units",
                                                         false);
        config->action_after_file_open = config_load_integer(key_file,
                                                             "Interface",
                                                             "action_after_file_open",
                                                             0);
        config->action_after_file_open_keyboard = config_load_boolean (key_file,
                                                                    "Interface",
                                                                    "action_after_file_open_keyboard",
                                                                    false);
        config->action_after_file_open_mouse = config_load_boolean (key_file,
                                                                    "Interface",
                                                                    "action_after_file_open_mouse",
                                                                    false);

        // Warning Dialogs
        config->show_dialog_failed_opening = config_load_boolean(key_file,
                                                                 "Dialogs",
                                                                 "show_dialog_failed_opening",
                                                                 true);

        // Default actions
        config->action_failed_opening_stay_open = config_load_boolean(key_file,
                                                                     "Default_Actions",
                                                                     "action_failed_opening_stay_open",
                                                                     true);

        // Window
        config->restore_window_size = config_load_boolean (key_file,
                                                         "Interface",
                                                         "restore_window_size",
                                                         false);
        config->window_width = config_load_integer (key_file,
                                                    "Interface",
                                                    "window_width",
                                                    800);
        config->window_height = config_load_integer (key_file,
                                                     "Interface",
                                                     "window_height",
                                                     600);

        // Columns
        config->show_listview_icons = config_load_boolean (key_file,
                                                        "Interface",
                                                        "show_listview_icons",
                                                        true);
        config->show_path_column = config_load_boolean (key_file,
                                                        "Interface",
                                                        "show_path_column",
                                                        true);
        config->show_type_column = config_load_boolean (key_file,
                                                        "Interface",
                                                        "show_type_column",
                                                        true);
        config->show_size_column = config_load_boolean (key_file,
                                                        "Interface",
                                                        "show_size_column",
                                                        true);
        config->show_modified_column = config_load_boolean (key_file,
                                                            "Interface",
                                                            "show_modified_column",
                                                            true);

        // Column Size
        config->name_column_width = config_load_integer (key_file,
                                                   "Interface",
                                                   "name_column_width",
                                                   250);
        config->path_column_width = config_load_integer (key_file,
                                                   "Interface",
                                                   "path_column_width",
                                                   250);
        config->type_column_width = config_load_integer (key_file,
                                                   "Interface",
                                                   "type_column_width",
                                                   100);
        config->size_column_width = config_load_integer (key_file,
                                                   "Interface",
                                                   "size_column_width",
                                                   75);
        config->modified_column_width = config_load_integer (key_file,
                                                   "Interface",
                                                   "modified_column_width",
                                                   75);

        // Column position
        config->name_column_pos = config_load_integer (key_file,
                                                   "Interface",
                                                   "name_column_pos",
                                                   0);
        config->path_column_pos = config_load_integer (key_file,
                                                   "Interface",
                                                   "path_column_pos",
                                                   1);
        config->type_column_pos = config_load_integer (key_file,
                                                   "Interface",
                                                   "type_column_pos",
                                                   2);
        config->size_column_pos = config_load_integer (key_file,
                                                   "Interface",
                                                   "size_column_pos",
                                                   3);
        config->modified_column_pos = config_load_integer (key_file,
                                                   "Interface",
                                                   "modified_column_pos",
                                                   4);

        // Search
        config->search_as_you_type = config_load_boolean (key_file,
                                                          "Search",
                                                          "search_as_you_type",
                                                          true);
        config->auto_search_in_path = config_load_boolean (key_file,
                                                           "Search",
                                                           "auto_search_in_path",
                                                           true);
        config->match_case = config_load_boolean (key_file,
                                                  "Search",
                                                  "match_case",
                                                  false);
        config->enable_regex = config_load_boolean (key_file,
                                                    "Search",
                                                    "enable_regex",
                                                    false);
        config->search_in_path = config_load_boolean (key_file,
                                                      "Search",
                                                      "search_in_path",
                                                      false);
        config->hide_results_on_empty_search = config_load_boolean (key_file,
                                                                    "Search",
                                                                    "hide_results_on_empty_search",
                                                                    true);
        config->limit_results = config_load_boolean (key_file,
                                                     "Search",
                                                     "limit_results",
                                                     false);
        config->num_results = config_load_integer (key_file,
                                                   "Search",
                                                   "num_results",
                                                   1000);

        // Database
        config->update_database_on_launch = config_load_boolean (key_file,
                                                                 "Database",
                                                                 "update_database_on_launch",
                                                                 false);
        config->exclude_hidden_items = config_load_boolean (key_file,
                                                            "Database",
                                                            "exclude_hidden_files_and_folders",
                                                            false);
        config->follow_symlinks = config_load_boolean (key_file,
                                                       "Database",
                                                       "follow_symbolic_links",
                                                       false);

        char *exclude_files_str = config_load_string (key_file, "Database", "exclude_files", NULL);
        if (exclude_files_str) {
            config->exclude_files = g_strsplit (exclude_files_str, ";", -1);
            free (exclude_files_str);
            exclude_files_str = NULL;
        }

        // Locations
        uint32_t pos = 1;
        while (true) {
            char key[100] = "";
            snprintf (key, sizeof (key), "location_%d", pos++);
            char *value = config_load_string (key_file, "Database", key, NULL);
            if (value) {
                config->locations = g_list_append (config->locations, value);
            }
            else {
                break;
            }
        }
        // Exclude
        pos = 1;
        while (true) {
            char key[100] = "";
            snprintf (key, sizeof (key), "exclude_location_%d", pos++);
            char *value = config_load_string (key_file, "Database", key, NULL);
            if (value) {
                config->exclude_locations = g_list_append (config->exclude_locations, value);
            }
            else {
                break;
            }
        }

        result = true;
    }
    else {
        fprintf(stderr, "load config failed: %s\n", error->message);
        g_error_free (error);
    }

    g_key_file_free (key_file);
    return result;
}

bool
config_load_default (FsearchConfig *config)
{
    g_assert (config != NULL);

    // Search
    config->auto_search_in_path = true;
    config->search_as_you_type = true;
    config->match_case = false;
    config->enable_regex = false;
    config->search_in_path = false;
    config->hide_results_on_empty_search = true;
    config->limit_results = true;
    config->num_results = 1000;

    // Interface
    config->enable_dark_theme = false;
    config->enable_list_tooltips = true;
    config->restore_column_config = false;
    config->show_menubar = true;
    config->show_statusbar = true;
    config->show_filter = true;
    config->show_search_button = true;
    config->show_base_2_units = false;
    config->action_after_file_open = 0;
    config->action_after_file_open_keyboard = false;
    config->action_after_file_open_mouse = false;

    // Columns
    config->show_listview_icons = true;
    config->show_path_column = true;
    config->show_type_column = true;
    config->show_size_column = true;
    config->show_modified_column = true;

    config->name_column_pos = 0;
    config->path_column_pos = 1;
    config->type_column_pos = 2;
    config->size_column_pos = 3;
    config->modified_column_pos = 4;

    config->name_column_width = 250;
    config->path_column_width = 250;
    config->type_column_width = 100;
    config->size_column_width = 75;
    config->modified_column_width = 125;

    // Warning Dialogs
    config->show_dialog_failed_opening = true;

    // Default Actions
    config->action_failed_opening_stay_open = true;

    // Window
    config->restore_window_size = false;
    config->window_width = 800;
    config->window_height = 600;

    // Database
    config->update_database_on_launch = false;
    config->exclude_hidden_items = false;
    config->follow_symlinks = false;

    // Locations
    config->locations = NULL;
    config->exclude_locations = NULL;

    return true;
}

bool
config_save (FsearchConfig *config)
{
    g_assert (config != NULL);

    bool result = false;
    GKeyFile *key_file = g_key_file_new ();
    g_assert (key_file != NULL);

    // Interface
    g_key_file_set_boolean (key_file, "Interface", "restore_column_configuration", config->restore_column_config);
    g_key_file_set_boolean (key_file, "Interface", "enable_list_tooltips", config->enable_list_tooltips);
    g_key_file_set_boolean (key_file, "Interface", "enable_dark_theme", config->enable_dark_theme);
    g_key_file_set_boolean (key_file, "Interface", "show_menubar", config->show_menubar);
    g_key_file_set_boolean (key_file, "Interface", "show_statusbar", config->show_statusbar);
    g_key_file_set_boolean (key_file, "Interface", "show_filter", config->show_filter);
    g_key_file_set_boolean (key_file, "Interface", "show_search_button", config->show_search_button);
    g_key_file_set_boolean (key_file, "Interface", "show_base_2_units", config->show_base_2_units);
    g_key_file_set_integer (key_file, "Interface", "action_after_file_open", config->action_after_file_open);
    g_key_file_set_boolean (key_file, "Interface", "action_after_file_open_keyboard", config->action_after_file_open_keyboard);
    g_key_file_set_boolean (key_file, "Interface", "action_after_file_open_mouse", config->action_after_file_open_mouse);

    // Warning Dialogs
    g_key_file_set_boolean (key_file, "Dialogs", "show_dialog_failed_opening", config->show_dialog_failed_opening);

    // Default Actions
    g_key_file_set_boolean (key_file, "Default_Actions", "action_failed_opening_stay_open", config->action_failed_opening_stay_open);

    // Window
    g_key_file_set_boolean (key_file, "Interface", "restore_window_size", config->restore_window_size);
    g_key_file_set_integer (key_file, "Interface", "window_width", config->window_width);
    g_key_file_set_integer (key_file, "Interface", "window_height", config->window_height);

    // Columns visibility
    g_key_file_set_boolean (key_file, "Interface", "show_listview_icons", config->show_listview_icons);
    g_key_file_set_boolean (key_file, "Interface", "show_path_column", config->show_path_column);
    g_key_file_set_boolean (key_file, "Interface", "show_type_column", config->show_type_column);
    g_key_file_set_boolean (key_file, "Interface", "show_size_column", config->show_size_column);
    g_key_file_set_boolean (key_file, "Interface", "show_modified_column", config->show_modified_column);

    // Column width
    g_key_file_set_integer (key_file, "Interface", "name_column_width", config->name_column_width);
    g_key_file_set_integer (key_file, "Interface", "path_column_width", config->path_column_width);
    g_key_file_set_integer (key_file, "Interface", "type_column_width", config->type_column_width);
    g_key_file_set_integer (key_file, "Interface", "size_column_width", config->size_column_width);
    g_key_file_set_integer (key_file, "Interface", "modified_column_width", config->modified_column_width);

    // Column position
    g_key_file_set_integer (key_file, "Interface", "name_column_pos", config->name_column_pos);
    g_key_file_set_integer (key_file, "Interface", "path_column_pos", config->path_column_pos);
    g_key_file_set_integer (key_file, "Interface", "type_column_pos", config->type_column_pos);
    g_key_file_set_integer (key_file, "Interface", "size_column_pos", config->size_column_pos);
    g_key_file_set_integer (key_file, "Interface", "modified_column_pos", config->modified_column_pos);

    // Search
    g_key_file_set_boolean (key_file, "Search", "search_as_you_type", config->search_as_you_type);
    g_key_file_set_boolean (key_file, "Search", "auto_search_in_path", config->auto_search_in_path);
    g_key_file_set_boolean (key_file, "Search", "search_in_path", config->search_in_path);
    g_key_file_set_boolean (key_file, "Search", "enable_regex", config->enable_regex);
    g_key_file_set_boolean (key_file, "Search", "match_case", config->match_case);
    g_key_file_set_boolean (key_file, "Search", "hide_results_on_empty_search", config->hide_results_on_empty_search);
    g_key_file_set_boolean (key_file, "Search", "limit_results", config->limit_results);
    g_key_file_set_integer (key_file, "Search", "num_results", config->num_results);

    // Database
    g_key_file_set_boolean (key_file, "Database", "update_database_on_launch", config->update_database_on_launch);
    g_key_file_set_boolean (key_file, "Database", "exclude_hidden_files_and_folders", config->exclude_hidden_items);
    g_key_file_set_boolean (key_file, "Database", "follow_symbolic_links", config->follow_symlinks);

    if (config->locations) {
        uint32_t pos = 1;
        for (GList *l = config->locations; l != NULL; l = l->next) {
            char location[100] = "";
            snprintf (location, sizeof (location), "location_%d", pos);
            g_key_file_set_string (key_file, "Database", location, l->data);
            pos++;
        }
    }

    if (config->exclude_locations) {
        uint32_t pos = 1;
        for (GList *l = config->exclude_locations; l != NULL; l = l->next) {
            char location[100] = "";
            snprintf (location, sizeof (location), "exclude_location_%d", pos);
            g_key_file_set_string (key_file, "Database", location, l->data);
            pos++;
        }
    }

    if (config->exclude_files) {
        char *exclude_files_str = g_strjoinv (";", config->exclude_files);
        g_key_file_set_string (key_file, "Database", "exclude_files", exclude_files_str);
        free (exclude_files_str);
    }

    gchar config_path[PATH_MAX] = "";
    config_build_path (config_path, sizeof (config_path));

    GError *error = NULL;
    if (g_key_file_save_to_file (key_file, config_path, &error)) {
        trace ("saved config file\n");
        result = true;
    }
    else {
        fprintf(stderr, "save config failed: %s\n", error->message);
    }

    g_key_file_free (key_file);
    return result;
}

void
config_free (FsearchConfig *config)
{
    g_assert (config != NULL);

    if (config->locations) {
        g_list_free_full (config->locations, (GDestroyNotify)free);
        config->locations = NULL;
    }
    if (config->exclude_locations) {
        g_list_free_full (config->exclude_locations, (GDestroyNotify)free);
        config->exclude_locations = NULL;
    }
    if (config->exclude_files) {
        g_strfreev (config->exclude_files);
        config->exclude_files = NULL;
    }
    free (config);
    config = NULL;
}


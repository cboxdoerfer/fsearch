/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

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

#define G_LOG_DOMAIN "fsearch-config"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsearch_config.h"
#include "fsearch_exclude_path.h"
#include "fsearch_index.h"
#include "fsearch_limits.h"

const char *config_file_name = "fsearch.conf";
const char *config_folder_name = "fsearch";

void
config_build_dir(char *path, size_t len) {
    g_assert(path != NULL);

    const gchar *xdg_conf_dir = g_get_user_config_dir();
    snprintf(path, len, "%s/%s", xdg_conf_dir, config_folder_name);
    return;
}

static void
config_build_path(char *path, size_t len) {
    g_assert(path != NULL);

    const gchar *xdg_conf_dir = g_get_user_config_dir();
    snprintf(path, len, "%s/%s/%s", xdg_conf_dir, config_folder_name, config_file_name);
    return;
}

bool
config_make_dir(void) {
    gchar config_dir[PATH_MAX] = "";
    config_build_dir(config_dir, sizeof(config_dir));
    return !g_mkdir_with_parents(config_dir, 0700);
}

static void
config_load_handle_error(GError *error) {
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
    g_clear_pointer(&error, g_error_free);
}

static uint32_t
config_load_integer(GKeyFile *key_file, const char *group_name, const char *key, uint32_t default_value) {
    GError *error = NULL;
    uint32_t result = g_key_file_get_integer(key_file, group_name, key, &error);
    if (error != NULL) {
        result = default_value;
        config_load_handle_error(error);
    }
    return result;
}

static bool
config_load_boolean(GKeyFile *key_file, const char *group_name, const char *key, bool default_value) {
    GError *error = NULL;
    bool result = g_key_file_get_boolean(key_file, group_name, key, &error);
    if (error != NULL) {
        result = default_value;
        config_load_handle_error(error);
    }
    return result;
}

static char *
config_load_string(GKeyFile *key_file, const char *group_name, const char *key, const char *default_value) {
    GError *error = NULL;
    char *result = g_key_file_get_string(key_file, group_name, key, &error);
    if (error != NULL) {
        result = g_strdup(default_value);
        config_load_handle_error(error);
    }
    return result;
}

static GList *
config_load_indexes(GKeyFile *key_file, GList *indexes, const char *prefix) {
    uint32_t pos = 1;
    while (true) {
        char key[100] = "";
        snprintf(key, sizeof(key), "%s_%d", prefix, pos);
        char *path = config_load_string(key_file, "Database", key, NULL);
        snprintf(key, sizeof(key), "%s_enabled_%d", prefix, pos);
        bool enabled = config_load_boolean(key_file, "Database", key, true);
        snprintf(key, sizeof(key), "%s_update_%d", prefix, pos);
        bool update = config_load_boolean(key_file, "Database", key, true);

        pos++;
        if (path) {
            FsearchIndex *index = fsearch_index_new(FSEARCH_INDEX_FOLDER_TYPE, path, enabled, update, 0);
            indexes = g_list_append(indexes, index);
        }
        else {
            break;
        }
    }
    return indexes;
}

static GList *
config_load_exclude_locations(GKeyFile *key_file, GList *locations, const char *prefix) {
    uint32_t pos = 1;
    while (true) {
        char key[100] = "";
        snprintf(key, sizeof(key), "%s_%d", prefix, pos);
        char *path = config_load_string(key_file, "Database", key, NULL);
        snprintf(key, sizeof(key), "%s_enabled_%d", prefix, pos);
        bool enabled = config_load_boolean(key_file, "Database", key, true);

        pos++;
        if (path) {
            FsearchExcludePath *fs_path = fsearch_exclude_path_new(path, enabled);
            locations = g_list_append(locations, fs_path);
        }
        else {
            break;
        }
    }
    return locations;
}

bool
config_load(FsearchConfig *config) {
    g_assert(config != NULL);

    bool result = false;
    GKeyFile *key_file = g_key_file_new();
    g_assert(key_file != NULL);

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    gchar config_path[PATH_MAX] = "";
    config_build_path(config_path, sizeof(config_path));

    const char *debug_message = NULL;

    GError *error = NULL;
    if (g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_NONE, &error)) {
        g_debug("[config] loading...");
        // Interface
        config->highlight_search_terms = config_load_boolean(key_file, "Interface", "highlight_search_terms", true);
        config->single_click_open = config_load_boolean(key_file, "Interface", "single_click_open", false);
        config->restore_sort_order = config_load_boolean(key_file, "Interface", "restore_sort_order", true);
        config->restore_column_config =
            config_load_boolean(key_file, "Interface", "restore_column_configuration", false);
        config->double_click_path = config_load_boolean(key_file, "Interface", "double_click_path", false);
        config->enable_list_tooltips = config_load_boolean(key_file, "Interface", "enable_list_tooltips", true);
        config->enable_dark_theme = config_load_boolean(key_file, "Interface", "enable_dark_theme", false);
        config->show_menubar = config_load_boolean(key_file, "Interface", "show_menubar", true);
        config->show_statusbar = config_load_boolean(key_file, "Interface", "show_statusbar", true);
        config->show_filter = config_load_boolean(key_file, "Interface", "show_filter", true);
        config->show_search_button = config_load_boolean(key_file, "Interface", "show_search_button", true);
        config->show_base_2_units = config_load_boolean(key_file, "Interface", "show_base_2_units", false);
        config->action_after_file_open =
            config_load_integer(key_file, "Interface", "action_after_file_open", ACTION_AFTER_OPEN_NOTHING);
        config->action_after_file_open_keyboard =
            config_load_boolean(key_file, "Interface", "action_after_file_open_keyboard", false);
        config->action_after_file_open_mouse =
            config_load_boolean(key_file, "Interface", "action_after_file_open_mouse", false);
        config->show_indexing_status = config_load_boolean(key_file, "Interface", "show_indexing_status", true);

        // Warning Dialogs
        config->show_dialog_failed_opening =
            config_load_boolean(key_file, "Dialogs", "show_dialog_failed_opening", true);

        // Applications
        config->folder_open_cmd = config_load_string(key_file, "Applications", "folder_open_cmd", NULL);

        // Window
        config->restore_window_size = config_load_boolean(key_file, "Interface", "restore_window_size", false);
        config->window_width = config_load_integer(key_file, "Interface", "window_width", 800);
        config->window_height = config_load_integer(key_file, "Interface", "window_height", 600);

        // Columns
        if (config->restore_column_config) {
            config->show_listview_icons = config_load_boolean(key_file, "Interface", "show_listview_icons", true);
            config->show_path_column = config_load_boolean(key_file, "Interface", "show_path_column", true);
            config->show_type_column = config_load_boolean(key_file, "Interface", "show_type_column", false);
            config->show_extension_column = config_load_boolean(key_file, "Interface", "show_extension_column", true);
            config->show_size_column = config_load_boolean(key_file, "Interface", "show_size_column", true);
            config->show_modified_column = config_load_boolean(key_file, "Interface", "show_modified_column", true);
        }
        else {
            config->show_listview_icons = true;
            config->show_path_column = true;
            config->show_type_column = false;
            config->show_extension_column = true;
            config->show_size_column = true;
            config->show_modified_column = true;
        }

        // Column Sort
        config->sort_ascending = config_load_boolean(key_file, "Interface", "sort_ascending", true);
        config->sort_by = config_load_string(key_file, "Interface", "sort_by", "Name");

        // Column Size
        config->name_column_width = config_load_integer(key_file, "Interface", "name_column_width", 250);
        config->path_column_width = config_load_integer(key_file, "Interface", "path_column_width", 250);
        config->extension_column_width = config_load_integer(key_file, "Interface", "extension_column_width", 100);
        config->type_column_width = config_load_integer(key_file, "Interface", "type_column_width", 100);
        config->size_column_width = config_load_integer(key_file, "Interface", "size_column_width", 75);
        config->modified_column_width = config_load_integer(key_file, "Interface", "modified_column_width", 75);

        // Column position
        config->name_column_pos = config_load_integer(key_file, "Interface", "name_column_pos", 0);
        config->path_column_pos = config_load_integer(key_file, "Interface", "path_column_pos", 1);
        config->type_column_pos = config_load_integer(key_file, "Interface", "type_column_pos", 2);
        config->size_column_pos = config_load_integer(key_file, "Interface", "size_column_pos", 3);
        config->modified_column_pos = config_load_integer(key_file, "Interface", "modified_column_pos", 4);

        // Search
        config->search_as_you_type = config_load_boolean(key_file, "Search", "search_as_you_type", true);
        config->auto_match_case = config_load_boolean(key_file, "Search", "auto_match_case", true);
        config->auto_search_in_path = config_load_boolean(key_file, "Search", "auto_search_in_path", true);
        config->match_case = config_load_boolean(key_file, "Search", "match_case", false);
        config->enable_regex = config_load_boolean(key_file, "Search", "enable_regex", false);
        config->search_in_path = config_load_boolean(key_file, "Search", "search_in_path", false);
        config->hide_results_on_empty_search =
            config_load_boolean(key_file, "Search", "hide_results_on_empty_search", true);

        // Database
        config->update_database_on_launch =
            config_load_boolean(key_file, "Database", "update_database_on_launch", true);
        config->update_database_every = config_load_boolean(key_file, "Database", "update_database_every", false);
        config->update_database_every_hours =
            config_load_integer(key_file, "Database", "update_database_every_hours", 0);
        config->update_database_every_minutes =
            config_load_integer(key_file, "Database", "update_database_every_minutes", 15);
        config->exclude_hidden_items =
            config_load_boolean(key_file, "Database", "exclude_hidden_files_and_folders", false);
        config->follow_symlinks = config_load_boolean(key_file, "Database", "follow_symbolic_links", false);

        char *exclude_files_str = config_load_string(key_file, "Database", "exclude_files", NULL);
        if (exclude_files_str) {
            config->exclude_files = g_strsplit(exclude_files_str, ";", -1);
            g_clear_pointer(&exclude_files_str, free);
        }

        config->indexes = config_load_indexes(key_file, config->indexes, "location");
        config->exclude_locations =
            config_load_exclude_locations(key_file, config->exclude_locations, "exclude_location");

        result = true;
        debug_message = "[config] loaded in %f ms";
    }
    else {
        debug_message = "[config] loading failed (%f ms)";
        g_clear_pointer(&error, g_error_free);
    }
    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);

    g_clear_pointer(&timer, g_timer_destroy);

    g_debug(debug_message, seconds * 1000);

    g_clear_pointer(&key_file, g_key_file_free);
    return result;
}

bool
config_load_default(FsearchConfig *config) {
    g_assert(config != NULL);

    // Search
    config->auto_search_in_path = true;
    config->auto_match_case = true;
    config->search_as_you_type = true;
    config->match_case = false;
    config->enable_regex = false;
    config->search_in_path = false;
    config->hide_results_on_empty_search = true;

    // Interface
    config->single_click_open = false;
    config->highlight_search_terms = true;
    config->enable_dark_theme = false;
    config->enable_list_tooltips = true;
    config->restore_column_config = false;
    config->restore_sort_order = true;
    config->double_click_path = false;
    config->show_menubar = true;
    config->show_statusbar = true;
    config->show_filter = true;
    config->show_search_button = true;
    config->show_base_2_units = false;
    config->action_after_file_open = ACTION_AFTER_OPEN_NOTHING;
    config->action_after_file_open_keyboard = false;
    config->action_after_file_open_mouse = false;
    config->show_indexing_status = true;

    // Columns
    config->show_listview_icons = true;
    config->show_path_column = true;
    config->show_type_column = false;
    config->show_extension_column = false;
    config->show_size_column = true;
    config->show_modified_column = true;

    config->sort_by = NULL;
    config->sort_ascending = true;

    config->name_column_pos = 0;
    config->path_column_pos = 1;
    config->type_column_pos = 2;
    config->size_column_pos = 3;
    config->modified_column_pos = 4;

    config->name_column_width = 250;
    config->path_column_width = 250;
    config->extension_column_width = 100;
    config->type_column_width = 100;
    config->size_column_width = 75;
    config->modified_column_width = 125;

    // Warning Dialogs
    config->show_dialog_failed_opening = true;

    // Window
    config->restore_window_size = false;
    config->window_width = 800;
    config->window_height = 600;

    // Database
    config->update_database_on_launch = true;
    config->update_database_every = false;
    config->update_database_every_hours = 0;
    config->update_database_every_minutes = 15;
    config->exclude_hidden_items = false;
    config->follow_symlinks = false;

    // Locations
    config->indexes = NULL;
    FsearchIndex *index = fsearch_index_new(FSEARCH_INDEX_FOLDER_TYPE, g_get_home_dir(), true, true, 0);
    config->indexes = g_list_append(config->indexes, index);
    config->exclude_locations = NULL;

    return true;
}

static void
config_save_indexes(GKeyFile *key_file, GList *indexes, const char *prefix) {
    if (!indexes) {
        return;
    }

    uint32_t pos = 1;
    for (GList *l = indexes; l != NULL; l = l->next) {
        FsearchIndex *index = l->data;
        if (!index) {
            continue;
        }

        char key[100] = "";
        snprintf(key, sizeof(key), "%s_%d", prefix, pos);
        g_key_file_set_string(key_file, "Database", key, index->path);

        snprintf(key, sizeof(key), "%s_enabled_%d", prefix, pos);
        g_key_file_set_boolean(key_file, "Database", key, index->enabled);

        snprintf(key, sizeof(key), "%s_update_%d", prefix, pos);
        g_key_file_set_boolean(key_file, "Database", key, index->update);

        pos++;
    }
}

static void
config_save_exclude_locations(GKeyFile *key_file, GList *locations, const char *prefix) {
    if (!locations) {
        return;
    }

    uint32_t pos = 1;
    for (GList *l = locations; l != NULL; l = l->next) {
        FsearchExcludePath *index = l->data;
        if (!index) {
            continue;
        }

        char key[100] = "";
        snprintf(key, sizeof(key), "%s_%d", prefix, pos);
        g_key_file_set_string(key_file, "Database", key, index->path);

        snprintf(key, sizeof(key), "%s_enabled_%d", prefix, pos);
        g_key_file_set_boolean(key_file, "Database", key, index->enabled);

        pos++;
    }
}

bool
config_save(FsearchConfig *config) {
    g_assert(config != NULL);

    bool result = false;
    GKeyFile *key_file = g_key_file_new();
    g_assert(key_file != NULL);

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    g_debug("[config] saving...");

    // Interface
    g_key_file_set_boolean(key_file, "Interface", "single_click_open", config->single_click_open);
    g_key_file_set_boolean(key_file, "Interface", "highlight_search_terms", config->highlight_search_terms);
    g_key_file_set_boolean(key_file, "Interface", "restore_column_configuration", config->restore_column_config);
    g_key_file_set_boolean(key_file, "Interface", "restore_sort_order", config->restore_sort_order);
    g_key_file_set_boolean(key_file, "Interface", "double_click_path", config->double_click_path);
    g_key_file_set_boolean(key_file, "Interface", "enable_list_tooltips", config->enable_list_tooltips);
    g_key_file_set_boolean(key_file, "Interface", "enable_dark_theme", config->enable_dark_theme);
    g_key_file_set_boolean(key_file, "Interface", "show_menubar", config->show_menubar);
    g_key_file_set_boolean(key_file, "Interface", "show_statusbar", config->show_statusbar);
    g_key_file_set_boolean(key_file, "Interface", "show_filter", config->show_filter);
    g_key_file_set_boolean(key_file, "Interface", "show_search_button", config->show_search_button);
    g_key_file_set_boolean(key_file, "Interface", "show_base_2_units", config->show_base_2_units);
    g_key_file_set_integer(key_file, "Interface", "action_after_file_open", config->action_after_file_open);
    g_key_file_set_boolean(key_file,
                           "Interface",
                           "action_after_file_open_keyboard",
                           config->action_after_file_open_keyboard);
    g_key_file_set_boolean(key_file, "Interface", "action_after_file_open_mouse", config->action_after_file_open_mouse);
    g_key_file_set_boolean(key_file, "Interface", "show_indexing_status", config->show_indexing_status);

    // Warning Dialogs
    g_key_file_set_boolean(key_file, "Dialogs", "show_dialog_failed_opening", config->show_dialog_failed_opening);

    // Window
    g_key_file_set_boolean(key_file, "Interface", "restore_window_size", config->restore_window_size);
    g_key_file_set_integer(key_file, "Interface", "window_width", config->window_width);
    g_key_file_set_integer(key_file, "Interface", "window_height", config->window_height);

    // Columns visibility
    g_key_file_set_boolean(key_file, "Interface", "show_listview_icons", config->show_listview_icons);
    g_key_file_set_boolean(key_file, "Interface", "show_path_column", config->show_path_column);
    g_key_file_set_boolean(key_file, "Interface", "show_type_column", config->show_type_column);
    g_key_file_set_boolean(key_file, "Interface", "show_extension_column", config->show_extension_column);
    g_key_file_set_boolean(key_file, "Interface", "show_size_column", config->show_size_column);
    g_key_file_set_boolean(key_file, "Interface", "show_modified_column", config->show_modified_column);

    g_key_file_set_boolean(key_file, "Interface", "sort_ascending", config->sort_ascending);
    if (config->sort_by) {
        g_key_file_set_string(key_file, "Interface", "sort_by", config->sort_by);
    }

    // Column width
    g_key_file_set_integer(key_file, "Interface", "name_column_width", config->name_column_width);
    g_key_file_set_integer(key_file, "Interface", "path_column_width", config->path_column_width);
    g_key_file_set_integer(key_file, "Interface", "extension_column_width", config->extension_column_width);
    g_key_file_set_integer(key_file, "Interface", "type_column_width", config->type_column_width);
    g_key_file_set_integer(key_file, "Interface", "size_column_width", config->size_column_width);
    g_key_file_set_integer(key_file, "Interface", "modified_column_width", config->modified_column_width);

    // Column position
    g_key_file_set_integer(key_file, "Interface", "name_column_pos", config->name_column_pos);
    g_key_file_set_integer(key_file, "Interface", "path_column_pos", config->path_column_pos);
    g_key_file_set_integer(key_file, "Interface", "type_column_pos", config->type_column_pos);
    g_key_file_set_integer(key_file, "Interface", "size_column_pos", config->size_column_pos);
    g_key_file_set_integer(key_file, "Interface", "modified_column_pos", config->modified_column_pos);

    // Applications
    if (config->folder_open_cmd) {
        g_key_file_set_string(key_file, "Applications", "folder_open_cmd", config->folder_open_cmd);
    }

    // Search
    g_key_file_set_boolean(key_file, "Search", "search_as_you_type", config->search_as_you_type);
    g_key_file_set_boolean(key_file, "Search", "auto_search_in_path", config->auto_search_in_path);
    g_key_file_set_boolean(key_file, "Search", "auto_match_case", config->auto_match_case);
    g_key_file_set_boolean(key_file, "Search", "search_in_path", config->search_in_path);
    g_key_file_set_boolean(key_file, "Search", "enable_regex", config->enable_regex);
    g_key_file_set_boolean(key_file, "Search", "match_case", config->match_case);
    g_key_file_set_boolean(key_file, "Search", "hide_results_on_empty_search", config->hide_results_on_empty_search);

    // Database
    g_key_file_set_boolean(key_file, "Database", "update_database_on_launch", config->update_database_on_launch);
    g_key_file_set_boolean(key_file, "Database", "update_database_every", config->update_database_every);
    g_key_file_set_integer(key_file, "Database", "update_database_every_hours", config->update_database_every_hours);
    g_key_file_set_integer(key_file,
                           "Database",
                           "update_database_every_minutes",
                           config->update_database_every_minutes);
    g_key_file_set_boolean(key_file, "Database", "exclude_hidden_files_and_folders", config->exclude_hidden_items);
    g_key_file_set_boolean(key_file, "Database", "follow_symbolic_links", config->follow_symlinks);

    config_save_indexes(key_file, config->indexes, "location");
    config_save_exclude_locations(key_file, config->exclude_locations, "exclude_location");

    if (config->exclude_files) {
        char *exclude_files_str = g_strjoinv(";", config->exclude_files);
        g_key_file_set_string(key_file, "Database", "exclude_files", exclude_files_str);
        g_clear_pointer(&exclude_files_str, free);
    }

    gchar config_path[PATH_MAX] = "";
    config_build_path(config_path, sizeof(config_path));

    const char *debug_message = NULL;
    GError *error = NULL;
    if (g_key_file_save_to_file(key_file, config_path, &error)) {
        debug_message = "[config] saved in %f ms";
        result = true;
    }
    else {
        debug_message = "[config] saving failed (%f ms)";
    }

    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);

    g_clear_pointer(&timer, g_timer_destroy);

    g_debug(debug_message, seconds * 1000);

    g_clear_pointer(&key_file, g_key_file_free);
    return result;
}

static bool
config_excludes_compare(void *e1, void *e2) {
    if (!e1 && !e2) {
        return true;
    }
    if (!e1 || !e2) {
        return false;
    }
    FsearchExcludePath *path1 = e1;
    FsearchExcludePath *path2 = e2;

    if (path1->enabled != path2->enabled) {
        return false;
    }
    if (g_strcmp0(path1->path, path2->path) != 0) {
        return false;
    }
    return true;
}

static bool
config_indexes_compare(void *i1, void *i2) {
    if (!i1 && !i2) {
        return true;
    }
    if (!i1 || !i2) {
        return false;
    }
    FsearchIndex *index1 = i1;
    FsearchIndex *index2 = i2;

    if (index1->enabled != index2->enabled) {
        return false;
    }
    if (index1->update != index2->update) {
        return false;
    }
    if (g_strcmp0(index1->path, index2->path) != 0) {
        return false;
    }
    return true;
}

static bool
config_list_compare(GList *l1, GList *l2, bool (*cmp_func)(void *, void *)) {
    if (!l1 && !l2) {
        return true;
    }
    if (!l1 || !l2) {
        return false;
    }
    uint32_t len1 = g_list_length(l1);
    uint32_t len2 = g_list_length(l2);
    if (len1 != len2) {
        return false;
    }
    for (int i = 0; i < len1; i++) {
        void *data1 = g_list_nth_data(l1, i);
        void *data2 = g_list_nth_data(l2, i);
        if (!data1 || !data2 || !cmp_func(data1, data2)) {
            return false;
        }
    }
    return true;
}

#if !GLIB_CHECK_VERSION(2, 60, 0)
// Copied from glib for backwards compatibility
static gboolean
g_strv_equal(const gchar *const *strv1, const gchar *const *strv2) {
    g_return_val_if_fail(strv1 != NULL, FALSE);
    g_return_val_if_fail(strv2 != NULL, FALSE);

    if (strv1 == strv2)
        return TRUE;

    for (; *strv1 != NULL && *strv2 != NULL; strv1++, strv2++) {
        if (!g_str_equal(*strv1, *strv2))
            return FALSE;
    }

    return (*strv1 == NULL && *strv2 == NULL);
}
#endif

FsearchConfigCompareResult
config_cmp(FsearchConfig *c1, FsearchConfig *c2) {
    FsearchConfigCompareResult result = {};

    if (c1->hide_results_on_empty_search != c2->hide_results_on_empty_search
        || c1->auto_search_in_path != c2->auto_search_in_path || c1->auto_match_case != c2->auto_match_case
        || c1->search_as_you_type != c2->search_as_you_type || c1->search_in_path != c2->search_in_path
        || c1->enable_regex != c2->enable_regex || c1->match_case != c2->match_case) {
        result.search_config_changed = true;
    }
    if (c1->highlight_search_terms != c2->highlight_search_terms || c1->show_listview_icons != c2->show_listview_icons
        || c1->single_click_open != c2->single_click_open || c1->enable_list_tooltips != c2->enable_list_tooltips) {
        result.listview_config_changed = true;
    }

    bool exclude_files_changed = false;
    if (c1->exclude_files && c2->exclude_files
        && !g_strv_equal((const gchar *const *)c1->exclude_files, (const gchar *const *)c2->exclude_files)) {
        exclude_files_changed = true;
    }
    else if ((c1->exclude_files && !c2->exclude_files) || (!c1->exclude_files && c2->exclude_files)) {
        exclude_files_changed = true;
    }

    bool indexes_changed = !config_list_compare(c1->indexes, c2->indexes, config_indexes_compare);
    bool exclude_locations_changed =
        !config_list_compare(c1->exclude_locations, c2->exclude_locations, config_excludes_compare);

    if (c1->exclude_hidden_items != c2->exclude_hidden_items || exclude_files_changed || exclude_locations_changed
        || indexes_changed) {
        result.database_config_changed = true;
    }

    return result;
}

FsearchConfig *
config_copy(FsearchConfig *config) {
    FsearchConfig *copy = calloc(1, sizeof(FsearchConfig));
    g_assert(copy != NULL);

    memcpy(copy, config, sizeof(*config));

    if (config->folder_open_cmd) {
        copy->folder_open_cmd = g_strdup(config->folder_open_cmd);
    }
    if (config->sort_by) {
        copy->sort_by = g_strdup(config->sort_by);
    }
    if (config->indexes) {
        copy->indexes = g_list_copy_deep(config->indexes, (GCopyFunc)fsearch_index_copy, NULL);
    }
    if (config->exclude_locations) {
        copy->exclude_locations =
            g_list_copy_deep(config->exclude_locations, (GCopyFunc)fsearch_exclude_path_copy, NULL);
    }
    if (config->exclude_files) {
        copy->exclude_files = g_strdupv(config->exclude_files);
    }
    return copy;
}

void
config_free(FsearchConfig *config) {
    g_assert(config != NULL);

    g_clear_pointer(&config->folder_open_cmd, free);
    g_clear_pointer(&config->sort_by, free);
    if (config->indexes) {
        g_list_free_full(g_steal_pointer(&config->indexes), (GDestroyNotify)fsearch_index_free);
    }
    if (config->exclude_locations) {
        g_list_free_full(g_steal_pointer(&config->exclude_locations), (GDestroyNotify)fsearch_exclude_path_free);
    }
    g_clear_pointer(&config->exclude_files, g_strfreev);
    g_clear_pointer(&config, free);
}

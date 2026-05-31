/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

#include "fsearch_config.h"

#include "fsearch_database_exclude.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_filter.h"
#include "fsearch_filter_manager.h"
#include "fsearch_limits.h"
#include "fsearch_query_flags.h"
#include "fsearch_string_utils.h"

#include <glib.h>
#include <glib-object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { TYPE_INT, TYPE_INT64, TYPE_STRING, TYPE_BOOL } FsearchConfigValueType;

typedef struct {
    const char *key_name;
    FsearchConfigValueType type;
    size_t struct_offset; // offset to data in config struct
    union {
        int i;
        int64_t i64;
        bool b;
        const char *s;
    } default_val;
} FsearchKeyData;

#define CONF_INT_OF(S, member, def)  { #member, TYPE_INT,    offsetof(S, member), .default_val = {.i = (def)} }
#define CONF_INT64_OF(S, member, def)  { #member, TYPE_INT64,    offsetof(S, member), .default_val = {.i64 = (def)} }
#define CONF_STR_OF(S, member, def)  { #member, TYPE_STRING, offsetof(S, member), .default_val = {.s = (def)} }
#define CONF_BOOL_OF(S, member, def) { #member, TYPE_BOOL,   offsetof(S, member), .default_val = {.b = (def)} }

#define CONF_INT(member, def)  CONF_INT_OF(FsearchConfig, member, def)
#define CONF_INT64(member, def)  CONF_INT64_OF(FsearchConfig, member, def)
#define CONF_STR(member, def)  CONF_STR_OF(FsearchConfig, member, def)
#define CONF_BOOL(member, def) CONF_BOOL_OF(FsearchConfig, member, def)

#define CONFIG_SAVE_SECTION(kf, sec, arr, cfg) \
    config_save_section(kf, sec, arr, G_N_ELEMENTS(arr), cfg)
#define CONFIG_LOAD_SECTION(kf, sec, arr, cfg) \
    config_load_section(kf, sec, arr, G_N_ELEMENTS(arr), cfg)
#define CONFIG_SAVE_OBJECT_KEYS(kf, sec, pre, idx, arr, obj) \
    config_save_object(kf, sec, pre, idx, arr, G_N_ELEMENTS(arr), obj)
#define CONFIG_LOAD_OBJECT_KEYS(kf, sec, pre, idx, arr, obj) \
    config_load_object(kf, sec, pre, idx, arr, G_N_ELEMENTS(arr), obj)
#define CONFIG_DEFAULT_SECTION(arr, cfg) \
    config_get_section_default(arr, G_N_ELEMENTS(arr), cfg)

static const FsearchKeyData SEARCH_SECTION[] = {
    CONF_BOOL(hide_results_on_empty_search, false),
    CONF_BOOL(search_in_path, false),
    CONF_BOOL(enable_regex, false),
    CONF_BOOL(match_case, false),
    CONF_BOOL(auto_search_in_path, true),
    CONF_BOOL(auto_match_case, true),
    CONF_BOOL(search_as_you_type, true),
};

static const FsearchKeyData WINDOW_SECTION[] = {
    CONF_BOOL(restore_window_size, true),
    CONF_INT(window_width, 850),
    CONF_INT(window_height, 600),
};

static const FsearchKeyData APPLICATIONS_SECTION[] = {
    CONF_STR(folder_open_cmd, NULL),
};

static const FsearchKeyData DATABASE_SECTION[] = {
    CONF_BOOL(update_database_on_launch, false),
    CONF_BOOL(update_database_every, false),
    CONF_INT(update_database_every_hours, 24),
    CONF_INT(update_database_every_minutes, 0),
};

static const FsearchKeyData INTERFACE_SECTION[] = {
    CONF_BOOL(show_base_2_units, false),
    CONF_BOOL(highlight_search_terms, true),
    CONF_BOOL(single_click_open, false),
    CONF_BOOL(launch_desktop_files, true),
    CONF_BOOL(enable_dark_theme, false),
    CONF_BOOL(enable_list_tooltips, true),
    CONF_BOOL(restore_column_config, true),
    CONF_BOOL(restore_sort_order, true),
    CONF_BOOL(double_click_path, false),
    CONF_BOOL(action_after_file_open_keyboard, false),
    CONF_BOOL(action_after_file_open_mouse, false),
    CONF_INT(action_after_file_open, ACTION_AFTER_OPEN_NOTHING),
    CONF_BOOL(exit_on_escape, false),
    CONF_BOOL(show_indexing_status, true),
    CONF_BOOL(show_menubar, true),
    CONF_BOOL(show_statusbar, true),
    CONF_BOOL(show_filter, true),
    CONF_BOOL(show_search_button, false),
    CONF_BOOL(show_listview_icons, true),
    CONF_BOOL(show_path_column, true),
    CONF_BOOL(show_type_column, false),
    CONF_BOOL(show_extension_column, true),
    CONF_BOOL(show_size_column, true),
    CONF_BOOL(show_modified_column, true),
    CONF_BOOL(sort_ascending, true),
    CONF_STR(sort_by, "Name"),
    CONF_INT(name_column_width, 250),
    CONF_INT(path_column_width, 250),
    CONF_INT(type_column_width, 100),
    CONF_INT(extension_column_width, 100),
    CONF_INT(size_column_width, 75),
    CONF_INT(modified_column_width, 75),
    CONF_INT(name_column_pos, 0),
    CONF_INT(path_column_pos, 1),
    CONF_INT(type_column_pos, 2),
    CONF_INT(size_column_pos, 3),
    CONF_INT(modified_column_pos, 4),
};

static const FsearchKeyData DIALOG_SECTION[] = {
    CONF_BOOL(show_dialog_failed_opening, true),
};

typedef struct {
    char *name;
    char *query;
    char *macro;
    bool match_case;
    bool search_in_path;
    bool enable_regex;
} FsearchConfigFilterKeys;

static const FsearchKeyData FILTER_KEYS[] = {
    CONF_STR_OF(FsearchConfigFilterKeys, name, NULL),
    CONF_STR_OF(FsearchConfigFilterKeys, query, NULL),
    CONF_STR_OF(FsearchConfigFilterKeys, macro, NULL),
    CONF_BOOL_OF(FsearchConfigFilterKeys, match_case, false),
    CONF_BOOL_OF(FsearchConfigFilterKeys, search_in_path, false),
    CONF_BOOL_OF(FsearchConfigFilterKeys, enable_regex, false),
};

typedef struct {
    int id;
    char *path;
    bool active;
    bool one_file_system;
    bool monitor;
    int64_t rescan_after;
} FsearchConfigIncludeKeys;

static const FsearchKeyData INCLUDE_KEYS[] = {
    CONF_BOOL_OF(FsearchConfigIncludeKeys, active, false),
    CONF_INT_OF(FsearchConfigIncludeKeys, id, 0),
    CONF_STR_OF(FsearchConfigIncludeKeys, path, NULL),
    CONF_BOOL_OF(FsearchConfigIncludeKeys, one_file_system, false),
    CONF_BOOL_OF(FsearchConfigIncludeKeys, monitor, false),
    CONF_INT64_OF(FsearchConfigIncludeKeys, rescan_after, 0),
};

typedef struct {
    char *type;
    char *pattern;
    char *match_scope;
    char *target;
    bool active;
} FsearchConfigExcludeKeys;

static const FsearchKeyData EXCLUDE_KEYS[] = {
    CONF_BOOL_OF(FsearchConfigExcludeKeys, active, false),
    CONF_STR_OF(FsearchConfigExcludeKeys, type, NULL),
    CONF_STR_OF(FsearchConfigExcludeKeys, pattern, NULL),
    CONF_STR_OF(FsearchConfigExcludeKeys, match_scope, NULL),
    CONF_STR_OF(FsearchConfigExcludeKeys, target, NULL),
};

static const char *config_file_name = "fsearch.conf";
static const char *config_folder_name = "fsearch";

void
config_build_dir(char *path, size_t len) {
    g_assert(path);

    const gchar *xdg_conf_dir = g_get_user_config_dir();
    snprintf(path, len, "%s/%s", xdg_conf_dir, config_folder_name);
}

static void
config_build_path(char *path, size_t len) {
    g_assert(path);

    const gchar *xdg_conf_dir = g_get_user_config_dir();
    snprintf(path, len, "%s/%s/%s", xdg_conf_dir, config_folder_name, config_file_name);
}

bool
config_make_dir(void) {
    gchar config_dir[PATH_MAX] = "";
    config_build_dir(config_dir, sizeof(config_dir));
    return !g_mkdir_with_parents(config_dir, 0700);
}

static void
config_save_key(GKeyFile *key_file,
                const char *key_section,
                const char *key_name,
                const FsearchKeyData *key_data,
                void *key_obj) {
    void *ptr = (char *)key_obj + key_data->struct_offset;
    switch (key_data->type) {
    case TYPE_INT:
        g_key_file_set_integer(key_file, key_section, key_name, *(int *)ptr);
        break;
    case TYPE_INT64:
        g_key_file_set_int64(key_file, key_section, key_name, *(int *)ptr);
        break;
    case TYPE_STRING: {
        const char *str = *(const char **)ptr;
        if (str)
            g_key_file_set_string(key_file, key_section, key_name, str);
        break;
    }
    case TYPE_BOOL:
        g_key_file_set_boolean(key_file, key_section, key_name, *(bool *)ptr);
        break;
    default:
        g_assert_not_reached();
    }
}

static void
config_load_key(GKeyFile *key_file,
                const char *key_section,
                const char *key_name,
                const FsearchKeyData *key_data,
                void *key_obj) {
    void *ptr = (char *)key_obj + key_data->struct_offset;
    g_autoptr(GError) error = NULL;
    switch (key_data->type) {
    case TYPE_INT:
        *(int *)ptr = g_key_file_get_integer(key_file, key_section, key_name, &error);
        if (error) {
            *(int *)ptr = key_data->default_val.i;
        }
        break;
    case TYPE_INT64:
        *(int *)ptr = g_key_file_get_int64(key_file, key_section, key_name, &error);
        if (error) {
            *(int *)ptr = key_data->default_val.i64;
        }
        break;
    case TYPE_STRING: {
        char **str_ptr = (char **)ptr;
        g_free(*str_ptr);
        *str_ptr = g_key_file_get_string(key_file, key_section, key_name, &error);
        if (error) {
            *str_ptr = g_strdup(key_data->default_val.s);
        }
        break;
    }
    case TYPE_BOOL:
        *(bool *)ptr = g_key_file_get_boolean(key_file, key_section, key_name, &error);
        if (error) {
            *(bool *)ptr = key_data->default_val.b;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void
config_save_object_key(GKeyFile *key_file,
                       const char *key_section,
                       const char *key_prefix,
                       uint32_t key_idx,
                       const FsearchKeyData *key_data,
                       void *key_obj) {
    g_autoptr(GString) key_name = g_string_sized_new(32);
    g_string_printf(key_name, "%s_%d_%s", key_prefix, key_idx + 1, key_data->key_name);

    config_save_key(key_file, key_section, key_name->str, key_data, key_obj);
}

static void
config_load_object_key(GKeyFile *key_file,
                       const char *key_section,
                       const char *key_prefix,
                       uint32_t key_idx,
                       const FsearchKeyData *key_data,
                       void *key_obj) {
    g_autoptr(GString) key_name = g_string_sized_new(32);
    g_string_printf(key_name, "%s_%d_%s", key_prefix, key_idx + 1, key_data->key_name);

    config_load_key(key_file, key_section, key_name->str, key_data, key_obj);
}

static void
config_save_object(GKeyFile *key_file,
                   const char *object_section,
                   const char *object_prefix,
                   uint32_t object_idx,
                   const FsearchKeyData *object_keys,
                   size_t num_object_keys,
                   void *object) {
    for (uint32_t i = 0; i < num_object_keys; i++) {
        config_save_object_key(key_file, object_section, object_prefix, object_idx, &object_keys[i], object);
    }
}

static void
config_load_object(GKeyFile *key_file,
                   const char *object_section,
                   const char *object_prefix,
                   uint32_t object_idx,
                   const FsearchKeyData *object_keys,
                   size_t num_object_keys,
                   void *object) {
    for (uint32_t i = 0; i < num_object_keys; i++) {
        config_load_object_key(key_file, object_section, object_prefix, object_idx, &object_keys[i], object);
    }
}

static void
config_save_section(GKeyFile *key_file,
                    const char *key_section,
                    const FsearchKeyData *keys,
                    size_t num_keys,
                    FsearchConfig *config) {
    for (size_t i = 0; i < num_keys; i++) {
        config_save_key(key_file, key_section, keys[i].key_name, &keys[i], config);
    }
}

static void
config_load_section(GKeyFile *key_file,
                    const char *key_section,
                    const FsearchKeyData *keys,
                    size_t num_keys,
                    FsearchConfig *config) {
    for (size_t i = 0; i < num_keys; i++) {
        config_load_key(key_file, key_section, keys[i].key_name, &keys[i], config);
    }
}

static void
config_get_section_default(const FsearchKeyData *keys,
                           size_t num_keys,
                           FsearchConfig *config) {
    for (size_t i = 0; i < num_keys; i++) {
        void *ptr = (char *)config + keys[i].struct_offset;

        switch (keys[i].type) {
        case TYPE_INT:
            *(int *)ptr = keys[i].default_val.i;
            break;
        case TYPE_INT64:
            *(int *)ptr = keys[i].default_val.i64;
            break;
        case TYPE_STRING: {
            char **str_ptr = (char **)ptr;
            if (*str_ptr) {
                g_free(*str_ptr);
            }
            *str_ptr = g_strdup(keys[i].default_val.s);
            break;
        }
        case TYPE_BOOL:
            *(bool *)ptr = keys[i].default_val.b;
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static FsearchFilterManager *
config_load_filters(GKeyFile *key_file) {
    if (!g_key_file_has_group(key_file, "Filters")) {
        return fsearch_filter_manager_new_with_defaults();
    }

    FsearchFilterManager *filters = fsearch_filter_manager_new();

    for (uint32_t i = 0; ; i++) {
        FsearchConfigFilterKeys filter_keys = {};

        CONFIG_LOAD_OBJECT_KEYS(key_file, "Filters", "filter", i, FILTER_KEYS, &filter_keys);

        if (!filter_keys.name || fsearch_string_is_empty(filter_keys.name)) {
            break;
        }

        FsearchQueryFlags flags = 0;
        if (filter_keys.match_case) {
            flags |= QUERY_FLAG_MATCH_CASE;
        }
        if (filter_keys.search_in_path) {
            flags |= QUERY_FLAG_SEARCH_IN_PATH;
        }
        if (filter_keys.enable_regex) {
            flags |= QUERY_FLAG_REGEX;
        }

        FsearchFilter *f = fsearch_filter_new(filter_keys.name, filter_keys.macro, filter_keys.query, flags);
        fsearch_filter_manager_append_filter(filters, f);
        g_clear_pointer(&f, fsearch_filter_unref);

        g_clear_pointer(&filter_keys.name, g_free);
        g_clear_pointer(&filter_keys.macro, g_free);
        g_clear_pointer(&filter_keys.query, g_free);
    }
    return filters;
}

static FsearchDatabaseIncludeManager *
config_load_includes(GKeyFile *key_file) {
    if (!g_key_file_has_group(key_file, "Database")) {
        return fsearch_database_include_manager_new_with_defaults();
    }

    FsearchDatabaseIncludeManager *include_manager = fsearch_database_include_manager_new();

    for (uint32_t i = 0; ; i++) {
        FsearchConfigIncludeKeys include_keys = {};

        CONFIG_LOAD_OBJECT_KEYS(key_file, "Database", "folder", i, INCLUDE_KEYS, &include_keys);

        if (!include_keys.path || fsearch_string_is_empty(include_keys.path)) {
            break;
        }

        g_autoptr(FsearchDatabaseInclude) include = fsearch_database_include_new(
            include_keys.path,
            include_keys.active,
            include_keys.one_file_system,
            include_keys.monitor,
            false,
            include_keys.rescan_after,
            include_keys.id);
        fsearch_database_include_manager_add(include_manager, include);

        g_clear_pointer(&include_keys.path, g_free);
    }
    return include_manager;
}

static FsearchDatabaseExcludeManager *
config_load_excludes(GKeyFile *key_file) {
    if (!g_key_file_has_group(key_file, "Database")) {
        return fsearch_database_exclude_manager_new_with_defaults();
    }

    FsearchDatabaseExcludeManager *exclude_manager = fsearch_database_exclude_manager_new();

    for (uint32_t i = 0; ; i++) {
        FsearchConfigExcludeKeys exclude_keys = {};

        CONFIG_LOAD_OBJECT_KEYS(key_file, "Database", "exclude", i, EXCLUDE_KEYS, &exclude_keys);

        if (!exclude_keys.type || fsearch_string_is_empty(exclude_keys.type)) {
            break;
        }

        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(
            exclude_keys.pattern,
            exclude_keys.active,
            fsearch_database_exclude_get_type_from_string(exclude_keys.type),
            fsearch_database_exclude_get_match_scope_from_string(exclude_keys.match_scope),
            fsearch_database_exclude_get_target_from_string(exclude_keys.target)
            );
        fsearch_database_exclude_manager_add(exclude_manager, exclude);

        g_clear_pointer(&exclude_keys.type, g_free);
        g_clear_pointer(&exclude_keys.pattern, g_free);
        g_clear_pointer(&exclude_keys.match_scope, g_free);
        g_clear_pointer(&exclude_keys.target, g_free);
    }
    return exclude_manager;
}

bool
config_load(FsearchConfig *config) {
    g_assert(config != NULL);

    bool result = false;
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_assert(key_file);

    g_autoptr(GTimer) timer = g_timer_new();

    gchar config_path[PATH_MAX] = "";
    config_build_path(config_path, sizeof(config_path));

    const char *debug_message = NULL;

    g_autoptr(GError) error = NULL;
    if (g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_NONE, &error)) {
        g_debug("[config] loading...");
        // Interface
        CONFIG_LOAD_SECTION(key_file, "Interface", INTERFACE_SECTION, config);

        // Warning Dialogs
        CONFIG_LOAD_SECTION(key_file, "Dialogs", DIALOG_SECTION, config);

        // Applications
        CONFIG_LOAD_SECTION(key_file, "Applications", APPLICATIONS_SECTION, config);

        // Window
        CONFIG_LOAD_SECTION(key_file, "Interface", WINDOW_SECTION, config);

        // Columns
        if (!config->restore_column_config) {
            config->show_listview_icons = true;
            config->show_path_column = true;
            config->show_type_column = false;
            config->show_extension_column = true;
            config->show_size_column = true;
            config->show_modified_column = true;
        }

        // Search
        CONFIG_LOAD_SECTION(key_file, "Search", SEARCH_SECTION, config);

        // Database
        CONFIG_LOAD_SECTION(key_file, "Database", DATABASE_SECTION, config);
        // Includes
        config->includes = config_load_includes(key_file);
        // Excludes
        config->excludes = config_load_excludes(key_file);

        // Filters
        config->filters = config_load_filters(key_file);

        result = true;
        debug_message = "[config] loaded in %f ms";
    }
    else {
        debug_message = "[config] loading failed (%f ms)";
    }
    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);

    g_debug(debug_message, seconds * 1000);

    return result;
}

bool
config_load_default(FsearchConfig *config) {
    g_assert(config);

    CONFIG_DEFAULT_SECTION(INTERFACE_SECTION, config);
    CONFIG_DEFAULT_SECTION(WINDOW_SECTION, config);
    CONFIG_DEFAULT_SECTION(DIALOG_SECTION, config);
    CONFIG_DEFAULT_SECTION(APPLICATIONS_SECTION, config);
    CONFIG_DEFAULT_SECTION(SEARCH_SECTION, config);
    CONFIG_DEFAULT_SECTION(DATABASE_SECTION, config);

    config->filters = fsearch_filter_manager_new_with_defaults();
    config->includes = fsearch_database_include_manager_new_with_defaults();
    config->excludes = fsearch_database_exclude_manager_new_with_defaults();

    return true;
}

static void
config_save_filters(GKeyFile *key_file, FsearchFilterManager *filters) {
    if (!filters) {
        return;
    }

    for (uint32_t i = 0; i < fsearch_filter_manager_get_num_filters(filters); ++i) {
        FsearchFilter *filter = fsearch_filter_manager_get_filter(filters, i);
        if (!filter) {
            g_assert_not_reached();
        }

        FsearchConfigFilterKeys filter_keys = {.name = filter->name, .query = filter->query, .macro = filter->macro,
                                               .match_case = filter->flags & QUERY_FLAG_MATCH_CASE ? true : false,
                                               .search_in_path = filter->flags & QUERY_FLAG_SEARCH_IN_PATH
                                                                     ? true
                                                                     : false,
                                               .enable_regex = filter->flags & QUERY_FLAG_REGEX ? true : false};

        CONFIG_SAVE_OBJECT_KEYS(key_file, "Filters", "filter", i, FILTER_KEYS, &filter_keys);

        g_clear_pointer(&filter, fsearch_filter_unref);
    }
}

static void
config_save_includes(GKeyFile *key_file, FsearchDatabaseIncludeManager *include_manager) {
    if (!include_manager) {
        return;
    }

    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    for (uint32_t i = 0; i < includes->len; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        if (!include) {
            g_assert_not_reached();
        }

        FsearchConfigIncludeKeys include_keys = {.path = (char *)fsearch_database_include_get_path(include),
                                                 .monitor = fsearch_database_include_get_monitored(include),
                                                 .active = fsearch_database_include_get_active(include),
                                                 .one_file_system = fsearch_database_include_get_one_file_system(
                                                     include),
                                                 .rescan_after = fsearch_database_include_get_rescan_after(include),
                                                 .id = fsearch_database_include_get_id(include)};

        CONFIG_SAVE_OBJECT_KEYS(key_file, "Database", "folder", i, INCLUDE_KEYS, &include_keys);
    }
}

static void
config_save_excludes(GKeyFile *key_file, FsearchDatabaseExcludeManager *exclude_manager) {
    if (!exclude_manager) {
        return;
    }
    g_autoptr(GPtrArray) excludes = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    for (uint32_t i = 0; i < excludes->len; ++i) {
        FsearchDatabaseExclude *exclude = g_ptr_array_index(excludes, i);
        if (!exclude) {
            g_assert_not_reached();
        }
        FsearchConfigExcludeKeys exclude_keys = {
            .pattern = (char *)fsearch_database_exclude_get_pattern(exclude),
            .active = fsearch_database_exclude_get_active(exclude),
            .type = (char *)fsearch_database_exclude_type_to_string(fsearch_database_exclude_get_exclude_type(exclude)),
            .match_scope = (char *)fsearch_database_exclude_match_scope_to_string(
                fsearch_database_exclude_get_match_scope(exclude)),
            .target = (char *)fsearch_database_exclude_target_to_string(fsearch_database_exclude_get_target(exclude)),
        };
        CONFIG_SAVE_OBJECT_KEYS(key_file, "Database", "exclude", i, EXCLUDE_KEYS, &exclude_keys);
    }
}

bool
config_save(FsearchConfig *config) {
    g_assert(config);

    bool result = false;
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_assert(key_file);

    g_autoptr(GTimer) timer = g_timer_new();

    g_debug("[config] saving...");

    // Interface
    CONFIG_SAVE_SECTION(key_file, "Interface", INTERFACE_SECTION, config);
    CONFIG_SAVE_SECTION(key_file, "Interface", WINDOW_SECTION, config);
    // Warning Dialogs
    CONFIG_SAVE_SECTION(key_file, "Dialogs", DIALOG_SECTION, config);

    // Applications
    CONFIG_SAVE_SECTION(key_file, "Applications", APPLICATIONS_SECTION, config);

    // Search
    CONFIG_SAVE_SECTION(key_file, "Search", SEARCH_SECTION, config);

    // Database
    CONFIG_SAVE_SECTION(key_file, "Database", DATABASE_SECTION, config);

    // Filters
    config_save_filters(key_file, config->filters);

    // Includes
    config_save_includes(key_file, config->includes);

    // Excludes
    config_save_excludes(key_file, config->excludes);

    gchar config_path[PATH_MAX] = "";
    config_build_path(config_path, sizeof(config_path));

    const char *debug_message = NULL;
    g_autoptr(GError) error = NULL;
    if (g_key_file_save_to_file(key_file, config_path, &error)) {
        debug_message = "[config] saved in %f ms";
        result = true;
    }
    else {
        debug_message = "[config] saving failed (%f ms)";
    }

    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);

    g_debug(debug_message, seconds * 1000);

    return result;
}

FsearchConfigCompareResult
config_cmp(FsearchConfig *c1, FsearchConfig *c2) {
    FsearchConfigCompareResult result = {};

    if (c1->hide_results_on_empty_search != c2->hide_results_on_empty_search
        || c1->auto_search_in_path != c2->auto_search_in_path || c1->auto_match_case != c2->auto_match_case
        || c1->search_as_you_type != c2->search_as_you_type || c1->search_in_path != c2->search_in_path
        || c1->enable_regex != c2->enable_regex || c1->match_case != c2->match_case) {
        result.search_config_changed = true;
    }
    if (!fsearch_filter_manager_cmp(c1->filters, c2->filters)) {
        result.search_config_changed = true;
    }
    if (c1->highlight_search_terms != c2->highlight_search_terms || c1->show_listview_icons != c2->
        show_listview_icons
        || c1->single_click_open != c2->single_click_open || c1->enable_list_tooltips != c2->
        enable_list_tooltips) {
        result.listview_config_changed = true;
    }

    const bool includes_changed = !fsearch_database_include_manager_equal(c1->includes, c2->includes);
    const bool excludes_changed = !fsearch_database_exclude_manager_equal(c1->excludes, c2->excludes);

    if (c1->exclude_hidden_items != c2->exclude_hidden_items || excludes_changed || includes_changed) {
        result.database_config_changed = true;
    }

    return result;
}

FsearchConfig *
config_copy(FsearchConfig *config) {
    FsearchConfig *copy = calloc(1, sizeof(FsearchConfig));
    g_assert(copy);

    memcpy(copy, config, sizeof(*config));

    if (config->folder_open_cmd) {
        copy->folder_open_cmd = g_strdup(config->folder_open_cmd);
    }
    if (config->sort_by) {
        copy->sort_by = g_strdup(config->sort_by);
    }
    if (config->includes) {
        copy->includes = fsearch_database_include_manager_copy(config->includes);
    }
    if (config->excludes) {
        copy->excludes = fsearch_database_exclude_manager_copy(config->excludes);
    }
    if (config->filters) {
        copy->filters = fsearch_filter_manager_copy(config->filters);
    }
    return copy;
}

void
config_free(FsearchConfig *config) {
    g_assert(config);

    g_clear_pointer(&config->folder_open_cmd, g_free);
    g_clear_pointer(&config->sort_by, g_free);
    g_clear_pointer(&config->filters, fsearch_filter_manager_unref);
    g_clear_object(&config->includes);
    g_clear_object(&config->excludes);
    g_clear_pointer(&config, g_free);
}
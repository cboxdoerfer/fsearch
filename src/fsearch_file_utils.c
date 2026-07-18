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

#define G_LOG_DOMAIN "fsearch-utils"

#include "fsearch_file_utils.h"
#include "fsearch_string_utils.h"

#ifndef __MACH__
#include <gio/gdesktopappinfo.h>
#endif

#include <ctype.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

const char *data_folder_name = "fsearch";

#if !defined(__MACH__)
static void
launch_uris_ready(GObject *source_object, GAsyncResult *result, gpointer user_data);
#endif

static void
add_error_message_with_format(GString *error_messages, const char *description, const char *item_name, const char *reason) {
    if (!error_messages || !description || !item_name || !reason) {
        return;
    }
    g_autoptr(GString) error_message = g_string_new(NULL);
    g_string_printf(error_message, "%s \"%s\": %s", description, item_name, reason);
    g_string_append(error_messages, error_message->str);
    g_string_append_c(error_messages, '\n');
}

static void
add_error_message(GString *error_messages, const char *error_message) {
    if (!error_messages || !error_message) {
        return;
    }
    g_string_append(error_messages, error_message);
    g_string_append_c(error_messages, '\n');
}

gchar *
fsearch_file_utils_get_app_user_state_dir() {
#if GLIB_CHECK_VERSION(2, 72, 0)
    const gchar *state_dir = g_get_user_state_dir();
    return g_build_filename(state_dir, data_folder_name, NULL);

#else
    const gchar *xdg_state_home = g_getenv("XDG_STATE_HOME");

    if (xdg_state_home != NULL && xdg_state_home[0] != '\0') {
        return g_build_filename(xdg_state_home, data_folder_name, NULL);
    }
    else {
        /* Default to ~/.local/state as per the spec */
        const gchar *home_dir = g_get_home_dir();
        return g_build_filename(home_dir, ".local", "state", data_folder_name, NULL);
    }
#endif
}

void
fsearch_file_utils_init_data_dir_path(char *path, size_t len) {
    g_assert(path);

    const gchar *xdg_data_dir = g_get_user_data_dir();
    snprintf(path, len, "%s/%s", xdg_data_dir, data_folder_name);
}

bool
fsearch_file_utils_create_dir(const char *path) {
    return !g_mkdir_with_parents(path, 0700);
}

bool
fsearch_file_utils_is_desktop_file(const char *path) {
    const char *uri_extension = fsearch_string_get_extension(path);
    if (uri_extension && !strcmp(uri_extension, "desktop")) {
        return true;
    }
    return false;
}

static gboolean
keyword_eval_cb(const GMatchInfo *info, GString *res, gpointer data) {
    g_autofree gchar *match = g_match_info_fetch(info, 0);
    if (!match) {
        return FALSE;
    }
    gchar *r = g_hash_table_lookup((GHashTable *)data, match);
    if (r) {
        g_string_append(res, r);
    }

    return FALSE;
}

static char *
build_folder_open_cmd(const char *path, const char *path_full, const char *cmd) {
    if (!path || !path_full) {
        return NULL;
    }
    g_autofree char *path_quoted = g_shell_quote(path);
    g_autofree char *path_full_quoted = g_shell_quote(path_full);

    // The following code is mostly based on the example code found here:
    // https://developer.gnome.org/glib/stable/glib-Perl-compatible-regular-expressions.html#g-regex-replace-eval
    //
    // Create hash table which hold all valid keywords as keys
    // and their replacements as values
    // All valid keywords are:
    // - {path_raw}
    //     The raw path of a file or folder. E.g. the path of /foo/bar is /foo
    // - {path_full_raw}
    //     The raw full path of a file or folder. E.g. the full path of /foo/bar
    //     is /foo/bar
    // - {path} and {path_full}
    //     Those are the same as {path_raw} and {path_full_raw} but they get
    //     properly escaped and quoted for the usage in shells. E.g. /foo/'bar
    //     becomes '/foo/'\''bar'

    g_autoptr(GHashTable) keywords = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(keywords, "{path_raw}", (char *)path);
    g_hash_table_insert(keywords, "{path_full_raw}", (char *)path_full);
    g_hash_table_insert(keywords, "{path}", path_quoted);
    g_hash_table_insert(keywords, "{path_full}", path_full_quoted);

    // Regular expression which matches multiple words (and underscores)
    // surrounded with {}
    g_autoptr(GRegex) reg = g_regex_new("{[\\w]+}", 0, 0, NULL);
    // Replace all the matched keywords
    return g_regex_replace_eval(reg, cmd, -1, 0, 0, keyword_eval_cb, keywords, NULL);
}

static bool
file_remove_or_trash(const char *path, bool delete, GString *error_messages) {
    g_autoptr(GFile) file = g_file_new_for_path(path);
    if (!file) {
        add_error_message_with_format(error_messages,
                                      C_("Will be followed by the path of the file.", "Error when removing file"),
                                      path,
                                      _("Failed to get path"));
        return false;
    }
    g_autoptr(GError) error = NULL;
    if (delete) {
        g_file_delete(file, NULL, &error);
    }
    else {
        g_file_trash(file, NULL, &error);
    }

    if (error) {
        add_error_message(error_messages, error->message);
        return false;
    }
    return true;
}

bool
fsearch_file_utils_remove(const char *path, GString *error_messages) {
    return file_remove_or_trash(path, true, error_messages);
}

bool
fsearch_file_utils_trash(const char *path, GString *error_messages) {
    return file_remove_or_trash(path, false, error_messages);
}

// Structure to store files (`uris`) which should be opened with the application described by `app_info`
typedef struct {
    GAppInfo *app_info;
    GList *uris;
} FsearchFileUtilsLaunchUrisContext;

typedef struct {
    GQueue *launch_uris_ctx_queue;
    GQueue *path_queue;
    GAppLaunchContext *app_launch_context;
    GString *error_messages;
    bool launch_desktop_files;

    // Groups files by resolved app (keyed by app id)
    GHashTable *app_groups;
    guint pending_resolves;

    FsearchFileUtilsOpenCallback callback;
    gpointer callback_data;
} FsearchFileUtilsLaunchContext;

static void
launch_uris_context_free(FsearchFileUtilsLaunchUrisContext *ctx) {
    if (!ctx) {
        return;
    }
    g_clear_object(&ctx->app_info);
    g_list_free_full(g_steal_pointer(&ctx->uris), g_free);
    g_clear_pointer(&ctx, g_free);
}

static FsearchFileUtilsLaunchContext *
launch_context_new(GAppLaunchContext *app_launch_context,
                   bool launch_desktop_files,
                   FsearchFileUtilsOpenCallback callback,
                   gpointer callback_data) {
    g_return_val_if_fail(app_launch_context, NULL);
    g_return_val_if_fail(callback, NULL);

    FsearchFileUtilsLaunchContext *launch_ctx = g_new0(FsearchFileUtilsLaunchContext, 1);
    launch_ctx->app_launch_context = g_object_ref(app_launch_context);
    launch_ctx->launch_desktop_files = launch_desktop_files;
    launch_ctx->launch_uris_ctx_queue = g_queue_new();
    launch_ctx->path_queue = g_queue_new();
    launch_ctx->app_groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    launch_ctx->error_messages = g_string_new(NULL);
    launch_ctx->callback = callback;
    launch_ctx->callback_data = callback_data;
    return launch_ctx;
}

static void
launch_context_free(FsearchFileUtilsLaunchContext *ctx) {
    if (!ctx) {
        return;
    }

    g_string_free(g_steal_pointer(&ctx->error_messages), TRUE);
    g_queue_free_full(g_steal_pointer(&ctx->launch_uris_ctx_queue), (GDestroyNotify)launch_uris_context_free);
    g_queue_free_full(g_steal_pointer(&ctx->path_queue), (GDestroyNotify)g_free);
    g_clear_pointer(&ctx->app_groups, g_hash_table_destroy);
    g_clear_object(&ctx->app_launch_context);
    g_clear_pointer(&ctx, g_free);
}

// Adds a desktop file that should be launched as its associated application (no URIs passed),
// rather than opened with a handler derived from its content type.
static void
add_desktop_launch_for_uri_to_launch_context(FsearchFileUtilsLaunchContext *launch_ctx, const char *path) {
#ifdef __MACH__
    GAppInfo *app_info = g_app_info_create_from_commandline("/usr/bin/open", NULL, G_APP_INFO_CREATE_NONE, NULL);
#else
    GAppInfo *app_info = G_APP_INFO(g_desktop_app_info_new_from_filename(path));
#endif
    if (!app_info) {
        add_error_message_with_format(launch_ctx->error_messages,
                                      C_("Will be followed by the file path.", "Error when getting information from file"),
                                      path,
                                      _("Failed to get application information"));
        return;
    }
    FsearchFileUtilsLaunchUrisContext *uris_ctx = g_new0(FsearchFileUtilsLaunchUrisContext, 1);
    uris_ctx->app_info = app_info;
    g_queue_push_tail(launch_ctx->launch_uris_ctx_queue, uris_ctx);
}

static void
add_app_info_for_uri_to_launch_context(FsearchFileUtilsLaunchContext *launch_ctx, GAppInfo *app_info, char *uri) {
    const char *id = g_app_info_get_id(app_info);
    FsearchFileUtilsLaunchUrisContext *uris_ctx = id ? g_hash_table_lookup(launch_ctx->app_groups, id) : NULL;
    if (uris_ctx) {
        uris_ctx->uris = g_list_append(uris_ctx->uris, uri);
        g_object_unref(app_info);
        return;
    }

    uris_ctx = g_new0(FsearchFileUtilsLaunchUrisContext, 1);
    uris_ctx->app_info = app_info;
    uris_ctx->uris = g_list_append(NULL, uri);
    if (id) {
        g_hash_table_insert(launch_ctx->app_groups, g_strdup(id), uris_ctx);
    }
    else {
        // An application without a desktop id can't be grouped, launch it on its own.
        g_queue_push_tail(launch_ctx->launch_uris_ctx_queue, uris_ctx);
    }
}

static void
handle_callback(FsearchFileUtilsOpenCallback callback, gpointer callback_data, GString *error_message) {
    if (error_message->len == 0) {
        callback(TRUE, NULL, callback_data);
    }
    else {
        callback(FALSE, error_message->str, callback_data);
    }
}

static void
handle_queued_uris(FsearchFileUtilsLaunchContext *launch_ctx) {
    if (g_queue_is_empty(launch_ctx->launch_uris_ctx_queue)) {
        // All files were handled, either successfully or with errors
        handle_callback(launch_ctx->callback, launch_ctx->callback_data, launch_ctx->error_messages);
        g_clear_pointer(&launch_ctx, launch_context_free);
        return;
    }

    FsearchFileUtilsLaunchUrisContext *uris_ctx = g_queue_pop_head(launch_ctx->launch_uris_ctx_queue);
#if !defined(__MACH__)
    g_app_info_launch_uris_async(uris_ctx->app_info,
                                 uris_ctx->uris,
                                 launch_ctx->app_launch_context,
                                 NULL,
                                 launch_uris_ready,
                                 launch_ctx);
    g_clear_pointer(&uris_ctx, launch_uris_context_free);
#else
    g_autoptr(GError) error = NULL;
    g_app_info_launch_uris(uris_ctx->app_info, uris_ctx->uris, launch_ctx->app_launch_context, &error);
    if (error) {
        add_error_message(launch_ctx->error_messages, error->message);
    }
    g_clear_pointer(&uris_ctx, launch_uris_context_free);
    handle_queued_uris(launch_ctx);
#endif
}

#if !defined(__MACH__)
static void
launch_uris_ready(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    FsearchFileUtilsLaunchContext *ctx = user_data;
    g_autoptr(GError) error = NULL;
    g_app_info_launch_uris_finish(G_APP_INFO(source_object), result, &error);

    if (error) {
        add_error_message(ctx->error_messages, error->message);
    }

    handle_queued_uris(ctx);
}
#endif

static void
move_group_to_queue(gpointer key, gpointer value, gpointer user_data) {
    FsearchFileUtilsLaunchContext *launch_ctx = user_data;
    g_queue_push_tail(launch_ctx->launch_uris_ctx_queue, value);
}

// Once every file's handler has been resolved we move the groups into the launch queue and
// launch them
static void
launch_collected_apps_with_uris(FsearchFileUtilsLaunchContext *launch_ctx) {
    g_hash_table_foreach(launch_ctx->app_groups, move_group_to_queue, launch_ctx);
    g_hash_table_destroy(g_steal_pointer(&launch_ctx->app_groups));
    handle_queued_uris(launch_ctx);
}

#if !defined(__MACH__)
static void
resolve_default_handler_ready(GObject *source, GAsyncResult *result, gpointer user_data) {
    FsearchFileUtilsLaunchContext *launch_ctx = user_data;
    GFile *file = G_FILE(source);

    g_autoptr(GError) error = NULL;
    g_autoptr(GAppInfo) app_info = g_file_query_default_handler_finish(file, result, &error);
    if (app_info) {
        add_app_info_for_uri_to_launch_context(launch_ctx, g_steal_pointer(&app_info), g_file_get_uri(file));
    }
    else {
        g_autofree char *path = g_file_get_path(file);
        add_error_message_with_format(launch_ctx->error_messages,
                                      C_("Will be followed by the file path.", "Error when opening file"),
                                      path ? path : "",
                                      error ? error->message : _("No default application registered"));
    }
    launch_ctx->pending_resolves -= 1;

    if (launch_ctx->pending_resolves == 0) {
        launch_collected_apps_with_uris(launch_ctx);
    }
}
#endif

static bool
app_is_sandboxed(void) {
    static bool is_sandboxed = false;
    static gsize initialization_value = 0;

    if (g_once_init_enter(&initialization_value)) {
        g_auto(GStrv) env = g_get_environ();
        if (g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS)) {
            is_sandboxed = true;
        }
        else if (g_environ_getenv(env, "SNAP")) {
            is_sandboxed = true;
        }

        g_once_init_leave(&initialization_value, 1);
    }
    return is_sandboxed;
}

static void
launch_default_for_uri_ready(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    FsearchFileUtilsLaunchContext *launch_context = user_data;

    g_autoptr(GError) error = NULL;

    g_app_info_launch_default_for_uri_finish(result, &error);
    if (error) {
        add_error_message(launch_context->error_messages, error->message);
    }

    if (g_queue_is_empty(launch_context->path_queue)) {
        // All files have handled, either successfully or with errors
        handle_callback(launch_context->callback, launch_context->callback_data, launch_context->error_messages);
        g_clear_pointer(&launch_context, launch_context_free);
    }
    else {
        g_autofree char *uri = g_queue_pop_head(launch_context->path_queue);
        g_app_info_launch_default_for_uri_async(uri,
                                                launch_context->app_launch_context,
                                                NULL,
                                                launch_default_for_uri_ready,
                                                launch_context);
    }
}

static void
launch_default_for_path(GList *paths,
                        GAppLaunchContext *launch_context,
                        FsearchFileUtilsOpenCallback callback,
                        gpointer callback_data) {
    FsearchFileUtilsLaunchContext *open_default_ctx = launch_context_new(launch_context, false, callback, callback_data);

    for (GList *p = paths; p != NULL; p = p->next) {
        g_autoptr(GFile) file = g_file_new_for_path(p->data);
        char *uri = g_file_get_uri(file);
        if (uri) {
            g_queue_push_tail(open_default_ctx->path_queue, uri);
        }
    }

    g_autofree char *uri = g_queue_pop_head(open_default_ctx->path_queue);
    g_app_info_launch_default_for_uri_async(uri,
                                            launch_context,
                                            NULL,
                                            launch_default_for_uri_ready,
                                            g_steal_pointer(&open_default_ctx));
}

void
fsearch_file_utils_open_path_list(GList *paths,
                                  bool launch_desktop_files,
                                  GAppLaunchContext *app_launch_context,
                                  FsearchFileUtilsOpenCallback callback,
                                  gpointer callback_data) {
    g_return_if_fail(paths);

    if (app_is_sandboxed()) {
        g_debug("[open_path_list] FSearch is sandboxed. Ask the system to open the files for us.");
        return launch_default_for_path(paths, app_launch_context, callback, callback_data);
    }

    FsearchFileUtilsLaunchContext *launch_ctx = launch_context_new(app_launch_context,
                                                                   launch_desktop_files,
                                                                   callback,
                                                                   callback_data);

#if !defined(__MACH__)
    // Track how many async default handlers resolves are started
    launch_ctx->pending_resolves = 0;
    for (GList *p = paths; p != NULL; p = p->next) {
        const char *path = p->data;
        if (launch_desktop_files && fsearch_file_utils_is_desktop_file(path)) {
            add_desktop_launch_for_uri_to_launch_context(launch_ctx, path);
            continue;
        }
        // Let GIO figure out the default handler for a file
        launch_ctx->pending_resolves++;
        g_autoptr(GFile) file = g_file_new_for_path(path);
        g_file_query_default_handler_async(file, G_PRIORITY_DEFAULT, NULL, resolve_default_handler_ready, launch_ctx);
    }
    if (launch_ctx->pending_resolves == 0) {
        launch_collected_apps_with_uris(launch_ctx);
    }
#else
    for (GList *p = paths; p != NULL; p = p->next) {
        const char *path = p->data;
        if (launch_desktop_files && fsearch_file_utils_is_desktop_file(path)) {
            add_desktop_launch_for_uri_to_launch_context(launch_ctx, path);
            continue;
        }
        g_autoptr(GFile) file = g_file_new_for_path(path);
        g_autoptr(GError) error = NULL;
        g_autoptr(GAppInfo) app_info = g_file_query_default_handler(file, NULL, &error);
        if (app_info) {
            add_app_info_for_uri_to_launch_context(launch_ctx, g_steal_pointer(&app_info), g_file_get_uri(file));
        }
        else {
            g_autofree char *epath = g_file_get_path(file);
            add_error_message_with_format(launch_ctx->error_messages,
                                          C_("Will be followed by the file path.", "Error when opening file"),
                                          epath ? epath : "",
                                          error ? error->message : _("No default application registered"));
        }
    }
    launch_collected_apps_with_uris(launch_ctx);
#endif
}

static bool
fsearch_file_utils_open_path_with_command(const char *path, const char *cmd, GString *error_message) {
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFile) parent = g_file_get_parent(file);
    // If file has no parent it means it's the root directory.
    // We still want to open a folder, so we consider the root directory to be its own parent
    // and open it, instead of doing nothing or failing.
    g_autofree char *parent_path = g_file_get_path(parent ? parent : file);

    if (!parent_path) {
        add_error_message_with_format(error_message,
                                      C_("Will be followed by the path of the folder.",
                                         "Error when opening parent folder"),
                                      path,
                                      _("Failed to get parent path"));
        return false;
    }

    const char *error_description = C_("Will be followed by the path of the folder.", "Error while opening folder");
    g_autofree char *cmd_res = build_folder_open_cmd(path, path, cmd);
    if (!cmd_res) {
        add_error_message_with_format(error_message, error_description, path, _("Failed to build open command"));
        return false;
    }

    g_autoptr(GError) error = NULL;
    if (!g_spawn_command_line_async(cmd_res, &error)) {
        add_error_message(error_message, error->message);
        return false;
    }

    return true;
}

bool
fsearch_file_utils_open_path_list_with_command(GList *paths, const char *cmd, GString *error_message) {
    g_return_val_if_fail(cmd, false);
    g_return_val_if_fail(paths, false);

    for (GList *p = paths; p != NULL; p = p->next) {
        const char *path = p->data;
        fsearch_file_utils_open_path_with_command(path, cmd, error_message);
    }
    return true;
}

static gchar *
get_content_type_description(const gchar *name) {
    if (!name) {
        return NULL;
    }
    g_autofree gchar *content_type = g_content_type_guess(name, NULL, 0, NULL);
    if (!content_type) {
        return NULL;
    }
    return g_content_type_get_description(content_type);
}

gchar *
fsearch_file_utils_get_file_type_non_localized(const char *name, gboolean is_dir) {
    gchar *type = NULL;
    if (is_dir) {
        type = g_strdup("Folder");
    }
    else {
        type = get_content_type_description(name);
    }
    if (type == NULL) {
        type = g_strdup("Unknown Type");
    }
    return type;
}

gchar *
fsearch_file_utils_get_file_type(const char *name, gboolean is_dir) {
    gchar *type = NULL;
    if (is_dir) {
        type = g_strdup(_("Folder"));
    }
    else {
        type = get_content_type_description(name);
    }
    if (type == NULL) {
        type = g_strdup(_("Unknown Type"));
    }
    return type;
}

#define DEFAULT_FILE_ICON_NAME "application-octet-stream"

GIcon *
fsearch_file_utils_get_desktop_file_icon(const char *path) {
#ifdef __MACH__
    g_autoptr(GAppInfo) info = NULL;
#else
    g_autoptr(GAppInfo) info = (GAppInfo *)g_desktop_app_info_new_from_filename(path);
#endif

    if (!info) {
        goto default_icon;
    }

    GIcon *icon = g_app_info_get_icon(info);
    if (!icon) {
        goto default_icon;
    }

    return g_object_ref(icon);

default_icon:
    return g_themed_icon_new("application-x-executable");
}

GIcon *
fsearch_file_utils_guess_icon(const char *name, const char *path, bool is_dir) {
    if (is_dir) {
        return g_themed_icon_new("folder");
    }

    if (fsearch_file_utils_is_desktop_file(name)) {
        return fsearch_file_utils_get_desktop_file_icon(path);
    }

    GIcon *icon = fsearch_file_utils_get_thumbnail_icon(path);
    if (icon) {
        return icon;
    }

    g_autofree gchar *content_type = g_content_type_guess(name, NULL, 0, NULL);
    if (!content_type) {
        return g_themed_icon_new(DEFAULT_FILE_ICON_NAME);
    }

    icon = g_content_type_get_icon(content_type);

    return icon ? icon : g_themed_icon_new(DEFAULT_FILE_ICON_NAME);
}

GIcon *
fsearch_file_utils_get_icon_for_path(const char *path) {
    g_autoptr(GFile) g_file = g_file_new_for_path(path);
    if (!g_file) {
        return g_themed_icon_new("edit-delete");
    }

    g_autoptr(GFileInfo) file_info = g_file_query_info(g_file, "standard::icon", 0, NULL, NULL);
    if (!file_info) {
        return g_themed_icon_new("edit-delete");
    }

    GIcon *icon = g_file_info_get_icon(file_info);
    return g_object_ref(icon);
}

char *
fsearch_file_utils_get_size_formatted(off_t size, bool show_base_2_units) {
    if (show_base_2_units) {
        return g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
    }
    else {
        return g_format_size_full(size, G_FORMAT_SIZE_DEFAULT);
    }
}

char *
fsearch_file_utils_get_content_type(const char *path, GError **error) {
    g_assert(path);
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFileInfo) info = g_file_query_info(file,
                                                  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                                  G_FILE_QUERY_INFO_NONE,
                                                  NULL,
                                                  error);

    // Check if info has content type attribute before calling getter to avoid glib internal warning
    if (info && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)) {
        const char *content_type = g_file_info_get_content_type(info);
        if (content_type) {
            return g_strdup(content_type);
        }
    }

    // Querying content type failed, try guessing
    return g_content_type_guess(path, NULL, 0, NULL);
}

GIcon *
fsearch_file_utils_get_thumbnail_icon(const char *path) {
    g_autoptr(GFile) g_file = g_file_new_for_path(path);
    if (!g_file) {
        return NULL;
    }

    g_autoptr(GFileInfo) file_info = g_file_query_info(g_file, "thumbnail::path", 0, NULL, NULL);
    if (!file_info) {
        return NULL;
    }

    const char *thumbnail = g_file_info_get_attribute_byte_string(file_info, "thumbnail::path");
    if (!thumbnail) {
        return NULL;
    }

    return g_icon_new_for_string(thumbnail, NULL);
}

bool
fsearch_file_utils_get_info(const char *path, time_t *mtime, off_t *size, bool *is_dir) {
    g_return_val_if_fail(path, false);

    struct stat st;
    if (lstat(path, &st)) {
        g_debug("[get_info] can't stat: %s", path);
        return false;
    }
    if (mtime) {
        *mtime = st.st_mtime;
    }
    if (size) {
        *size = st.st_size;
    }
    if (is_dir) {
        *is_dir = S_ISDIR(st.st_mode) ? true : false;
    }
    return true;
}

// Based on strverscmp from GNU glibc, with slight modification to make sure
// full paths are sorted properly.
//

// Make sure path seperators sort before any other characters
static inline int
path_char_weight(unsigned char c) {
    if (c == '\0') {
        return 0; // Null terminator is always the absolute lowest
    }
    if (c == G_DIR_SEPARATOR) {
        return 1; // Path separator is strictly greater than \0, but less than everything else
    }
    // Shift all other characters up by 1 to prevent collisions with G_DIR_SEPARATOR
    return c + 1;
}

/* Compare strings while treating digits characters numerically.
   Copyright (C) 1997-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Jean-François Bignolles <bignolle@ecoledoc.ibp.fr>, 1997.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/* states: S_N: normal, S_I: comparing integral part, S_F: comparing
           fractionnal parts, S_Z: idem but with leading Zeroes only */
#define S_N 0x0
#define S_I 0x3
#define S_F 0x6
#define S_Z 0x9

/* result_type: CMP: return diff; LEN: compare using len_diff/diff */
#define CMP 2
#define LEN 3

/* Compare S1 and S2 as strings holding indices/version numbers,
   returning less than, equal to or greater than zero if S1 is less than,
   equal to or greater than S2 (for more info, see the Glibc texinfo doc).  */

int
fsearch_file_utils_cmp_paths(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    /* Symbol(s)    0       [1-9]   others
       Transition   (10) 0  (01) d  (00) x   */
    static const uint8_t next_state[] = {/* state    x    d    0  */
                                         /* S_N */ S_N,
                                         S_I,
                                         S_Z,
                                         /* S_I */ S_N,
                                         S_I,
                                         S_I,
                                         /* S_F */ S_N,
                                         S_F,
                                         S_F,
                                         /* S_Z */ S_N,
                                         S_F,
                                         S_Z};

    static const int8_t result_type[] = {/* state   x/x  x/d  x/0  d/x  d/d  d/0  0/x  0/d  0/0  */

                                         /* S_N */ CMP, CMP, CMP, CMP, LEN, CMP, CMP, CMP, CMP,
                                         /* S_I */ CMP, -1,  -1,  +1,  LEN, LEN, +1,  LEN, LEN,
                                         /* S_F */ CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
                                         /* S_Z */ CMP, +1,  +1,  -1,  CMP, CMP, -1,  CMP, CMP};

    if (p1 == p2)
        return 0;

    unsigned char c1 = *p1++;
    unsigned char c2 = *p2++;
    /* Hint: '0' is a digit too.  */
    int state = S_N + ((c1 == '0') + (isdigit(c1) != 0));

    int diff;
    while ((diff = path_char_weight(c1) - path_char_weight(c2)) == 0) {
        if (c1 == '\0')
            return diff;

        state = next_state[state];
        c1 = *p1++;
        c2 = *p2++;
        state += (c1 == '0') + (isdigit(c1) != 0);
    }

    state = result_type[state * 3 + (((c2 == '0') + (isdigit(c2) != 0)))];

    switch (state) {
    case CMP:
        return diff;

    case LEN:
        while (isdigit(*p1++))
            if (!isdigit(*p2++))
                return 1;

        return isdigit(*p2) ? -1 : diff;

    default:
        return state;
    }
}
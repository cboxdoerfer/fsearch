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

#define _GNU_SOURCE

#include "utils.h"
#include "debug.h"
#include "fsearch_limits.h"
#include "fsearch_list_view.h"
#include "ui_utils.h"
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <stdbool.h>
#include <stdio.h>

const char *data_folder_name = "fsearch";

void
init_data_dir_path(char *path, size_t len) {
    g_assert(path != NULL);
    g_assert(len >= 0);

    const gchar *xdg_data_dir = g_get_user_data_dir();
    snprintf(path, len, "%s/%s", xdg_data_dir, data_folder_name);
    return;
}

bool
create_dir(const char *path) {
    return !g_mkdir_with_parents(path, 0700);
}

static gboolean
keyword_eval_cb(const GMatchInfo *info, GString *res, gpointer data) {
    gchar *match = g_match_info_fetch(info, 0);
    if (!match) {
        return FALSE;
    }
    gchar *r = g_hash_table_lookup((GHashTable *)data, match);
    if (r) {
        g_string_append(res, r);
    }
    g_free(match);

    return FALSE;
}

static char *
build_folder_open_cmd(BTreeNode *node, const char *cmd) {
    if (!cmd || !node) {
        return NULL;
    }

    char path[PATH_MAX] = "";
    if (!btree_node_init_path(node, path, sizeof(path))) {
        return NULL;
    }
    char path_full[PATH_MAX] = "";
    if (!btree_node_init_parent_path(node, path_full, sizeof(path_full))) {
        return NULL;
    }
    char *path_quoted = g_shell_quote(path);
    char *path_full_quoted = g_shell_quote(path_full);

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
    // - {path_quoted} and {path_full_quoted}
    //     Those are the same as {path_raw} and {path_full_raw} but they get
    //     properly escaped and quoted for the usage in shells. E.g. /foo/'bar
    //     becomes '/foo/'\''bar'

    GHashTable *keywords = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(keywords, "{path_raw}", path);
    g_hash_table_insert(keywords, "{path_full_raw}", path_full);
    g_hash_table_insert(keywords, "{path}", path_quoted);
    g_hash_table_insert(keywords, "{path_full}", path_full_quoted);

    // Regular expression which matches multiple words (and underscores)
    // surrouned with {}
    GRegex *reg = g_regex_new("{[\\w]+}", 0, 0, NULL);
    // Replace all the matched keywords
    char *cmd_res = g_regex_replace_eval(reg, cmd, -1, 0, 0, keyword_eval_cb, keywords, NULL);

    g_regex_unref(reg);
    g_hash_table_destroy(keywords);
    g_free(path_quoted);
    g_free(path_full_quoted);

    return cmd_res;
}

static bool
open_with_cmd(BTreeNode *node, const char *cmd) {
    if (!cmd) {
        return false;
    }

    char *cmd_res = build_folder_open_cmd(node, cmd);
    if (!cmd_res) {
        return false;
    }

    bool result = true;
    GError *error = NULL;
    if (!g_spawn_command_line_async(cmd_res, &error)) {

        fprintf(stderr, "open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog_async(NULL,
                                      GTK_MESSAGE_ERROR,
                                      GTK_BUTTONS_OK,
                                      "Error while opening file:",
                                      error->message,
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
        g_error_free(error);
        result = false;
    }

    g_free(cmd_res);
    cmd_res = NULL;

    return result;
}

static bool
open_uri(const char *uri) {
    if (!uri) {
        return false;
    }

    if (!g_file_test(uri, G_FILE_TEST_EXISTS)) {
        return false;
    }

    GError *error = NULL;
    const char *argv[3];
    argv[0] = "xdg-open";
    argv[1] = uri;
    argv[2] = NULL;

    if (!g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {

        fprintf(stderr, "xdg-open: error: %s\n", error->message);
        ui_utils_run_gtk_dialog_async(NULL,
                                      GTK_MESSAGE_ERROR,
                                      GTK_BUTTONS_OK,
                                      "Error while opening file:",
                                      error->message,
                                      G_CALLBACK(gtk_widget_destroy),
                                      NULL);
        g_error_free(error);

        return false;
    }
    return true;
}

static bool
file_remove_or_trash(const char *path, bool delete) {
    GFile *file = g_file_new_for_path(path);
    if (!file) {
        return false;
    }
    bool success = false;
    if (delete) {
        success = g_file_delete(file, NULL, NULL);
    }
    else {
        success = g_file_trash(file, NULL, NULL);
    }
    g_object_unref(file);

    if (success) {
        if (delete) {
            trace("[file_remove] deleted file: %s\n", path);
        }
        else {
            trace("[file_remove] moved file to trash: %s\n", path);
        }
    }
    else {
        trace("[file_remove] failed removing: %s\n", path);
    }
    return success;
}

bool
file_remove(const char *path) {
    return file_remove_or_trash(path, true);
}

bool
file_trash(const char *path) {
    return file_remove_or_trash(path, false);
}

bool
launch_node(BTreeNode *node) {
    char path[PATH_MAX] = "";
    bool res = btree_node_init_parent_path(node, path, sizeof(path));
    if (res) {
        return open_uri(path);
    }
    return false;
}

bool
launch_node_path(BTreeNode *node, const char *cmd) {
    if (cmd) {
        return open_with_cmd(node, cmd);
    }
    else {
        char path[PATH_MAX] = "";
        bool res = btree_node_init_path(node, path, sizeof(path));
        if (res) {
            return open_uri(path);
        }
    }
    return false;
}

gchar *
get_mimetype(const gchar *path) {
    if (!path) {
        return NULL;
    }
    gchar *content_type = g_content_type_guess(path, NULL, 1, NULL);
    if (content_type) {
        gchar *mimetype = g_content_type_get_description(content_type);
        g_free(content_type);
        content_type = NULL;
        return mimetype;
    }
    return NULL;
}

gchar *
get_file_type(BTreeNode *node, const gchar *path) {
    gchar *type = NULL;
    if (node->is_dir) {
        type = g_strdup(_("Folder"));
    }
    else {
        type = get_mimetype(path);
    }
    if (type == NULL) {
        type = g_strdup(_("Unknown Type"));
    }
    return type;
}

GIcon *
get_gicon_for_path(const char *path) {
    GFile *g_file = g_file_new_for_path(path);
    if (!g_file) {
        return g_themed_icon_new("edit-delete");
    }

    GFileInfo *file_info = g_file_query_info(g_file, "standard::icon", 0, NULL, NULL);
    if (!file_info) {
        g_object_unref(g_file);
        g_file = NULL;
        return g_themed_icon_new("edit-delete");
    }

    GIcon *icon = g_file_info_get_icon(file_info);
    g_object_ref(icon);

    g_object_unref(file_info);
    file_info = NULL;

    g_object_unref(g_file);
    g_file = NULL;

    return icon;
}

cairo_surface_t *
get_icon_surface(GdkWindow *win, const char *path, int icon_size, int scale_factor) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    if (!icon_theme) {
        return NULL;
    }

    cairo_surface_t *icon_surface = NULL;
    GIcon *icon = get_gicon_for_path(path);
    const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
    if (!names) {
        g_object_unref(icon);
        return NULL;
    }

    GtkIconInfo *icon_info = gtk_icon_theme_choose_icon_for_scale(icon_theme,
                                                                  (const char **)names,
                                                                  icon_size,
                                                                  scale_factor,
                                                                  GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!icon_info) {
        return NULL;
    }

    GdkPixbuf *pixbuf = gtk_icon_info_load_icon(icon_info, NULL);
    if (pixbuf) {
        icon_surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, win);
        g_object_unref(pixbuf);
    }
    g_object_unref(icon);
    g_object_unref(icon_info);

    return icon_surface;
}

int
get_icon_size_for_height(int height) {
    if (height < 24) {
        return 16;
    }
    if (height < 32) {
        return 24;
    }
    if (height < 48) {
        return 32;
    }
    return 48;
}

char *
get_size_formatted(BTreeNode *node, bool show_base_2_units) {
    if (!node->is_dir) {
        if (show_base_2_units) {
            return g_format_size_full(node->size, G_FORMAT_SIZE_IEC_UNITS);
        }
        else {
            return g_format_size_full(node->size, G_FORMAT_SIZE_DEFAULT);
        }
    }
    else {
        char buffer[100] = "";
        uint32_t num_children = btree_node_n_children(node);
        if (num_children == 1) {
            snprintf(buffer, sizeof(buffer), _("%d Item"), num_children);
        }
        else {
            snprintf(buffer, sizeof(buffer), _("%d Items"), num_children);
        }
        return g_strdup(buffer);
    }
}

int
compare_path(BTreeNode **a_node, BTreeNode **b_node) {
    if ((*a_node)->is_dir != (*b_node)->is_dir) {
        return (*b_node)->is_dir - (*a_node)->is_dir;
    }
    BTreeNode *a = (*a_node)->parent;
    BTreeNode *b = (*b_node)->parent;
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    const int32_t a_depth = btree_node_depth(a);
    const int32_t b_depth = btree_node_depth(b);
    char *a_parents[a_depth + 1];
    char *b_parents[b_depth + 1];
    a_parents[a_depth] = NULL;
    b_parents[b_depth] = NULL;

    BTreeNode *temp = a;
    for (int32_t i = a_depth - 1; i >= 0 && temp; i--) {
        a_parents[i] = temp->name;
        temp = temp->parent;
    }
    temp = b;
    for (int32_t i = b_depth - 1; i >= 0 && temp; i--) {
        b_parents[i] = temp->name;
        temp = temp->parent;
    }

    uint32_t i = 0;
    char *a_name = a_parents[i];
    char *b_name = b_parents[i];

    while (a_name && b_name) {
        int res = strverscmp(a_name, b_name);
        if (res != 0) {
            return res;
        }
        i++;
        a_name = a_parents[i];
        b_name = b_parents[i];
    }
    return a_depth - b_depth;
}

int
compare_name(BTreeNode **a_node, BTreeNode **b_node) {
    BTreeNode *a = *a_node;
    BTreeNode *b = *b_node;
    return a->pos - b->pos;
}

int
compare_size(BTreeNode **a_node, BTreeNode **b_node) {
    BTreeNode *a = *a_node;
    BTreeNode *b = *b_node;
    bool is_dir_a = a->is_dir;
    bool is_dir_b = b->is_dir;
    if (is_dir_a != is_dir_b) {
        return is_dir_b - is_dir_a;
    }
    if (is_dir_a && is_dir_b) {
        uint32_t n_a = btree_node_n_children(a);
        uint32_t n_b = btree_node_n_children(b);
        return n_a - n_b;
    }

    if (a->size == b->size) {
        return 0;
    }

    return (a->size > b->size) ? 1 : -1;
}

int
compare_changed(BTreeNode **a_node, BTreeNode **b_node) {
    BTreeNode *a = *a_node;
    BTreeNode *b = *b_node;
    if (a->is_dir != b->is_dir) {
        return b->is_dir - a->is_dir;
    }
    return a->mtime - b->mtime;
}

int
compare_type(BTreeNode **a_node, BTreeNode **b_node) {
    BTreeNode *a = *a_node;
    BTreeNode *b = *b_node;
    bool is_dir_a = a->is_dir;
    bool is_dir_b = b->is_dir;
    if (is_dir_a != is_dir_b) {
        return is_dir_b - is_dir_a;
    }
    if (is_dir_a && is_dir_b) {
        return 0;
    }

    gchar *type_a = NULL;
    gchar *type_b = NULL;

    gchar path_a[PATH_MAX] = "";
    gchar path_b[PATH_MAX] = "";

    btree_node_init_parent_path(a, path_a, sizeof(path_a));
    type_a = get_file_type(a, path_a);
    btree_node_init_parent_path(b, path_b, sizeof(path_b));
    type_b = get_file_type(b, path_b);

    gint return_val = 0;
    if (type_a && type_b) {
        return_val = strverscmp(type_a, type_b);
    }
    if (type_a) {
        g_free(type_a);
        type_a = NULL;
    }
    if (type_b) {
        g_free(type_b);
        type_b = NULL;
    }
    return return_val;
}


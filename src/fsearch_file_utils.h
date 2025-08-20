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

#pragma once

#include <gtk/gtk.h>
#include <stdbool.h>

typedef void (*FsearchFileUtilsOpenCallback)(gboolean result, const char *error_message, gpointer user_data);

void
fsearch_file_utils_init_data_dir_path(char *path, size_t len);

bool
fsearch_file_utils_create_dir(const char *path);

bool
fsearch_file_utils_trash(const char *path, GString *error_messages);

bool
fsearch_file_utils_remove(const char *path, GString *error_messages);

void
fsearch_file_utils_open_path_list(GList *paths,
                                  bool launch_desktop_files,
                                  GAppLaunchContext *app_launch_context,
                                  FsearchFileUtilsOpenCallback callback,
                                  gpointer callback_data);

bool
fsearch_file_utils_open_path_list_with_command(GList *paths, const char *cmd, GString *error_message);

bool
fsearch_file_utils_open_path_list_with_command_internal(GList *paths, const char *cmd, GString *error_message, bool use_full_path);

gchar *
fsearch_file_utils_get_file_type(const gchar *name, gboolean is_dir);

gchar *
fsearch_file_utils_get_file_type_non_localized(const char *name, gboolean is_dir);

GIcon *
fsearch_file_utils_get_icon_for_path(const char *path);

GIcon *
fsearch_file_utils_guess_icon(const char *name, const char *path, bool is_dir);

char *
fsearch_file_utils_get_size_formatted(off_t size, bool show_base_2_units);

bool
fsearch_file_utils_is_desktop_file(const char *path);

GIcon *
fsearch_file_utils_get_desktop_file_icon(const char *path);

char *
fsearch_file_utils_get_content_type(const char *path, GError **error);

GIcon *
fsearch_file_utils_get_thumbnail_icon(const char *path);

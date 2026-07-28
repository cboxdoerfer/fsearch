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

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef enum {
    FSEARCH_QUERY_CLI_SUCCESS = 0,
    FSEARCH_QUERY_CLI_ERROR_DB_NOT_FOUND = 1,
    FSEARCH_QUERY_CLI_ERROR_DB_EMPTY = 1,
    FSEARCH_QUERY_CLI_ERROR_EMPTY_QUERY = 1,
    FSEARCH_QUERY_CLI_ERROR_DB_LOAD = 1,
    FSEARCH_QUERY_CLI_ERROR_SEARCH = 1,
} FsearchQueryCliExitCode;

typedef enum {
    FSEARCH_QUERY_CLI_OUTPUT_JSON = 0,
    FSEARCH_QUERY_CLI_OUTPUT_PRETTY,
} FsearchQueryCliOutputFormat;

typedef enum {
    FSEARCH_QUERY_CLI_SORT_NAME = 0,
    FSEARCH_QUERY_CLI_SORT_PATH,
    FSEARCH_QUERY_CLI_SORT_SIZE,
    FSEARCH_QUERY_CLI_SORT_MTIME,
    FSEARCH_QUERY_CLI_SORT_EXTENSION,
} FsearchQueryCliSortProperty;

int
fsearch_query_cli_run(const char *search_term,
                      int limit,
                      bool use_regex,
                      bool match_case,
                      bool search_in_path,
                      bool files_only,
                      bool folders_only,
                      FsearchQueryCliSortProperty sort_prop,
                      bool sort_desc,
                      FsearchQueryCliOutputFormat output_format);

G_END_DECLS

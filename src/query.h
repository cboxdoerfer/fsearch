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

#pragma once

#include <glib.h>
#include <pango/pango.h>
#include <stdbool.h>

#include "database.h"
#include "fsearch_filter.h"

typedef struct _FsearchQueryHighlightToken {
    GRegex *regex;

    bool is_supported_glob;
    bool start_with_asterisk;
    bool end_with_asterisk;

    uint32_t hl_start;
    uint32_t hl_end;

    char *text;
    size_t query_len;
} FsearchQueryHighlightToken;

typedef struct _FsearchQueryHighlight {
    GList *token;

    bool auto_search_in_path;
    bool auto_match_case;
    bool search_in_path;
    bool has_separator;
    bool match_case;
} FsearchQueryHighlight;

typedef struct _FsearchQuery {
    char *text;
    FsearchDatabase *db;
    FsearchFilter filter;

    uint32_t max_results;

    bool match_case;
    bool auto_match_case;
    bool enable_regex;
    bool search_in_path;
    bool auto_search_in_path;
    bool pass_on_empty_query;

    void (*callback)(void *);
    void *callback_data;
    void (*callback_cancelled)(void *);
    void *callback_cancelled_data;
} FsearchQuery;

FsearchQuery *
fsearch_query_new(const char *text,
                  FsearchDatabase *db,
                  FsearchFilter filter,
                  void (*callback)(void *),
                  void *callback_data,
                  void (*callback_cancelled)(void *),
                  void *callback_cancelled_data,
                  uint32_t max_results,
                  bool match_case,
                  bool auto_match_case,
                  bool enable_regex,
                  bool auto_search_in_path,
                  bool search_in_path,
                  bool pass_on_empty_query);

void
fsearch_query_free(FsearchQuery *query);

PangoAttrList *
fsearch_query_highlight_match(FsearchQueryHighlight *q, const char *input);

FsearchQueryHighlight *
fsearch_query_highlight_new(const char *text,
                            bool enable_regex,
                            bool match_case,
                            bool auto_match_case,
                            bool auto_search_in_path,
                            bool search_in_path);

void
fsearch_query_highlight_free(FsearchQueryHighlight *query_highlight);


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

#include <glib.h>
#include <pango/pango.h>
#include <stdbool.h>

#include "array.h"
#include "fsearch_filter.h"
#include "token.h"

typedef struct FsearchQueryFlags {
    bool match_case;
    bool auto_match_case;
    bool enable_regex;
    bool search_in_path;
    bool auto_search_in_path;
} FsearchQueryFlags;

typedef struct FsearchQueryHighlightToken {
    GRegex *regex;

    bool is_supported_glob;
    bool start_with_asterisk;
    bool end_with_asterisk;

    uint32_t hl_start;
    uint32_t hl_end;

    char *text;
    size_t query_len;
} FsearchQueryHighlightToken;

typedef struct FsearchQueryHighlight {
    GList *token;

    FsearchQueryFlags flags;
    bool has_separator;
} FsearchQueryHighlight;

typedef struct FsearchQuery {
    char *text;
    DynamicArray *entries;
    uint32_t num_folders;
    uint32_t num_files;

    FsearchFilter *filter;

    FsearchToken **token;
    uint32_t num_token;

    FsearchToken **filter_token;
    uint32_t num_filter_token;

    bool pass_on_empty_query;
    FsearchQueryFlags flags;

    void (*callback)(void *);
    void *callback_data;
    void (*callback_cancelled)(void *);
    void *callback_cancelled_data;
} FsearchQuery;

FsearchQuery *
fsearch_query_new(const char *text,
                  DynamicArray *array,
                  uint32_t num_folders,
                  uint32_t num_files,
                  FsearchFilter *filter,
                  void (*callback)(void *),
                  void *callback_data,
                  void (*callback_cancelled)(void *),
                  void *callback_cancelled_data,
                  FsearchQueryFlags flags,
                  bool pass_on_empty_query);

void
fsearch_query_free(FsearchQuery *query);

PangoAttrList *
fsearch_query_highlight_match(FsearchQueryHighlight *q, const char *input);

FsearchQueryHighlight *
fsearch_query_highlight_new(const char *text, FsearchQueryFlags flags);

void
fsearch_query_highlight_free(FsearchQueryHighlight *query_highlight);


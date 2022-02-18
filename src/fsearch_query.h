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
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <stdbool.h>

#include "fsearch_array.h"
#include "fsearch_database.h"
#include "fsearch_filter.h"
#include "fsearch_list_view.h"
#include "fsearch_query_match_context.h"
#include "fsearch_query_flags.h"
#include "fsearch_thread_pool.h"
#include "fsearch_query_node.h"

typedef struct FsearchQuery {
    char *search_term;

    FsearchDatabase *db;

    int32_t sort_order;

    FsearchThreadPool *pool;

    FsearchFilter *filter;

    GNode *token;
    GNode *filter_token;

    GList *highlight_tokens;

    FsearchQueryFlags flags;

    bool has_separator;

    char *query_id;

    gpointer data;

    volatile int ref_count;
} FsearchQuery;

FsearchQuery *
fsearch_query_new(const char *search_term,
                  FsearchDatabase *db,
                  int32_t sort_order,
                  FsearchFilter *filter,
                  FsearchThreadPool *pool,
                  FsearchQueryFlags flags,
                  const char *query_id,
                  gpointer data);

FsearchQuery *
fsearch_query_ref(FsearchQuery *query);

void
fsearch_query_unref(FsearchQuery *query);

PangoAttrList *
fsearch_query_highlight_match(FsearchQuery *q, const char *input);

bool
fsearch_query_matches_everything(FsearchQuery *query);

bool
fsearch_query_match(FsearchQuery *queyr, FsearchQueryMatchContext *matcher);
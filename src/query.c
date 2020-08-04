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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "query.h"

FsearchQuery *
fsearch_query_new (const char *query,
                   FsearchDatabase *db,
                   FsearchFilter filter,
                   void (*callback)(void *),
                   void *callback_data,
                   uint32_t max_results,
                   bool match_case,
                   bool enable_regex,
                   bool auto_search_in_path,
                   bool search_in_path,
                   bool pass_on_empty_query)
{
    FsearchQuery *q = calloc (1, sizeof (FsearchQuery));
    assert (q != NULL);
    if (query) {
        q->query = strdup (query);
    }
    q->db = db;
    q->filter = filter;
    q->callback = callback;
    q->callback_data = callback_data;
    q->max_results = max_results;
    q->match_case = match_case;
    q->enable_regex = enable_regex;
    q->auto_search_in_path = auto_search_in_path;
    q->search_in_path = search_in_path;
    q->pass_on_empty_query = pass_on_empty_query;
    return q;
}

void
fsearch_query_free (FsearchQuery *query)
{
    assert (query != NULL);
    if (query->query) {
        free (query->query);
        query->query = NULL;
    }
    free (query);
    query = NULL;
}

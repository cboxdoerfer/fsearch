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

#include <stdbool.h>

typedef struct
{
    char *query;
    bool match_case;
    bool enable_regex;
    bool auto_search_in_path;
    bool search_in_path;

    void (*callback)(void *);
    void *callback_data;
} FsearchQuery;

FsearchQuery *
fsearch_query_new (const char *query,
                   void (*callback)(void *),
                   void *callback_data,
                   bool match_case,
                   bool enable_regex,
                   bool auto_search_in_path,
                   bool search_in_path);

void
fsearch_query_free (FsearchQuery *query);

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

#include "fsearch_query_flags.h"

typedef struct FsearchFilter {
    char *name;
    char *macro;
    char *query;
    FsearchQueryFlags flags;

    volatile int ref_count;
} FsearchFilter;

FsearchFilter *
fsearch_filter_new(const char *name, const char *macro, const char *query, FsearchQueryFlags flags);

FsearchFilter *
fsearch_filter_ref(FsearchFilter *filter);

bool
fsearch_filter_cmp(FsearchFilter *filter_1, FsearchFilter *filter_2);

FsearchFilter *
fsearch_filter_copy(FsearchFilter *filter);

void
fsearch_filter_unref(FsearchFilter *filter);

GPtrArray *
fsearch_filter_get_default_filters(void);
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

#include "fsearch_database_entry.h"
#include "fsearch_utf.h"

#include <pango/pango-attributes.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchQueryMatchData FsearchQueryMatchData;

FsearchQueryMatchData *
fsearch_query_match_data_new(size_t *file_attr_offsets, size_t *folder_attr_offsets);

void
fsearch_query_match_data_free(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_entry(FsearchQueryMatchData *match_data, FsearchDatabaseEntry *entry);

void
fsearch_query_match_data_add_highlight(FsearchQueryMatchData *match_data,
                                       PangoAttribute *attribute,
                                       FsearchDatabaseIndexProperty idx);

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchData *match_data, FsearchDatabaseIndexProperty idx);

GHashTable *
fsearch_query_match_data_get_highlights(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_thread_id(FsearchQueryMatchData *match_data, int32_t thread_id);

int32_t
fsearch_query_match_data_get_thread_id(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_result(FsearchQueryMatchData *match_data, bool result);

bool
fsearch_query_match_data_get_result(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_name_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_parent_path_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_path_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_content_type_str(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_parent_path_builder(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_path_builder(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_name_builder(FsearchQueryMatchData *match_data);

FsearchDatabaseEntry *
fsearch_query_match_data_get_entry(FsearchQueryMatchData *match_data);
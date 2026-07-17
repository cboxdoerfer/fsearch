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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include "fsearch_query_match_data.h"
#include "fsearch_query_node.h"

#include <stdbool.h>
#include <stdint.h>

uint32_t
fsearch_query_matcher_false(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_true(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_date_modified(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_depth(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childcount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childfilecount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_childfoldercount(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_utf_strcasecmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_utf_strcasestr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strstr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcasestr(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_strcasecmp(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_none(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_extension(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_size(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_regex(FsearchQueryNode *node, FsearchQueryMatchData *match_data);

uint32_t
fsearch_query_matcher_highlight_ascii(FsearchQueryNode *node, FsearchQueryMatchData *match_data);
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

#include "fsearch_query_flags.h"
#include "fsearch_query_lexer.h"

#include <glib.h>
#include <stdbool.h>

typedef struct FsearchQueryParseContext {
    FsearchQueryLexer *lexer;
    GPtrArray *macro_filters;
    GQueue *operator_stack;
    GQueue *macro_stack;
    FsearchQueryToken last_token;
} FsearchQueryParseContext;

GList *
fsearch_query_parser_parse_expression(FsearchQueryParseContext *parse_ctx,
                                      bool in_open_bracket,
                                      FsearchQueryFlags flags);
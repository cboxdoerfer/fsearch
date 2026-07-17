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

typedef enum FsearchQueryToken {
    FSEARCH_QUERY_TOKEN_NONE,
    FSEARCH_QUERY_TOKEN_EOS,
    FSEARCH_QUERY_TOKEN_WORD,
    FSEARCH_QUERY_TOKEN_FIELD,
    FSEARCH_QUERY_TOKEN_FIELD_EMPTY,
    FSEARCH_QUERY_TOKEN_AND,
    FSEARCH_QUERY_TOKEN_OR,
    FSEARCH_QUERY_TOKEN_NOT,
    FSEARCH_QUERY_TOKEN_CONTAINS,
    FSEARCH_QUERY_TOKEN_GREATER_EQ,
    FSEARCH_QUERY_TOKEN_GREATER,
    FSEARCH_QUERY_TOKEN_SMALLER_EQ,
    FSEARCH_QUERY_TOKEN_SMALLER,
    FSEARCH_QUERY_TOKEN_EQUAL,
    FSEARCH_QUERY_TOKEN_BRACKET_OPEN,
    FSEARCH_QUERY_TOKEN_BRACKET_CLOSE,
    NUM_FSEARCH_QUERY_TOKENS,
} FsearchQueryToken;

typedef struct FsearchQueryLexer FsearchQueryLexer;

FsearchQueryLexer *
fsearch_query_lexer_new(const char *input);

void
fsearch_query_lexer_free(FsearchQueryLexer *lexer);

FsearchQueryToken
fsearch_query_lexer_peek_next_token(FsearchQueryLexer *lexer, GString **result);

FsearchQueryToken
fsearch_query_lexer_get_next_token(FsearchQueryLexer *lexer, GString **result);
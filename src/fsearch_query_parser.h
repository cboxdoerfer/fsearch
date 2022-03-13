#pragma once

#include "fsearch_query_flags.h"
#include "fsearch_query_lexer.h"

#include <glib.h>
#include <stdbool.h>

typedef struct FsearchQueryParseContext {
    GPtrArray *macro_filters;
    GQueue *operator_stack;
    GQueue *macro_stack;
    FsearchQueryToken last_token;
} FsearchQueryParseContext;

GList *
fsearch_query_parser_parse_expression(FsearchQueryLexer *lexer,
                                      FsearchQueryParseContext *parse_ctx,
                                      bool in_open_bracket,
                                      FsearchQueryFlags flags);

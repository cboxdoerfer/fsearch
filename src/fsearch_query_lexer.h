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

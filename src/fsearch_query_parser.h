#pragma once

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
    FSEARCH_QUERY_TOKEN_MACRO,
    NUM_FSEARCH_QUERY_TOKENS,
} FsearchQueryToken;

typedef struct FsearchQueryParser FsearchQueryParser;

typedef struct FsearchQueryParserMacro FsearchQueryParserMacro;

void
fsearch_query_parser_macro_free(FsearchQueryParserMacro *macro);

FsearchQueryParserMacro *
fsearch_query_parser_macro_new(const char *name, const char *text);

FsearchQueryParser *
fsearch_query_parser_new(const char *input, GPtrArray *macros);

void
fsearch_query_parser_free(FsearchQueryParser *parser);

FsearchQueryToken
fsearch_query_parser_peek_next_token(FsearchQueryParser *parser, GString **result);

FsearchQueryToken
fsearch_query_parser_get_next_token(FsearchQueryParser *parser, GString **result);

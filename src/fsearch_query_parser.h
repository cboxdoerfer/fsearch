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
    NUM_FSEARCH_QUERY_TOKENS,
} FsearchQueryToken;

typedef struct FsearchQueryParser FsearchQueryParser;

FsearchQueryParser *
fsearch_query_parser_new(const char *input);

void
fsearch_query_parser_free(FsearchQueryParser *parser);

FsearchQueryToken
fsearch_query_parser_peek_next_token(FsearchQueryParser *parser, GString **result);

FsearchQueryToken
fsearch_query_parser_get_next_token(FsearchQueryParser *parser, GString **result);

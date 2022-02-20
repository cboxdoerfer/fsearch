#include <assert.h>
#include <glib.h>
#include <stdlib.h>
#include <stdint.h>

#include "fsearch_query_parser.h"

struct FsearchQueryParser {
    GString *input;
    GQueue *char_stack;

    uint32_t input_pos;
};

static const char *reserved_chars = ":=<>()";

static char
get_next_input_char(FsearchQueryParser *parser) {
    if (parser->input && parser->input_pos < parser->input->len) {
        return parser->input->str[parser->input_pos++];
    }
    return '\0';
}

static char
top_char(GQueue *stack) {
    return (char)GPOINTER_TO_UINT(g_queue_peek_tail(stack));
}

static char
pop_char(GQueue *stack) {
    return (char)(GPOINTER_TO_UINT(g_queue_pop_tail(stack)));
}

static void
push_char(GQueue *stack, char c) {
    g_queue_push_tail(stack, GUINT_TO_POINTER((guint)c));
}

static char
get_next_char(FsearchQueryParser *parser) {
    GQueue *stack = parser->char_stack;
    if (!g_queue_is_empty(stack)) {
        return pop_char(stack);
    }
    return get_next_input_char(parser);
}

static void
give_back_char(FsearchQueryParser *parser, char c) {
    push_char(parser->char_stack, c);
}

// parse_string() assumes that the first double quote was already read
static void
parse_quoted_string(FsearchQueryParser *parser, GString *string) {
    char c = '\0';
    while ((c = get_next_char(parser))) {
        switch (c) {
        case '\\':
            // escape: get next char
            c = get_next_char(parser);
            g_string_append_c(string, c);
            if (c == '\0') {
                return;
            }
            break;
        case '"':
            // end of string reached
            return;
        default:
            g_string_append_c(string, c);
        }
    }
}

FsearchQueryToken
fsearch_query_parser_get_next_token(FsearchQueryParser *parser, GString **word) {
    FsearchQueryToken token = FSEARCH_QUERY_TOKEN_NONE;
    GString *token_value = NULL;

    char c = '\0';

    /* Skip white space.  */
    while ((c = get_next_char(parser)) && g_ascii_isspace(c)) {
        continue;
    }

    if (c == '\0')
        return FSEARCH_QUERY_TOKEN_EOS;

    // field-term relations, and ranges
    switch (c) {
    case '=':
        return FSEARCH_QUERY_TOKEN_EQUAL;
    case ':':
        return FSEARCH_QUERY_TOKEN_CONTAINS;
    case '<': {
        char c1 = get_next_char(parser);
        if (c1 == '=') {
            return FSEARCH_QUERY_TOKEN_SMALLER_EQ;
        }
        else {
            give_back_char(parser, c1);
            return FSEARCH_QUERY_TOKEN_SMALLER;
        }
    }
    case '>': {
        char c1 = get_next_char(parser);
        if (c1 == '=') {
            return FSEARCH_QUERY_TOKEN_GREATER_EQ;
        }
        else {
            give_back_char(parser, c1);
            return FSEARCH_QUERY_TOKEN_GREATER;
        }
    }
    case '!':
        return FSEARCH_QUERY_TOKEN_NOT;
    case '(':
        return FSEARCH_QUERY_TOKEN_BRACKET_OPEN;
    case ')':
        return FSEARCH_QUERY_TOKEN_BRACKET_CLOSE;
    }

    give_back_char(parser, c);

    // Other chars start a term or field name or reserved word
    token_value = g_string_sized_new(1024);
    while ((c = get_next_char(parser))) {
        if (g_ascii_isspace(c)) {
            // word broken by whitespace
            break;
        }
        else if (c == '"') {
            parse_quoted_string(parser, token_value);
        }
        else if (c == '\\') {
            // escape: get next char
            c = get_next_char(parser);
            g_string_append_c(token_value, c);
        }
        else if (strchr(reserved_chars, c)) {
            if (c == ':') {
                // field: detected
                token = FSEARCH_QUERY_TOKEN_FIELD;
                goto out;
            }
            // word broken by reserved character
            give_back_char(parser, c);
            break;
        }
        else {
            g_string_append_c(token_value, c);
        }
    }

    if (!strcmp(token_value->str, "NOT")) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
        return FSEARCH_QUERY_TOKEN_NOT;
    }
    if (!strcmp(token_value->str, "AND") || !strcmp(token_value->str, "&&")) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
        return FSEARCH_QUERY_TOKEN_AND;
    }
    else if (!strcmp(token_value->str, "OR") || !strcmp(token_value->str, "||")) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
        return FSEARCH_QUERY_TOKEN_OR;
    }

    token = FSEARCH_QUERY_TOKEN_WORD;

out:
    if (word) {
        *word = token_value;
    }
    else if (token_value) {
        g_string_free(g_steal_pointer(&token_value), TRUE);
    }
    return token;
}

FsearchQueryToken
fsearch_query_parser_peek_next_token(FsearchQueryParser *parser, GString **word) {
    // remember old lexing state
    size_t old_input_pos = parser->input_pos;
    GQueue *old_char_stack = g_queue_copy(parser->char_stack);

    FsearchQueryToken res = fsearch_query_parser_get_next_token(parser, word);

    // restore old lexing state
    parser->input_pos = old_input_pos;
    g_clear_pointer(&parser->char_stack, g_queue_free);
    parser->char_stack = old_char_stack;
    return res;
}

FsearchQueryParser *
fsearch_query_parser_new(const char *input) {
    assert(input != NULL);

    FsearchQueryParser *parser = calloc(1, sizeof(FsearchQueryParser));
    assert(parser != NULL);

    parser->input = g_string_new(input);
    parser->input_pos = 0;

    parser->char_stack = g_queue_new();
    return parser;
}

void
fsearch_query_parser_free(FsearchQueryParser *parser) {
    if (parser == NULL) {
        return;
    }
    g_string_free(g_steal_pointer(&parser->input), TRUE);
    g_clear_pointer(&parser->char_stack, g_queue_free);
    g_clear_pointer(&parser, free);
}

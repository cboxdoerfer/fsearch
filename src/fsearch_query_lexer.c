#include <glib.h>
#include <stdint.h>
#include <stdlib.h>

#include "fsearch_query_lexer.h"

struct FsearchQueryLexer {
    GString *input;
    GQueue *char_stack;

    uint32_t input_pos;
};

static const char *reserved_chars = ":=<>()";

static char
get_next_input_char(FsearchQueryLexer *lexer) {
    if (lexer->input && lexer->input_pos < lexer->input->len) {
        return lexer->input->str[lexer->input_pos++];
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
get_next_char(FsearchQueryLexer *lexer) {
    GQueue *stack = lexer->char_stack;
    if (!g_queue_is_empty(stack)) {
        return pop_char(stack);
    }
    return get_next_input_char(lexer);
}

static void
give_back_char(FsearchQueryLexer *lexer, char c) {
    push_char(lexer->char_stack, c);
}

// parse_string() assumes that the first double quote was already read
static void
parse_quoted_string(FsearchQueryLexer *lexer, GString *string) {
    char c = '\0';
    while ((c = get_next_char(lexer))) {
        if (c == '"') {
            return;
        }
        else {
            g_string_append_c(string, c);
        }
    }
}

FsearchQueryToken
fsearch_query_lexer_get_next_token(FsearchQueryLexer *lexer, GString **result) {
    FsearchQueryToken token = FSEARCH_QUERY_TOKEN_NONE;

    char c = '\0';

    /* Skip white space.  */
    while ((c = get_next_char(lexer)) && g_ascii_isspace(c)) {
        continue;
    }

    // field-term relations, and ranges
    switch (c) {
    case '\0':
        return FSEARCH_QUERY_TOKEN_EOS;
    case '=':
        return FSEARCH_QUERY_TOKEN_EQUAL;
    case ':':
        return FSEARCH_QUERY_TOKEN_CONTAINS;
    case '<': {
        char c1 = get_next_char(lexer);
        if (c1 == '=') {
            return FSEARCH_QUERY_TOKEN_SMALLER_EQ;
        }
        else {
            give_back_char(lexer, c1);
            return FSEARCH_QUERY_TOKEN_SMALLER;
        }
    }
    case '>': {
        char c1 = get_next_char(lexer);
        if (c1 == '=') {
            return FSEARCH_QUERY_TOKEN_GREATER_EQ;
        }
        else {
            give_back_char(lexer, c1);
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

    give_back_char(lexer, c);

    // Other chars start a term or field name or reserved word
    g_autoptr(GString) token_value = g_string_sized_new(1024);

    while ((c = get_next_char(lexer))) {
        if (g_ascii_isspace(c)) {
            // word broken by whitespace
            break;
        }
        else if (c == '"') {
            parse_quoted_string(lexer, token_value);
        }
        else if (c == '\\') {
            // escape: get next char
            c = get_next_char(lexer);
            g_string_append_c(token_value, c);
        }
        else if (strchr(reserved_chars, c)) {
            if (c == ':') {
                // field: detected
                c = get_next_char(lexer);
                if (g_ascii_isspace(c) || c == '\0') {
                    token = FSEARCH_QUERY_TOKEN_FIELD_EMPTY;
                }
                else {
                    give_back_char(lexer, c);
                    token = FSEARCH_QUERY_TOKEN_FIELD;
                }
                goto out;
            }
            // word broken by reserved character
            give_back_char(lexer, c);
            break;
        }
        else {
            g_string_append_c(token_value, c);
        }
    }

    if (!strcmp(token_value->str, "NOT")) {
        return FSEARCH_QUERY_TOKEN_NOT;
    }
    if (!strcmp(token_value->str, "AND") || !strcmp(token_value->str, "&&")) {
        return FSEARCH_QUERY_TOKEN_AND;
    }
    else if (!strcmp(token_value->str, "OR") || !strcmp(token_value->str, "||")) {
        return FSEARCH_QUERY_TOKEN_OR;
    }

    token = FSEARCH_QUERY_TOKEN_WORD;

out:
    if (result) {
        *result = g_steal_pointer(&token_value);
    }
    return token;
}

FsearchQueryToken
fsearch_query_lexer_peek_next_token(FsearchQueryLexer *lexer, GString **result) {
    // remember old lexing state
    size_t old_input_pos = lexer->input_pos;
    GQueue *old_char_stack = g_queue_copy(lexer->char_stack);

    FsearchQueryToken res = fsearch_query_lexer_get_next_token(lexer, result);

    // restore old lexing state
    lexer->input_pos = old_input_pos;
    g_clear_pointer(&lexer->char_stack, g_queue_free);
    lexer->char_stack = old_char_stack;
    return res;
}

FsearchQueryLexer *
fsearch_query_lexer_new(const char *input) {
    g_assert(input);

    FsearchQueryLexer *lexer = calloc(1, sizeof(FsearchQueryLexer));
    g_assert(lexer);

    lexer->input = g_string_new(input);
    lexer->input_pos = 0;

    lexer->char_stack = g_queue_new();
    return lexer;
}

void
fsearch_query_lexer_free(FsearchQueryLexer *lexer) {
    if (lexer == NULL) {
        return;
    }
    g_string_free(g_steal_pointer(&lexer->input), TRUE);
    g_clear_pointer(&lexer->char_stack, g_queue_free);
    g_clear_pointer(&lexer, free);
}

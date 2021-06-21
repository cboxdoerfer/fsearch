#include <glib.h>
#include <stdlib.h>

#include <src/fsearch_token.h>

typedef struct TestQuery {
    const char *query;
    int num_expected_token;
} TestQuery;

int
main(int argc, char *argv[]) {
    TestQuery test_queries[] = {
        {"only_token", 1},
        {"first_token second_token", 2},
        {"\"only token\"", 1},
        {"\"first token\" second_token", 2},
        {"\"first and only\"token", 1},
        {"first\\ and\\ only\\ token", 1},
        {"first\\ token second\\ token", 2},
    };

    const int num_test_queries = sizeof(test_queries) / sizeof(test_queries[0]);
    for (int i = 0; i < num_test_queries; i++) {
        FsearchToken **tokens = fsearch_tokens_new(test_queries[i].query, false, false, true);
        g_assert(tokens != NULL);

        int num_token = 0;
        for (uint32_t j = 0; tokens[j] != NULL; j++) {
            g_print("%d: token %d: %s\n", i, j, tokens[j]->text);
            num_token++;
        }
        g_assert(num_token == test_queries[i].num_expected_token);

        g_clear_pointer(&tokens, fsearch_tokens_free);
        g_assert(tokens == NULL);
    }
}

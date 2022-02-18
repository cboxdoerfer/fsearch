#include <glib.h>
#include <stdint.h>
#include <stdlib.h>

#include <src/fsearch_query_node.h>

typedef struct TestQuery {
    const char *query;
    int num_expected_token;
    char **expected_tokens;
} TestQuery;

char **
new_strv(int num, ...) {
    va_list arguments;

    va_start(arguments, num);

    char **ret = calloc(num + 1, sizeof(char *));

    for (int i = 0; i < num; i++) {
        ret[i] = g_strdup(va_arg(arguments, const char *));
    }
    ret[num] = NULL;

    va_end(arguments); // Cleans up the list

    return ret;
}

#define Q_TEST(q, n, ...) {q, n, new_strv(n, ##__VA_ARGS__)}

int
main(int argc, char *argv[]) {
    // TestQuery test_queries[] = {
    //    Q_TEST("only_token", 1, "only_token"),
    //    Q_TEST("first_token second_token", 2, "first_token", "second_token"),
    //    Q_TEST("\"only token\"", 1, "only token"),
    //    Q_TEST("\"first token\" second_token", 2, "first token", "second_token"),
    //    Q_TEST("\"first and only\"token", 1, "first and onlytoken"),
    //    Q_TEST("first\\ and\\ only\\ token", 1, "first and only token"),
    //    Q_TEST("first\\ token second\\ token", 2, "first token", "second token"),
    //    Q_TEST("first_token              second_token", 2, "first_token", "second_token"),
    //    Q_TEST("\"first     token\" second_token", 2, "first     token", "second_token"),
    //};

    // const int num_test_queries = sizeof(test_queries) / sizeof(test_queries[0]);
    // for (int i = 0; i < num_test_queries; i++) {
    //     uint32_t num_token = 0;
    //     FsearchQueryNode **tokens = fsearch_tokens_new(test_queries[i].query, QUERY_FLAG_AUTO_MATCH_CASE,
    //     &num_token); g_assert(tokens != NULL); g_assert(num_token == test_queries[i].num_expected_token);

    //    for (uint32_t j = 0; j < num_token; j++) {
    //        g_print("%d: token %d: %s\n", i, j, tokens[j]->search_term);
    //        g_assert(g_strcmp0(tokens[j]->search_term, test_queries[i].expected_tokens[j]) == 0);
    //    }
    //    g_clear_pointer(&test_queries[i].expected_tokens, g_strfreev);
    //    g_assert(test_queries[i].expected_tokens == NULL);

    //    g_clear_pointer(&tokens, fsearch_tokens_free);
    //    g_assert(tokens == NULL);
    //}
}

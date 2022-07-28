#include <glib.h>

#include <src/fsearch_size_utils.h>

static void
test_parse_size(void) {
    typedef struct {
        const char *string;
        gboolean expected_success;
        int64_t expected_size;
        int64_t expected_plus;
        int64_t expected_end_idx;
    } FsearchTestSizeParseContext;

    // size factors
    const int64_t fkb = 1000;
    const int64_t fmb = 1000 * fkb;
    const int64_t fgb = 1000 * fmb;
    const int64_t ftb = 1000 * fgb;
    // expected plus
    const int64_t pkb = 1000 - 50 - 1;
    const int64_t pmb = fkb * (1000 - 50) - 1;
    const int64_t pgb = fmb * (1000 - 50) - 1;
    const int64_t ptb = fgb * (1000 - 50) - 1;

    FsearchTestSizeParseContext file_names[] = {
        {"abc", FALSE, 0, 0, 0},
        {"mb", FALSE, 0, 0, 0},
        {"0m", TRUE, 0, pmb, 2},
        {"100", TRUE, 100, 0, 3},
        {"100abc", FALSE, 100, 0, 3},
        {"100k", TRUE, 100 * fkb, pkb, 4},
        {"100K", TRUE, 100 * fkb, pkb, 4},
        {"12mb", TRUE, 12 * fmb, pmb, 4},
        {"12Mb", TRUE, 12 * fmb, pmb, 4},
        {"12mB", TRUE, 12 * fmb, pmb, 4},
        {"123MB", TRUE, 123 * fmb, pmb, 5},
        {"1234GB", TRUE, 1234 * fgb, pgb, 6},
        {"12345TB", TRUE, 12345 * ftb, ptb, 7},
    };

    for (gint i = 0; i < G_N_ELEMENTS(file_names); ++i) {
        FsearchTestSizeParseContext *ctx = &file_names[i];
        int64_t size = 0;
        int64_t plus = 0;
        char *end_ptr = NULL;
        g_print("%s\n", ctx->string);
        gboolean res = fsearch_size_parse(ctx->string, &size, &plus);
        g_assert_true(res == ctx->expected_success);
        if (res == TRUE) {
            g_assert_cmpint(size, ==, ctx->expected_size);
            g_assert_cmpint(plus, ==, ctx->expected_size + ctx->expected_plus);
        }
        // g_assert_cmpstr(end_ptr, ==, ctx->string + ctx->expected_end_idx);
    }
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/size_utils/parse_size", test_parse_size);
    return g_test_run();
}

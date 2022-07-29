#include <glib.h>

#include <src/fsearch_size_utils.h>

static void
test_parse_size(void) {
    typedef struct {
        const char *string;
        gboolean expected_success;
        int64_t expected_size;
        int64_t expected_plus;
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
        {"abc", FALSE, 0, 0},
        {"mb", FALSE, 0, 0},
        {"0m", TRUE, 0, pmb},
        {"100", TRUE, 100, 0},
        {"100abc", FALSE, 100, 0},
        {"100k", TRUE, 100 * fkb, pkb},
        {"100K", TRUE, 100 * fkb, pkb},
        {"12mb", TRUE, 12 * fmb, pmb},
        {"12Mb", TRUE, 12 * fmb, pmb},
        {"12mB", TRUE, 12 * fmb, pmb},
        {"123MB", TRUE, 123 * fmb, pmb},
        {"1234GB", TRUE, 1234 * fgb, pgb},
        {"12345TB", TRUE, 12345 * ftb, ptb},
    };

    for (gint i = 0; i < G_N_ELEMENTS(file_names); ++i) {
        FsearchTestSizeParseContext *ctx = &file_names[i];
        int64_t size_start = 0;
        int64_t size_end = 0;

        gboolean res = fsearch_size_parse(ctx->string, &size_start, &size_end);
        g_assert_true(res == ctx->expected_success);
        if (res == TRUE) {
            g_assert_cmpint(size_start, ==, ctx->expected_size);
            g_assert_cmpint(size_end, ==, ctx->expected_size + ctx->expected_plus);
        }
    }
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/size_utils/parse_size", test_parse_size);
    return g_test_run();
}

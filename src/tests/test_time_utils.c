#include <glib.h>

#include <src/fsearch_time_utils.h>

static void
test_parse_time_interval(void) {
    typedef struct {
        const char *string;
        gboolean expected_success;
        time_t expected_time_start;
        time_t expected_time_end;
        int64_t expected_end_idx;
    } FsearchTestTimeIntervalParseContext;

    FsearchTestTimeIntervalParseContext strings[] = {
        {"2022", TRUE, 1640991600, 1672527600, strlen("2022")},
        {"abc", FALSE, 0, 0, 0},
        {"today", TRUE, 0, 0, strlen("today")},
        {"today", TRUE, 0, 0, strlen("today")},
        {"yesterday", TRUE, 0, 0, strlen("yesterday")},
        {"2022-01", TRUE, 0, 0, strlen("2022-01")},
        {"22-01", TRUE, 0, 0, strlen("22-01")},
        {"22-1", TRUE, 0, 0, strlen("22-1")},
        {"22-1-1", TRUE, 0, 0, strlen("22-1-1")},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestTimeIntervalParseContext *ctx = &strings[i];
        time_t time_start = 0;
        time_t time_end = 0;
        char *end_ptr = NULL;
        gboolean res = fsearch_date_time_parse_interval(ctx->string, &time_start, &time_end, &end_ptr);
        g_assert_true(res == ctx->expected_success);
        // g_assert_cmpint(time_start, ==, ctx->expected_time_start);
        // g_assert_cmpint(time_end, ==, ctx->expected_time_end);
        g_assert_cmpstr(end_ptr, ==, ctx->string + ctx->expected_end_idx);
    }
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/time_utils/parse_time_interval", test_parse_time_interval);
    return g_test_run();
}

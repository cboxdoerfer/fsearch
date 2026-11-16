#include <glib.h>

#include <src/fsearch_time_utils.h>

static void
test_parse_time_interval(void) {
    typedef struct {
        const char *string;
        gboolean expected_success;
        time_t expected_time_start;
        time_t expected_time_end;
    } FsearchTestTimeIntervalParseContext;

    FsearchTestTimeIntervalParseContext strings[] = {
        {"2000abc", FALSE, -1, -1},
        {"abc2000", FALSE, -1, -1},
        {"abc", FALSE, -1, -1},

        {"today", TRUE, -1, -1},
        {"yesterday", TRUE, -1, -1},
        {"thishour", TRUE, -1, -1},
        {"pastyear", TRUE, -1, -1},
        {"past4year", FALSE, -1, -1},
        {"pastyears", FALSE, -1, -1},
        {"past3years", TRUE, -1, -1},
        {"lastweek", TRUE, -1, -1},
        {"last2weeks", TRUE, -1, -1},
        {"lasttwoweeks", TRUE, -1, -1},
        {"lastweeks", FALSE, -1, -1},
        {"inthelastday", TRUE, -1, -1},
        {"4months", TRUE, -1, -1},
        {"4month", FALSE, -1, -1},
        {"3min", TRUE, -1, -1},
        {"3minutes", TRUE, -1, -1},

        {"2022", TRUE, -1, -1},
        {"22", TRUE, -1, -1},
        {"2022-01", TRUE, -1, -1},
        {"22-01", TRUE, -1, -1},
        {"22-1", TRUE, -1, -1},
        {"22-1-1", TRUE, -1, -1},
        {"22-1-1 12:00:00", TRUE, -1, -1},
        {"2022-01-01 12:00:00", TRUE, -1, -1},
        {"2022-01-01 12:00", TRUE, -1, -1},
        {"2022-01-01 12", TRUE, -1, -1},
        {"2022-01 12:00:00", FALSE, -1, -1},
        {"1960", FALSE, -1, -1},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestTimeIntervalParseContext *ctx = &strings[i];
        time_t time_start = 0;
        time_t time_end = 0;

        gboolean res = fsearch_date_time_parse_interval(ctx->string, &time_start, &time_end);
        g_assert_true(res == ctx->expected_success);
        if (res == TRUE && ctx->expected_time_start != -1) {
            g_assert_cmpint(time_start, ==, ctx->expected_time_start);
            g_assert_cmpint(time_end, ==, ctx->expected_time_end);
        }
    }
}

static void
test_date_formatting(void) {
    struct tm tm_val = {
        .tm_year = 126, // 2026
        .tm_mon = 10,   // November (0-based)
        .tm_mday = 10,
        .tm_hour = 14,
        .tm_min = 35,
        .tm_sec = 0,
        .tm_isdst = -1,
    };
    time_t t = mktime(&tm_val);

    char buf[100];
    fsearch_time_utils_format_time(t, FSEARCH_DATE_FORMAT_YYYY_MM_DD, buf, sizeof(buf));
    g_assert_cmpstr(buf, ==, "2026-11-10 14:35");

    fsearch_time_utils_format_time(t, FSEARCH_DATE_FORMAT_MM_DD_YYYY, buf, sizeof(buf));
    g_assert_cmpstr(buf, ==, "11/10/2026 14:35");

    fsearch_time_utils_format_time(t, FSEARCH_DATE_FORMAT_DD_MM_YYYY_SLASH, buf, sizeof(buf));
    g_assert_cmpstr(buf, ==, "10/11/2026 14:35");

    fsearch_time_utils_format_time(t, FSEARCH_DATE_FORMAT_DD_MM_YYYY_DOT, buf, sizeof(buf));
    g_assert_cmpstr(buf, ==, "10.11.2026 14:35");
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/time_utils/parse_time_interval", test_parse_time_interval);
    g_test_add_func("/FSearch/time_utils/date_formatting", test_date_formatting);
    return g_test_run();
}
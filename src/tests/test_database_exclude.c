#include "fsearch_database_exclude.h"

static void
test_database_exclude() {
    struct exclude_ctx {
        const char *path;
        gboolean active;
    };

    struct exclude_ctx excludes[] = {
        {.path = "/home/user_1", .active = TRUE},
        {.path = "/home/user_2", .active = FALSE},
    };

    for (guint i = 0; i < G_N_ELEMENTS(excludes); ++i) {
        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(excludes[i].path, excludes[i].active);
        g_assert_cmpstr(fsearch_database_exclude_get_path(exclude), ==, excludes[i].path);
        g_assert_cmpint(fsearch_database_exclude_get_active(exclude), ==, excludes[i].active);
    }

    g_autoptr(FsearchDatabaseExclude) i1 = fsearch_database_exclude_new(excludes[0].path, excludes[0].active);
    g_autoptr(FsearchDatabaseExclude) i2 = fsearch_database_exclude_new(excludes[1].path, excludes[1].active);
    g_assert_false(fsearch_database_exclude_equal(i1, i2));
    g_assert_true(fsearch_database_exclude_equal(i1, i1));
    g_assert_true(fsearch_database_exclude_equal(i2, i2));

    g_autoptr(FsearchDatabaseExclude) i1_copy = fsearch_database_exclude_copy(i1);
    g_assert_true(fsearch_database_exclude_equal(i1, i1_copy));
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/database/exclude", test_database_exclude);
    return g_test_run();
}

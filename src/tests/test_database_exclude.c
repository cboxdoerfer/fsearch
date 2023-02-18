#include "fsearch_database_exclude_manager.h"

struct exclude_ctx {
    const char *path;
    gboolean active;
};

static struct exclude_ctx excludes[] = {
    {.path = "/home/user_1", .active = TRUE},
    {.path = "/home/user_2", .active = FALSE},
};

static void
test_database_exclude() {
    for (guint i = 0; i < G_N_ELEMENTS(excludes); ++i) {
        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(excludes[i].path, excludes[i].active);
        g_assert_cmpstr(fsearch_database_exclude_get_path(exclude), ==, excludes[i].path);
        g_assert_cmpint(fsearch_database_exclude_get_active(exclude), ==, excludes[i].active);
    }

    g_autoptr(FsearchDatabaseExclude) e1 = fsearch_database_exclude_new(excludes[0].path, excludes[0].active);
    g_autoptr(FsearchDatabaseExclude) e2 = fsearch_database_exclude_new(excludes[1].path, excludes[1].active);
    g_assert_false(fsearch_database_exclude_equal(e1, e2));
    g_assert_true(fsearch_database_exclude_equal(e1, e1));
    g_assert_true(fsearch_database_exclude_equal(e2, e2));

    g_autoptr(FsearchDatabaseExclude) e1_copy = fsearch_database_exclude_copy(e1);
    g_assert_true(fsearch_database_exclude_equal(e1, e1_copy));
}

static void
test_database_exclude_manager() {
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    for (guint i = 0; i < G_N_ELEMENTS(excludes); ++i) {
        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(excludes[i].path, excludes[i].active);
        fsearch_database_exclude_manager_add(exclude_manager, exclude);
    }

    g_autoptr(GPtrArray) e = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    g_assert_true(e != NULL);
    g_assert_cmpint(e->len, ==, G_N_ELEMENTS(excludes));

    g_autoptr(FsearchDatabaseExcludeManager)
        exclude_manager_copy = fsearch_database_exclude_manager_copy(exclude_manager);
    g_assert_true(fsearch_database_exclude_manager_equal(exclude_manager, exclude_manager_copy));

    fsearch_database_exclude_manager_remove(exclude_manager, g_ptr_array_index(e, 0));
    g_assert_cmpint(e->len, ==, 1);

    g_assert_false(fsearch_database_exclude_manager_equal(exclude_manager, exclude_manager_copy));

    g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_copy(g_ptr_array_index(e, 0));
    fsearch_database_exclude_manager_add(exclude_manager, exclude);
    g_assert_cmpint(e->len, ==, 1);
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/database/exclude", test_database_exclude);
    g_test_add_func("/FSearch/database/exclude_manager", test_database_exclude_manager);
    return g_test_run();
}

#include "fsearch_database_exclude_manager.h"

struct exclude_ctx {
    const char *pattern;
    gboolean active;
    FsearchDatabaseExcludeType type;
    FsearchDatabaseExcludeMatchScope scope;
    FsearchDatabaseExcludeTarget target;
};

static struct exclude_ctx excludes[] = {
    {.pattern = "/home/user_1",
     .active = TRUE,
     .type = FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED,
     .scope = FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
     .target = FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS},
    {.pattern = "*.tmp",
     .active = TRUE,
     .type = FSEARCH_DATABASE_EXCLUDE_TYPE_WILDCARD,
     .scope = FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_BASENAME,
     .target = FSEARCH_DATABASE_EXCLUDE_TARGET_FILES},
    {.pattern = ".*\\.cache$",
     .active = FALSE,
     .type = FSEARCH_DATABASE_EXCLUDE_TYPE_REGEX,
     .scope = FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
     .target = FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH},
};

static void
test_database_exclude() {
    for (guint i = 0; i < G_N_ELEMENTS(excludes); ++i) {
        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(excludes[i].pattern,
                                                                                 excludes[i].active,
                                                                                 excludes[i].type,
                                                                                 excludes[i].scope,
                                                                                 excludes[i].target);
        g_assert_cmpstr(fsearch_database_exclude_get_pattern(exclude), ==, excludes[i].pattern);
        g_assert_cmpint(fsearch_database_exclude_get_active(exclude), ==, excludes[i].active);
        g_assert_cmpint(fsearch_database_exclude_get_exclude_type(exclude), ==, excludes[i].type);
        g_assert_cmpint(fsearch_database_exclude_get_match_scope(exclude), ==, excludes[i].scope);
        g_assert_cmpint(fsearch_database_exclude_get_target(exclude), ==, excludes[i].target);
    }

    g_autoptr(FsearchDatabaseExclude) e1 = fsearch_database_exclude_new(excludes[0].pattern,
                                                                        excludes[0].active,
                                                                        excludes[0].type,
                                                                        excludes[0].scope,
                                                                        excludes[0].target);
    g_autoptr(FsearchDatabaseExclude) e2 = fsearch_database_exclude_new(excludes[1].pattern,
                                                                        excludes[1].active,
                                                                        excludes[1].type,
                                                                        excludes[1].scope,
                                                                        excludes[1].target);
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
        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(excludes[i].pattern,
                                                                                 excludes[i].active,
                                                                                 excludes[i].type,
                                                                                 excludes[i].scope,
                                                                                 excludes[i].target);
        fsearch_database_exclude_manager_add(exclude_manager, exclude);
    }

    g_autoptr(GPtrArray) e = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    g_assert_true(e != NULL);
    g_assert_cmpint(e->len, ==, G_N_ELEMENTS(excludes));

    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager_copy = fsearch_database_exclude_manager_copy(exclude_manager);
    g_assert_true(fsearch_database_exclude_manager_equal(exclude_manager, exclude_manager_copy));

    fsearch_database_exclude_manager_remove(exclude_manager, g_ptr_array_index(e, 0));
    g_assert_cmpint(e->len, ==, G_N_ELEMENTS(excludes) - 1);

    g_assert_false(fsearch_database_exclude_manager_equal(exclude_manager, exclude_manager_copy));

    g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_copy(g_ptr_array_index(e, 0));
    fsearch_database_exclude_manager_add(exclude_manager, exclude);
    g_assert_cmpint(e->len, ==, G_N_ELEMENTS(excludes) - 1);
}

static void
test_database_exclude_matching() {
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();
    fsearch_database_exclude_manager_add(exclude_manager,
                                         fsearch_database_exclude_new("/home/user/build",
                                                                      TRUE,
                                                                      FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED,
                                                                      FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
                                                                      FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS));
    fsearch_database_exclude_manager_add(exclude_manager,
                                         fsearch_database_exclude_new("*.tmp",
                                                                      TRUE,
                                                                      FSEARCH_DATABASE_EXCLUDE_TYPE_WILDCARD,
                                                                      FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_BASENAME,
                                                                      FSEARCH_DATABASE_EXCLUDE_TARGET_FILES));
    fsearch_database_exclude_manager_add(exclude_manager,
                                         fsearch_database_exclude_new(".*\\.(swp|bak)$",
                                                                      TRUE,
                                                                      FSEARCH_DATABASE_EXCLUDE_TYPE_REGEX,
                                                                      FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_BASENAME,
                                                                      FSEARCH_DATABASE_EXCLUDE_TARGET_FILES));
    fsearch_database_exclude_manager_set_exclude_hidden(exclude_manager, TRUE);

    g_assert_true(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/build", "build", TRUE));
    g_assert_false(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/build", "build", FALSE));
    g_assert_true(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/file.tmp", "file.tmp", FALSE));
    g_assert_true(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/foo.swp", "foo.swp", FALSE));
    g_assert_true(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/.gitignore", ".gitignore", FALSE));
    g_assert_false(fsearch_database_exclude_manager_excludes(exclude_manager, "/home/user/file.txt", "file.txt", FALSE));
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/database/exclude", test_database_exclude);
    g_test_add_func("/FSearch/database/exclude_manager", test_database_exclude_manager);
    g_test_add_func("/FSearch/database/exclude_matching", test_database_exclude_matching);
    return g_test_run();
}

#include "fsearch_config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

typedef struct {
    char *config_home;
    char *state_home;
    char *cache_home;
    char *config_dir;
    char *state_dir;
    char *config_path;
    char *state_path;
} TestConfigDirs;

static void
test_config_dirs_init(TestConfigDirs *dirs) {
    g_assert(dirs);

    g_autoptr(GError) error = NULL;

    dirs->config_home = g_dir_make_tmp("fsearch-config-test-config-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dirs->config_home);

    dirs->state_home = g_dir_make_tmp("fsearch-config-test-state-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dirs->state_home);

    dirs->cache_home = g_dir_make_tmp("fsearch-config-test-cache-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dirs->cache_home);
    g_setenv("XDG_CACHE_HOME", dirs->cache_home, TRUE);

    g_setenv("XDG_CONFIG_HOME", dirs->config_home, TRUE);
    g_setenv("XDG_STATE_HOME", dirs->state_home, TRUE);

    dirs->config_dir = g_build_filename(dirs->config_home, "fsearch", NULL);
#if GLIB_CHECK_VERSION(2, 72, 0)
    dirs->state_dir = g_build_filename(dirs->state_home, "fsearch", NULL);
#else
    dirs->state_dir = g_build_filename(dirs->cache_home, "fsearch", NULL);
#endif
    dirs->config_path = g_build_filename(dirs->config_dir, "fsearch.conf", NULL);
    dirs->state_path = g_build_filename(dirs->state_dir, "state.ini", NULL);
}

static void
test_config_dirs_clear(TestConfigDirs *dirs) {
    g_assert(dirs);

    if (dirs->config_path) {
        g_remove(dirs->config_path);
    }
    if (dirs->state_path) {
        g_remove(dirs->state_path);
    }
    if (dirs->config_dir) {
        g_rmdir(dirs->config_dir);
    }
    if (dirs->state_dir) {
        g_rmdir(dirs->state_dir);
    }
    if (dirs->config_home) {
        g_rmdir(dirs->config_home);
    }
    if (dirs->state_home) {
        g_rmdir(dirs->state_home);
    }

    g_rmdir(dirs->cache_home);
    g_clear_pointer(&dirs->cache_home, g_free);

    g_clear_pointer(&dirs->config_home, g_free);
    g_clear_pointer(&dirs->state_home, g_free);
    g_clear_pointer(&dirs->config_dir, g_free);
    g_clear_pointer(&dirs->state_dir, g_free);
    g_clear_pointer(&dirs->config_path, g_free);
    g_clear_pointer(&dirs->state_path, g_free);
}

static void
assert_config_key_present(const char *path, const char *group, const char *key) {
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autoptr(GError) error = NULL;

    const gboolean loaded = g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error);
    g_assert_no_error(error);
    g_assert_true(loaded);
    g_assert_true(g_key_file_has_key(key_file, group, key, NULL));
}

static void
assert_config_key_absent(const char *path, const char *group, const char *key) {
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autoptr(GError) error = NULL;

    const gboolean loaded = g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error);
    g_assert_no_error(error);
    g_assert_true(loaded);
    g_assert_false(g_key_file_has_key(key_file, group, key, NULL));
}

static void
test_config_geometry_round_trip_impl(void) {
    TestConfigDirs dirs = {};
    test_config_dirs_init(&dirs);

    g_assert_true(config_make_dir());

    FsearchConfig *config = g_new0(FsearchConfig, 1);
    g_assert_true(config_load_default(config));

    config->window_width = 1234;
    config->window_height = 567;
    config->name_column_width = 999;
    config->name_column_pos = 4;

    g_assert_true(config_save(config));

    assert_config_key_absent(dirs.config_path, "Interface", "window_width");
    assert_config_key_absent(dirs.config_path, "Interface", "name_column_width");
    assert_config_key_present(dirs.state_path, "Window", "window_width");
    assert_config_key_present(dirs.state_path, "Window", "name_column_width");

    FsearchConfig *loaded = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(loaded));

    g_assert_cmpint(loaded->window_width, ==, 1234);
    g_assert_cmpint(loaded->window_height, ==, 567);
    g_assert_cmpuint(loaded->name_column_width, ==, 999);
    g_assert_cmpuint(loaded->name_column_pos, ==, 4);

    config_free(loaded);
    config_free(config);
    test_config_dirs_clear(&dirs);
}

static void
test_config_geometry_round_trip(void) {
    if (g_test_subprocess()) {
        test_config_geometry_round_trip_impl();
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
}

static void
test_config_geometry_migration_impl(void) {
    TestConfigDirs dirs = {};
    test_config_dirs_init(&dirs);

    g_assert_cmpint(g_mkdir_with_parents(dirs.config_dir, 0700), ==, 0);

    g_autoptr(GKeyFile) legacy_key_file = g_key_file_new();
    g_key_file_set_boolean(legacy_key_file, "Interface", "restore_window_size", TRUE);
    g_key_file_set_integer(legacy_key_file, "Interface", "window_width", 4321);
    g_key_file_set_integer(legacy_key_file, "Interface", "name_column_width", 888);

    g_autoptr(GError) error = NULL;
    const gboolean saved = g_key_file_save_to_file(legacy_key_file, dirs.config_path, &error);
    g_assert_no_error(error);
    g_assert_true(saved);
    g_assert_false(g_file_test(dirs.state_path, G_FILE_TEST_EXISTS));

    FsearchConfig *config = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(config));
    g_assert_cmpint(config->window_width, ==, 4321);
    g_assert_cmpuint(config->name_column_width, ==, 888);

    g_assert_true(config_save(config));

    assert_config_key_present(dirs.state_path, "Window", "window_width");
    assert_config_key_absent(dirs.config_path, "Interface", "window_width");
    assert_config_key_absent(dirs.config_path, "Interface", "name_column_width");

    config_free(config);
    test_config_dirs_clear(&dirs);
}

static void
test_config_geometry_migration(void) {
    if (g_test_subprocess()) {
        test_config_geometry_migration_impl();
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
}

static void
test_config_geometry_state_unwritable_fallback_impl(void) {
    TestConfigDirs dirs = {};
    test_config_dirs_init(&dirs);

    g_assert_true(config_make_dir());

    // Block both state locations so saving must fall back to the main config.
    g_autoptr(GError) blocker_error = NULL;
    g_assert_true(g_file_set_contents(dirs.state_dir, "not a directory", -1, &blocker_error));
    g_assert_no_error(blocker_error);

#if GLIB_CHECK_VERSION(2, 72, 0)
    g_autofree char *cache_dir = g_build_filename(dirs.cache_home, "fsearch", NULL);
    g_assert_true(g_file_set_contents(cache_dir, "not a directory", -1, &blocker_error));
    g_assert_no_error(blocker_error);
#endif

    FsearchConfig *config = g_new0(FsearchConfig, 1);
    g_assert_true(config_load_default(config));

    config->window_width = 1357;
    config->name_column_width = 246;

    // Main save still succeeds even though the state write cannot.
    g_assert_true(config_save(config));
    g_assert_false(g_file_test(dirs.state_path, G_FILE_TEST_EXISTS));

    // Geometry is preserved in the main config file as a fallback.
    assert_config_key_present(dirs.config_path, "Interface", "window_width");
    assert_config_key_present(dirs.config_path, "Interface", "name_column_width");

    // Remove the blocker so the geometry can be loaded back via the migration path.
    g_remove(dirs.state_dir);
#if GLIB_CHECK_VERSION(2, 72, 0)
    g_remove(cache_dir);
#endif

    FsearchConfig *loaded = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(loaded));
    g_assert_cmpint(loaded->window_width, ==, 1357);
    g_assert_cmpuint(loaded->name_column_width, ==, 246);

    config_free(loaded);
    config_free(config);
    test_config_dirs_clear(&dirs);
}

static void
test_config_geometry_state_unwritable_fallback(void) {
    if (g_test_subprocess()) {
        test_config_geometry_state_unwritable_fallback_impl();
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
}

#if GLIB_CHECK_VERSION(2, 72, 0)
static void
test_config_geometry_state_cache_fallback(void) {
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 0, 0);
        g_test_trap_assert_passed();
        return;
    }

    TestConfigDirs dirs = {};
    test_config_dirs_init(&dirs);
    g_assert_true(config_make_dir());
    g_autofree char *cache_dir = g_build_filename(dirs.cache_home, "fsearch", NULL);
    g_autofree char *cache_path = g_build_filename(cache_dir, "state.ini", NULL);

    // A blocked state directory forces the first save and load to use the cache.
    g_assert_true(g_file_set_contents(dirs.state_dir, "not a directory", -1, NULL));
    FsearchConfig *config = g_new0(FsearchConfig, 1);
    g_assert_true(config_load_default(config));
    config->window_width = 1357;
    g_assert_true(config_save(config));
    assert_config_key_present(cache_path, "Window", "window_width");
    assert_config_key_absent(dirs.config_path, "Interface", "window_width");
    FsearchConfig *loaded = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(loaded));
    g_assert_cmpint(loaded->window_width, ==, 1357);
    config_free(loaded);

    // Once state is writable, it takes precedence over the older cached value.
    g_assert_cmpint(g_remove(dirs.state_dir), ==, 0);
    config->window_width = 2468;
    g_assert_true(config_save(config));
    assert_config_key_present(dirs.state_path, "Window", "window_width");
    loaded = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(loaded));
    g_assert_cmpint(loaded->window_width, ==, 2468);
    config_free(loaded);

    // Invalid state content also falls back to the cache.
    g_assert_true(g_file_set_contents(dirs.state_path, "invalid key file", -1, NULL));
    loaded = g_new0(FsearchConfig, 1);
    g_assert_true(config_load(loaded));
    g_assert_cmpint(loaded->window_width, ==, 1357);
    config_free(loaded);
    config_free(config);
    g_remove(cache_path);
    g_rmdir(cache_dir);
    test_config_dirs_clear(&dirs);
}
#endif

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/config/geometry_round_trip", test_config_geometry_round_trip);
    g_test_add_func("/FSearch/config/geometry_migration", test_config_geometry_migration);
    g_test_add_func("/FSearch/config/geometry_state_unwritable_fallback", test_config_geometry_state_unwritable_fallback);
#if GLIB_CHECK_VERSION(2, 72, 0)
    g_test_add_func("/FSearch/config/geometry_state_cache_fallback", test_config_geometry_state_cache_fallback);
#endif
    return g_test_run();
}

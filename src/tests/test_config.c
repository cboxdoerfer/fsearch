#include <glib.h>

#include "fsearch_config.h"

// define how to free a FsearchConfig pointer
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchConfig, config_free)

static void
test_config_cmp_non_destructive_changes(void) {
    // Setup: Create a default config (c1) and a perfect copy (c2)
    g_autoptr(FsearchConfig) c1 = g_new0(FsearchConfig, 1);
    config_load_default(c1);
    g_autoptr(FsearchConfig) c2 = config_copy(c1);

    // Test 1: Two identical configs should report no changes.
    // This is a sanity check to ensure our baseline is correct.
    FsearchConfigCompareResult result = config_cmp(c1, c2);
    g_assert_false(result.database_config_changed);
    g_assert_false(result.listview_config_changed);
    g_assert_false(result.search_config_changed);
    g_clear_pointer(&c2, config_free);

    // Test 2: Changing only the diff_tool_cmd should trigger a listview_config_changed,
    // and MUST NOT trigger a database_config_changed. This is the core test for the feature.
    c2 = config_copy(c1);
    g_free(c2->diff_tool_cmd);
    c2->diff_tool_cmd = g_strdup("meld");

    result = config_cmp(c1, c2);
    g_assert_false(result.database_config_changed);
    g_assert_true(result.listview_config_changed);
    g_assert_false(result.search_config_changed);
    g_clear_pointer(&c2, config_free);

    // Test 3: Changing only the folder_open_cmd should also trigger a listview_config_changed.
    c2 = config_copy(c1);
    g_free(c2->folder_open_cmd);
    c2->folder_open_cmd = g_strdup("thunar");

    result = config_cmp(c1, c2);
    g_assert_false(result.database_config_changed);
    g_assert_true(result.listview_config_changed);
    g_assert_false(result.search_config_changed);
    g_clear_pointer(&c2, config_free);
}

static void
test_config_cmp_destructive_change(void) {
    g_autoptr(FsearchConfig) c1 = g_new0(FsearchConfig, 1);
    config_load_default(c1);
    g_autoptr(FsearchConfig) c2 = config_copy(c1);

    // Test: Changing a database-related setting like 'exclude_hidden_items'
    // MUST trigger a database_config_changed. This is a control test to
    // prove that our logic correctly distinguishes between change types.
    c2->exclude_hidden_items = !c1->exclude_hidden_items;

    FsearchConfigCompareResult result = config_cmp(c1, c2);
    g_assert_true(result.database_config_changed);
    g_assert_false(result.listview_config_changed);
    g_assert_false(result.search_config_changed);
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/FSearch/config/cmp_non_destructive", test_config_cmp_non_destructive_changes);
    g_test_add_func("/FSearch/config/cmp_destructive", test_config_cmp_destructive_change);

    return g_test_run();
}

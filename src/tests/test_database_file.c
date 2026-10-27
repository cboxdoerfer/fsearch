/*
 * Verifies fsearch_database_file_save()/_load() still correctly round-trip parent/child
 * relationships and non-NAME sort orders now that FsearchDatabaseEntry no longer carries a
 * dedicated `index` field.
 *
 * That field used to record each entry's position in the canonical (NAME-order) folders/files
 * array purely as save-time bookkeeping: database_file_save_folders()/_files() use it to write
 * each entry's parent as a stable on-disk array index, and database_file_save_sorted_arrays()
 * uses it to write, for every other fast-sort order (PATH/SIZE/MTIME/EXTENSION), a permutation
 * back into that canonical order. It was never read anywhere outside of saving.
 *
 * Since it cost every entry 8 bytes (4 for the field, 4 more from the alignment padding it forced
 * on FsearchDatabaseEntry) for the entire lifetime of the app just to support this one save-time
 * lookup, it's replaced by build_entry_index_map(): a flat array of (entry pointer, index) pairs,
 * built fresh for each save, sorted once by pointer value, and binary-searched -- no permanent
 * per-entry storage, and no GHashTable (which would be real overhead to populate with millions of
 * pointer keys).
 *
 * This test exercises both lookup call sites that map replaced: saving/loading a small file tree
 * with a subdirectory (so files have two different parents) and multiple sizes (so the SIZE-order
 * permutation differs from NAME-order), then checking that both survive a save+load roundtrip.
 */

#include "fsearch_database_entry.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_file.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static void
write_file(const char *path, const char *content) {
    g_assert_true(g_file_set_contents(path, content, -1, NULL));
}

static void
test_save_load_roundtrip_preserves_hierarchy_and_sort_orders(void) {
    g_autofree char *tmp_dir = g_dir_make_tmp("fsearch-test-database-file-XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);

    g_autofree char *subdir = g_build_filename(tmp_dir, "subdir", NULL);
    g_assert_cmpint(g_mkdir(subdir, 0755), ==, 0);

    // Sizes deliberately don't match name order: SIZE order is [c, b, a], NAME order is [a, b, c].
    g_autofree char *file_a = g_build_filename(tmp_dir, "a.txt", NULL);
    g_autofree char *file_b = g_build_filename(subdir, "b.txt", NULL);
    g_autofree char *file_c = g_build_filename(subdir, "c.txt", NULL);
    write_file(file_a, "aaaaaaaaaaaa"); // 12 bytes, largest
    write_file(file_b, "bb");           // 2 bytes
    write_file(file_c, "c");            // 1 byte, smallest

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseInclude) include = fsearch_database_include_new(tmp_dir, TRUE, FALSE, FALSE, FALSE, 0);
    fsearch_database_include_manager_add(include_manager, include);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    const FsearchDatabaseIndexPropertyFlags flags = DATABASE_INDEX_PROPERTY_FLAG_NAME
                                                   | DATABASE_INDEX_PROPERTY_FLAG_PATH
                                                   | DATABASE_INDEX_PROPERTY_FLAG_SIZE
                                                   | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME;

    FsearchDatabaseIndexStore *store =
        fsearch_database_index_store_new(include_manager, exclude_manager, flags, NULL, NULL);
    fsearch_database_index_store_start(store, NULL);

    g_assert_cmpuint(fsearch_database_index_store_get_num_files(store), ==, 3);
    // The include root itself and "subdir" -- 2 folders.
    g_assert_cmpuint(fsearch_database_index_store_get_num_folders(store), ==, 2);

    // Capture every live entry's real parent pointer before saving, so we can confirm below that
    // save() put them back afterward. fsearch_database_file_save() temporarily repurposes each
    // entry's `parent` field as scratch storage for its own canonical (NAME-order) index (see
    // database_file_encode_indices() in fsearch_database_file.c); if it failed to restore the real
    // pointers on every exit path, `store` -- which is still live and gets searched/sorted/saved
    // again after this, not thrown away -- would be left corrupted.
    g_autoptr(FsearchDatabaseChunkedArray) name_sorted_files_before_save =
        fsearch_database_index_store_get_files(store, DATABASE_INDEX_PROPERTY_NAME);
    g_autoptr(FsearchDatabaseChunkedArray) name_sorted_folders_before_save =
        fsearch_database_index_store_get_folders(store, DATABASE_INDEX_PROPERTY_NAME);
    g_assert_nonnull(name_sorted_files_before_save);
    g_assert_nonnull(name_sorted_folders_before_save);

    FsearchDatabaseEntry *files_before_save[3];
    FsearchDatabaseEntry *file_parents_before_save[3];
    off_t file_sizes_before_save[3];
    time_t file_mtimes_before_save[3];
    for (uint32_t i = 0; i < 3; i++) {
        files_before_save[i] = fsearch_database_chunked_array_get_entry(name_sorted_files_before_save, i);
        file_parents_before_save[i] = db_entry_get_parent(files_before_save[i]);
        file_sizes_before_save[i] = db_entry_get_size(files_before_save[i]);
        file_mtimes_before_save[i] = db_entry_get_mtime(files_before_save[i]);
        // Sanity: the live store must actually have real values to round-trip.
        g_assert_cmpint(file_sizes_before_save[i], >, 0);
        g_assert_cmpint(file_mtimes_before_save[i], >, 0);
    }
    FsearchDatabaseEntry *folders_before_save[2];
    FsearchDatabaseEntry *folder_parents_before_save[2];
    for (uint32_t i = 0; i < 2; i++) {
        folders_before_save[i] = fsearch_database_chunked_array_get_entry(name_sorted_folders_before_save, i);
        folder_parents_before_save[i] = db_entry_get_parent(folders_before_save[i]);
    }

    g_autofree char *db_path = g_build_filename(tmp_dir, "test.db", NULL);
    g_assert_true(fsearch_database_file_save(store, db_path));

    // The live store's entries must be untouched from the caller's perspective: same parents as
    // before the save.
    for (uint32_t i = 0; i < 3; i++) {
        g_assert_true(db_entry_get_parent(files_before_save[i]) == file_parents_before_save[i]);
    }
    for (uint32_t i = 0; i < 2; i++) {
        g_assert_true(db_entry_get_parent(folders_before_save[i]) == folder_parents_before_save[i]);
    }

    fsearch_database_index_store_unref(store);

    g_autoptr(FsearchDatabaseIndexStore) loaded_store = NULL;
    g_assert_true(fsearch_database_file_load(db_path,
                                             NULL,
                                             &loaded_store,
                                             include_manager,
                                             exclude_manager,
                                             NULL,
                                             NULL));
    g_assert_nonnull(loaded_store);

    g_assert_cmpuint(fsearch_database_index_store_get_num_files(loaded_store), ==, 3);
    g_assert_cmpuint(fsearch_database_index_store_get_num_folders(loaded_store), ==, 2);

    // Parent/child relationships: exercises the parent-index lookups used when saving each entry.
    g_autoptr(FsearchDatabaseChunkedArray) name_sorted_files =
        fsearch_database_index_store_get_files(loaded_store, DATABASE_INDEX_PROPERTY_NAME);
    g_assert_nonnull(name_sorted_files);
    for (uint32_t i = 0; i < 3; i++) {
        FsearchDatabaseEntry *entry = fsearch_database_chunked_array_get_entry(name_sorted_files, i);
        const char *name = db_entry_get_name_raw(entry);
        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        g_assert_nonnull(parent);
        if (!strcmp(name, "a.txt")) {
            // a.txt's parent is the include root, which is "subdir"'s parent, not "subdir" itself.
            g_assert_cmpstr(db_entry_get_name_raw(parent), !=, "subdir");
        }
        else {
            g_assert_cmpstr(db_entry_get_name_raw(parent), ==, "subdir");
        }
        // Exact size + mtime must survive the roundtrip (files are in NAME order both times).
        g_assert_cmpint(db_entry_get_size(entry), ==, file_sizes_before_save[i]);
        g_assert_cmpint(db_entry_get_mtime(entry), ==, file_mtimes_before_save[i]);
    }

    // Non-NAME sort order: exercises the self-index lookups used to write each fast-sort order's
    // permutation back into canonical (NAME-order) positions.
    g_autoptr(FsearchDatabaseChunkedArray) size_sorted_files =
        fsearch_database_index_store_get_files(loaded_store, DATABASE_INDEX_PROPERTY_SIZE);
    g_assert_nonnull(size_sorted_files);
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(size_sorted_files, 0)), ==, "c.txt");
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(size_sorted_files, 1)), ==, "b.txt");
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(size_sorted_files, 2)), ==, "a.txt");

    g_unlink(file_a);
    g_unlink(file_b);
    g_unlink(file_c);
    g_unlink(db_path);
    g_rmdir(subdir);
    g_rmdir(tmp_dir);
}

// Regression test for the "all results show 1969-12-31 19:00 / 0 bytes" bug.
//
// A database file whose header `index_flags` is missing the stored data properties (size, mtime)
// -- e.g. one written with a zero flag set -- used to load successfully: entries were built
// without those attributes, so db_entry_get_mtime()/db_entry_get_size() returned 0, and the next
// save re-persisted the zero flags, keeping the database broken forever.
//
// This test builds such a corrupt-by-construction file (a valid checksum, header index_flags == 0,
// entries lacking size/mtime bytes) by saving a store created with FLAG_NONE, then asserts that
// fsearch_database_file_load() now refuses it instead of silently surfacing all-zero values. A
// healthy store (FLAG_DEFAULT) must still load successfully.
static void
test_load_rejects_file_missing_required_data_flags(void) {
    g_autofree char *tmp_dir = g_dir_make_tmp("fsearch-test-database-file-XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);

    g_autofree char *file_a = g_build_filename(tmp_dir, "a.txt", NULL);
    write_file(file_a, "aaaaaaaaaaaa"); // 12 bytes

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseInclude) include = fsearch_database_include_new(tmp_dir, TRUE, FALSE, FALSE, FALSE, 0);
    fsearch_database_include_manager_add(include_manager, include);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    // Healthy store: the app's default property set. Save + load must still work (no regression).
    FsearchDatabaseIndexStore *good_store =
        fsearch_database_index_store_new(include_manager, exclude_manager,
                                         DATABASE_INDEX_PROPERTY_FLAG_DEFAULT, NULL, NULL);
    fsearch_database_index_store_start(good_store, NULL);
    g_assert_cmpuint(fsearch_database_index_store_get_num_files(good_store), ==, 1);
    g_autofree char *good_path = g_build_filename(tmp_dir, "good.db", NULL);
    g_assert_true(fsearch_database_file_save(good_store, good_path));
    fsearch_database_index_store_unref(good_store);

    g_autoptr(FsearchDatabaseIndexStore) loaded_good = NULL;
    g_assert_true(fsearch_database_file_load(good_path,
                                             NULL,
                                             &loaded_good,
                                             include_manager,
                                             exclude_manager,
                                             NULL,
                                             NULL));
    g_assert_nonnull(loaded_good);
    g_assert_cmpuint(fsearch_database_index_store_get_num_files(loaded_good), ==, 1);
    g_assert_cmpint(fsearch_database_index_store_get_flags(loaded_good), ==,
                    DATABASE_INDEX_PROPERTY_FLAG_DEFAULT);

    // Corrupt store: created with a zero flag set, as before the flags were defaulted. Saving it
    // writes header index_flags == 0 with a valid checksum and entries that store no size/mtime.
    FsearchDatabaseIndexStore *bad_store = fsearch_database_index_store_new(include_manager,
                                                                            exclude_manager,
                                                                            DATABASE_INDEX_PROPERTY_FLAG_NONE,
                                                                            NULL,
                                                                            NULL);
    fsearch_database_index_store_start(bad_store, NULL);
    g_autofree char *bad_path = g_build_filename(tmp_dir, "bad.db", NULL);
    g_assert_true(fsearch_database_file_save(bad_store, bad_path));
    fsearch_database_index_store_unref(bad_store);

    // Sanity: the corrupt file really has a zero index_flags in its header (offset 7, 8 bytes LE)
    // even though its checksum is valid -- i.e. it is indistinguishable from a healthy file by
    // checksum alone.
    {
        g_autoptr(GError) read_error = NULL;
        g_autofree char *raw = NULL;
        gsize len = 0;
        g_assert_true(g_file_get_contents(bad_path, &raw, &len, &read_error));
        g_assert_cmpuint(len, >=, 7 + sizeof(uint64_t));
        uint64_t header_index_flags = 0;
        memcpy(&header_index_flags, raw + 7, sizeof(uint64_t));
        g_assert_cmpuint(header_index_flags, ==, 0);
    }

    // The fix: a zero-flag file must be rejected so the caller can rescan from disk and repair it,
    // rather than being loaded with all-zero size/mtime. g_test_init() treats g_warning() as fatal,
    // so expect exactly one "corrupt file" warning before the load and assert it fired afterward.
    g_test_expect_message("fsearch-database-file", G_LOG_LEVEL_WARNING, "*missing required data properties*");
    g_autoptr(FsearchDatabaseIndexStore) loaded_bad = NULL;
    g_assert_false(fsearch_database_file_load(bad_path,
                                              NULL,
                                              &loaded_bad,
                                              include_manager,
                                              exclude_manager,
                                              NULL,
                                              NULL));
    g_assert_null(loaded_bad);
    g_test_assert_expected_messages();

    g_unlink(file_a);
    g_unlink(good_path);
    g_unlink(bad_path);
    g_rmdir(tmp_dir);
}

int
main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/FSearch/database/file/save_load_roundtrip_preserves_hierarchy_and_sort_orders",
                    test_save_load_roundtrip_preserves_hierarchy_and_sort_orders);
    g_test_add_func("/FSearch/database/file/load_rejects_file_missing_required_data_flags",
                    test_load_rejects_file_missing_required_data_flags);

    return g_test_run();
}

/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Extensive test suite for FsearchDatabaseChunkedArray.
 *
 * Covers every public function declared in fsearch_database_chunked_array.h:
 *   - fsearch_database_chunked_array_new
 *   - fsearch_database_chunked_array_ref / _unref
 *   - fsearch_database_chunked_array_insert
 *   - fsearch_database_chunked_array_insert_array
 *   - fsearch_database_chunked_array_steal
 *   - fsearch_database_chunked_array_find_slow
 *   - fsearch_database_chunked_array_steal_descendants
 *   - fsearch_database_chunked_array_remove_marked_folders
 *   - fsearch_database_chunked_array_steal_marked_folders
 *   - fsearch_database_chunked_array_find
 *   - fsearch_database_chunked_array_get_entry
 *   - fsearch_database_chunked_array_get_num_entries
 *   - fsearch_database_chunked_array_get_chunks
 *   - fsearch_database_chunked_array_get_joined
 *   - fsearch_database_chunked_array_get_type
 *
 * Not exercised: fsearch_database_chunked_array_balance is declared in the header but has
 * no definition anywhere in fsearch_database_chunked_array.c, so calling it would fail to link.
 *
 * The suite pays particular attention to chunk balancing: chunks are implemented with
 * TARGET_CHUNK_SIZE == 2048 and only split once a chunk reaches >= 2 * TARGET_CHUNK_SIZE
 * (4096) entries via single fsearch_database_chunked_array_insert() calls, whereas
 * construction (fsearch_database_chunked_array_new) and the bulk-merge path of
 * fsearch_database_chunked_array_insert_array() split any chunk that's already above the
 * plain TARGET_CHUNK_SIZE. There is also no logic anywhere that merges two small,
 * non-empty chunks back together - chunks only ever disappear once they become fully
 * empty. Several tests below exist specifically to pin down these asymmetries.
 * These constants are mirrored below (as TEST_TARGET_CHUNK_SIZE and TEST_MIN_BULK_INSERT)
 * purely to size test data appropriately; the implementation itself is never touched
 * directly.
 */

#include "fsearch_array.h"
#include "fsearch_database_chunked_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_sort.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define TEST_TARGET_CHUNK_SIZE 2048
#define TEST_MIN_BULK_INSERT 8192

/* ------------------------------------------------------------------------ *
 * Helpers
 * ------------------------------------------------------------------------ */

static FsearchDatabaseEntry *
make_file(const char *name) {
    return db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_NONE, name, NULL, DATABASE_ENTRY_TYPE_FILE);
}

static FsearchDatabaseEntry *
make_folder(const char *name, FsearchDatabaseEntry *parent) {
    return db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_NONE, name, parent, DATABASE_ENTRY_TYPE_FOLDER);
}

static FsearchDatabaseEntry *
make_file_in(const char *name, FsearchDatabaseEntry *parent) {
    return db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_NONE, name, parent, DATABASE_ENTRY_TYPE_FILE);
}

static FsearchDatabaseEntry *
make_file_with_size(const char *name, off_t size) {
    FsearchDatabaseEntry *e = db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_SIZE, name, NULL, DATABASE_ENTRY_TYPE_FILE);
    db_entry_set_size(e, size);
    return e;
}

// Builds `count` uniquely named file entries ("prefix_000000" .. "prefix_{count-1}"),
// already in ascending name order.
static DynamicArray *
make_sorted_files(const char *prefix, uint32_t count) {
    DynamicArray *array = darray_new(count);
    for (uint32_t i = 0; i < count; i++) {
        g_autofree char *name = g_strdup_printf("%s_%06u", prefix, i);
        darray_add_item(array, make_file(name));
    }
    return array;
}

// Builds `count` uniquely named file entries in a deterministic non-sorted order, using a
// stride coprime with `count` to produce a repeatable permutation of [0, count).
static DynamicArray *
make_shuffled_files(const char *prefix, uint32_t count, uint32_t stride) {
    DynamicArray *array = darray_new(count);
    uint32_t idx = 0;
    for (uint32_t i = 0; i < count; i++) {
        g_autofree char *name = g_strdup_printf("%s_%06u", prefix, idx);
        darray_add_item(array, make_file(name));
        idx = (idx + stride) % count;
    }
    return array;
}

static FsearchDatabaseChunkedArray *
make_chunked_array(DynamicArray *array,
                   gboolean is_sorted,
                   FsearchDatabaseIndexProperty sort_order,
                   FsearchDatabaseEntryType type,
                   GDestroyNotify free_func) {
    return fsearch_database_chunked_array_new(array,
                                              is_sorted,
                                              fsearch_database_sort_order_chain_for_property(sort_order),
                                              type,
                                              NULL,
                                              free_func);
}

static void
assert_sorted_by_property(FsearchDatabaseChunkedArray *array, FsearchDatabaseIndexProperty property) {
    FsearchDatabaseSortOrderChain chain = fsearch_database_sort_order_chain_for_property(property);
    g_autoptr(FsearchDatabaseEntryCompareContext) ctx = db_entry_compare_context_new(chain);
    const uint32_t n = fsearch_database_chunked_array_get_num_entries(array);
    FsearchDatabaseEntry *prev = NULL;
    for (uint32_t i = 0; i < n; i++) {
        FsearchDatabaseEntry *cur = fsearch_database_chunked_array_get_entry(array, i);
        g_assert_nonnull(cur);
        if (prev) {
            g_assert_cmpint(db_entry_compare_entries_by_chain((void *)&prev, (void *)&cur, ctx), <=, 0);
        }
        prev = cur;
    }
}

static uint32_t
num_chunks(FsearchDatabaseChunkedArray *array) {
    DynamicArray *chunks = fsearch_database_chunked_array_get_chunks(array);
    const uint32_t n = darray_get_num_items(chunks);
    darray_unref(chunks);
    return n;
}

static void
free_stolen(DynamicArray *stolen) {
    if (!stolen) {
        return;
    }
    darray_set_free_func(stolen, (GDestroyNotify)db_entry_free_no_unparent);
    darray_unref(stolen);
}

// g_test_init() makes g_critical()/g_warning() fatal by default. Several edge cases below
// deliberately trigger a g_return_if_fail()/g_return_val_if_fail() guard (from
// fsearch_database_chunked_array.c's own "fsearch-database-chunked-array" log domain) to
// verify the implementation fails gracefully instead of corrupting state; this tells the
// test harness to expect exactly one such message so it doesn't abort the whole binary.
static void
expect_one_critical(void) {
    g_test_expect_message("fsearch-database-chunked-array", G_LOG_LEVEL_CRITICAL, "*");
}

/* ------------------------------------------------------------------------ *
 * Construction & lifecycle
 * ------------------------------------------------------------------------ */

static void
test_new_empty_array(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 0);
    // Construction always leaves exactly one (possibly empty) chunk behind.
    g_assert_cmpuint(num_chunks(arr), ==, 1);
    expect_one_critical();
    g_assert_null(fsearch_database_chunked_array_get_entry(arr, 0));
    g_test_assert_expected_messages();
}

static void
test_new_small_array_single_chunk(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 100);
    g_assert_cmpuint(num_chunks(arr), ==, 1);
}

static void
test_new_large_array_splits_at_construction(void) {
    // 5000 > TARGET_CHUNK_SIZE, so construction must split it into multiple chunks
    // right away (unlike single inserts, which tolerate up to 2 * TARGET_CHUNK_SIZE).
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5000);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 5000);
    g_assert_cmpuint(num_chunks(arr), >, 1);

    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
}

static void
test_new_unsorted_input_gets_sorted(void) {
    g_autoptr(DynamicArray) input = make_shuffled_files("f", 500, 7);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE, // is_array_sorted
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 500);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
}

static void
test_new_claiming_sorted_when_not_is_trusted(void) {
    // If the caller lies about is_array_sorted, fsearch_database_chunked_array_new() trusts
    // it and skips sorting entirely. Documents that the resulting array preserves input
    // order verbatim instead of raising an error.
    g_autoptr(DynamicArray) input = make_shuffled_files("f", 50, 7);

    // Keep a copy of the pre-shuffle pointer order to compare against afterwards.
    FsearchDatabaseEntry *expected_order[50];
    for (uint32_t i = 0; i < 50; i++) {
        expected_order[i] = darray_get_item(input, i);
    }

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE, // lie: claim it's sorted
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    for (uint32_t i = 0; i < 50; i++) {
        g_assert_true(fsearch_database_chunked_array_get_entry(arr, i) == expected_order[i]);
    }
}

static void
test_ref_unref_roundtrip(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 10);
    FsearchDatabaseChunkedArray *arr = make_chunked_array(input,
                                                          TRUE,
                                                          DATABASE_INDEX_PROPERTY_NAME,
                                                          DATABASE_ENTRY_TYPE_FILE,
                                                          (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseChunkedArray *second = fsearch_database_chunked_array_ref(arr);
    g_assert_true(second == arr);

    // Dropping one ref must not free the array.
    fsearch_database_chunked_array_unref(second);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 10);

    // The final unref triggers the destructor.
    fsearch_database_chunked_array_unref(arr);
}

static void
test_get_type_is_registered(void) {
    g_assert_cmpuint(fsearch_database_chunked_array_get_type(), !=, 0);
}

/* ------------------------------------------------------------------------ *
 * get_num_entries / get_entry / get_chunks / get_joined
 * ------------------------------------------------------------------------ */

static void
test_get_entry_out_of_bounds_returns_null(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 10);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    expect_one_critical();
    g_assert_null(fsearch_database_chunked_array_get_entry(arr, 10));
    g_test_assert_expected_messages();

    expect_one_critical();
    g_assert_null(fsearch_database_chunked_array_get_entry(arr, UINT32_MAX));
    g_test_assert_expected_messages();
}

static void
test_get_entry_first_and_last(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 10);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 0)), ==, "f_000000");
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 9)), ==, "f_000009");
}

static void
test_get_entry_crosses_chunk_boundary(void) {
    // 5000 entries split into multiple chunks at construction (see split_chunk's ceil/floor
    // math): verify sequential get_entry() calls stay in sorted order right across whichever
    // chunk boundary that produces, without knowing the exact split point in advance.
    const uint32_t count = 5000;
    g_autoptr(DynamicArray) input = make_sorted_files("f", count);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(num_chunks(arr), >, 1);
    for (uint32_t i = 0; i < count; i++) {
        g_autofree char *expected = g_strdup_printf("f_%06u", i);
        g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, i)), ==, expected);
    }
}

static void
test_get_chunks_total_matches_num_entries(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5000);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) chunks = fsearch_database_chunked_array_get_chunks(arr);
    uint32_t total = 0;
    for (uint32_t i = 0; i < darray_get_num_items(chunks); i++) {
        total += darray_get_num_items(darray_get_item(chunks, i));
    }
    g_assert_cmpuint(total, ==, fsearch_database_chunked_array_get_num_entries(arr));
}

static void
test_get_joined_matches_sequential_get_entry(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 3000);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) joined = fsearch_database_chunked_array_get_joined(arr);
    g_assert_cmpuint(darray_get_num_items(joined), ==, fsearch_database_chunked_array_get_num_entries(arr));
    for (uint32_t i = 0; i < darray_get_num_items(joined); i++) {
        g_assert_true(darray_get_item(joined, i) == fsearch_database_chunked_array_get_entry(arr, i));
    }
}

/* ------------------------------------------------------------------------ *
 * insert (single item) & balancing
 * ------------------------------------------------------------------------ */

static void
test_insert_into_empty_array(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    fsearch_database_chunked_array_insert(arr, make_file("a"));
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 0)), ==, "a");
}

static void
test_insert_maintains_sort_order(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) to_insert = make_shuffled_files("f", 500, 7);
    for (uint32_t i = 0; i < darray_get_num_items(to_insert); i++) {
        fsearch_database_chunked_array_insert(arr, darray_get_item(to_insert, i));
    }
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 500);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
}

static void
test_insert_does_not_split_below_2x_threshold(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    // Insert in descending order (always at index 0) to also stress the insertion path.
    for (uint32_t i = 0; i < 2 * TEST_TARGET_CHUNK_SIZE - 1; i++) {
        g_autofree char *name = g_strdup_printf("f_%06u", 2 * TEST_TARGET_CHUNK_SIZE - 1 - i);
        fsearch_database_chunked_array_insert(arr, make_file(name));
    }
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 2 * TEST_TARGET_CHUNK_SIZE - 1);
    g_assert_cmpuint(num_chunks(arr), ==, 1);
}

static void
test_insert_splits_at_2x_threshold(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    for (uint32_t i = 0; i < 2 * TEST_TARGET_CHUNK_SIZE; i++) {
        g_autofree char *name = g_strdup_printf("f_%06u", i);
        fsearch_database_chunked_array_insert(arr, make_file(name));
    }
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 2 * TEST_TARGET_CHUNK_SIZE);
    // Crossing >= 2 * TARGET_CHUNK_SIZE must have triggered a split.
    g_assert_cmpuint(num_chunks(arr), >, 1);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
}

static void
test_insert_wrong_entry_type_is_rejected(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *folder = make_folder("dir", NULL);
    // g_return_if_fail(): must silently no-op instead of corrupting the array.
    expect_one_critical();
    fsearch_database_chunked_array_insert(arr, folder);
    g_test_assert_expected_messages();
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 5);
    db_entry_free(folder);
}

static void
test_insert_with_size_sort_order(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_SIZE,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    const off_t sizes[] = {500, 10, 9000, 1, 42};
    for (size_t i = 0; i < G_N_ELEMENTS(sizes); i++) {
        g_autofree char *name = g_strdup_printf("f_%zu", i);
        fsearch_database_chunked_array_insert(arr, make_file_with_size(name, sizes[i]));
    }
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_SIZE);
    g_assert_cmpint(db_entry_get_size(fsearch_database_chunked_array_get_entry(arr, 0)), ==, 1);
    g_assert_cmpint(db_entry_get_size(fsearch_database_chunked_array_get_entry(arr, 4)), ==, 9000);
}

/* ------------------------------------------------------------------------ *
 * insert_array (bulk)
 * ------------------------------------------------------------------------ */

static void
test_insert_array_empty_is_noop(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 10);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) empty = darray_new(0);
    fsearch_database_chunked_array_insert_array(arr, empty);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 10);
}

static void
test_insert_array_small_uses_individual_path(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("a", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    // "b" prefix sorts after "a" prefix, well under TEST_MIN_BULK_INSERT: exercises the
    // per-item insertion loop rather than the bulk merge-rebuild path.
    g_autoptr(DynamicArray) more = make_shuffled_files("b", 200, 11);
    fsearch_database_chunked_array_insert_array(arr, more);

    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 300);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
    // All "a" entries must still sort before all "b" entries.
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 99)), ==, "a_000099");
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 100)), ==, "b_000000");
}

static void
test_insert_array_bulk_path_at_threshold(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("a", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) more = make_shuffled_files("b", TEST_MIN_BULK_INSERT, 17);
    fsearch_database_chunked_array_insert_array(arr, more);

    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 100 + TEST_MIN_BULK_INSERT);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);

    // Nothing lost or duplicated: every "b" name from 0..MIN_BULK_INSERT-1 must be findable.
    for (uint32_t i = 0; i < TEST_MIN_BULK_INSERT; i += 777) {
        g_autofree char *name = g_strdup_printf("b_%06u", i);
        FsearchDatabaseEntry *query = make_file(name);
        g_assert_nonnull(fsearch_database_chunked_array_find(arr, query));
        db_entry_free(query);
    }
}

static void
test_insert_array_bulk_path_interleaves_correctly(void) {
    // Even-numbered names already in the array, odd-numbered names inserted via the bulk
    // path: the merge logic must interleave them back into a single, fully sorted sequence.
    g_autoptr(DynamicArray) evens = darray_new(TEST_MIN_BULK_INSERT);
    for (uint32_t i = 0; i < TEST_MIN_BULK_INSERT; i++) {
        g_autofree char *name = g_strdup_printf("f_%06u", 2 * i);
        darray_add_item(evens, make_file(name));
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(evens,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);

    g_autoptr(DynamicArray) odds = darray_new(TEST_MIN_BULK_INSERT);
    for (uint32_t i = 0; i < TEST_MIN_BULK_INSERT; i++) {
        g_autofree char *name = g_strdup_printf("f_%06u", 2 * i + 1);
        darray_add_item(odds, make_file(name));
    }
    fsearch_database_chunked_array_insert_array(arr, odds);

    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 2 * TEST_MIN_BULK_INSERT);
    for (uint32_t i = 0; i < 2 * TEST_MIN_BULK_INSERT; i += 555) {
        g_autofree char *expected = g_strdup_printf("f_%06u", i);
        g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, i)), ==, expected);
    }
}

static void
test_insert_array_bulk_path_into_empty_self(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) many = make_shuffled_files("f", TEST_MIN_BULK_INSERT, 13);
    fsearch_database_chunked_array_insert_array(arr, many);

    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, TEST_MIN_BULK_INSERT);
    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
}

/* ------------------------------------------------------------------------ *
 * find / find_slow
 * ------------------------------------------------------------------------ */

static void
test_find_existing_entry(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 3000);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *query = make_file("f_002500");
    FsearchDatabaseEntry *found = fsearch_database_chunked_array_find(arr, query);
    g_assert_nonnull(found);
    g_assert_cmpstr(db_entry_get_name_raw(found), ==, "f_002500");
    db_entry_free(query);
}

static void
test_find_missing_entry_returns_null(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *query = make_file("does_not_exist");
    g_assert_null(fsearch_database_chunked_array_find(arr, query));
    db_entry_free(query);
}

static void
test_find_empty_array_returns_null(void) {
    g_autoptr(DynamicArray) input = darray_new(0);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);
    FsearchDatabaseEntry *query = make_file("anything");
    g_assert_null(fsearch_database_chunked_array_find(arr, query));
    db_entry_free(query);
}

static void
test_find_slow_existing_and_missing(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *hit_query = make_file("f_000042");
    g_assert_nonnull(fsearch_database_chunked_array_find_slow(arr, hit_query));
    db_entry_free(hit_query);

    FsearchDatabaseEntry *miss_query = make_file("nope");
    g_assert_null(fsearch_database_chunked_array_find_slow(arr, miss_query));
    db_entry_free(miss_query);
}

static void
test_find_duplicate_keys_returns_a_matching_entry(void) {
    // Parentless entries with an identical name are genuinely tied under NAME sort order
    // (the tie-break in compare_by_name() falls back to compare_by_path(), which considers
    // two parentless entries equal). find() may return any of them.
    g_autoptr(DynamicArray) input = darray_new(10);
    for (uint32_t i = 0; i < 10; i++) {
        darray_add_item(input, make_file("dup"));
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *query = make_file("dup");
    FsearchDatabaseEntry *found = fsearch_database_chunked_array_find(arr, query);
    g_assert_nonnull(found);
    g_assert_cmpstr(db_entry_get_name_raw(found), ==, "dup");
    db_entry_free(query);
}

static void
test_find_with_duplicates_spanning_chunk_boundary(void) {
    // More duplicate-keyed entries than fit in a single chunk, forcing the tied block to
    // straddle a real chunk boundary. Binary search across chunks (chunk_compare_func) must
    // still be able to locate a match.
    const uint32_t dup_count = TEST_TARGET_CHUNK_SIZE + 500;
    g_autoptr(DynamicArray) input = darray_new(dup_count + 2);
    darray_add_item(input, make_file("aaa_before"));
    for (uint32_t i = 0; i < dup_count; i++) {
        darray_add_item(input, make_file("dup"));
    }
    darray_add_item(input, make_file("zzz_after"));

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(num_chunks(arr), >, 1);

    FsearchDatabaseEntry *dup_query = make_file("dup");
    g_assert_nonnull(fsearch_database_chunked_array_find(arr, dup_query));
    db_entry_free(dup_query);

    FsearchDatabaseEntry *before_query = make_file("aaa_before");
    g_assert_nonnull(fsearch_database_chunked_array_find(arr, before_query));
    db_entry_free(before_query);

    FsearchDatabaseEntry *after_query = make_file("zzz_after");
    g_assert_nonnull(fsearch_database_chunked_array_find(arr, after_query));
    db_entry_free(after_query);
}

/* ------------------------------------------------------------------------ *
 * steal
 * ------------------------------------------------------------------------ */

static void
test_steal_existing_entry(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 100);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *query = make_file("f_000050");
    FsearchDatabaseEntry *stolen = fsearch_database_chunked_array_steal(arr, query);
    g_assert_nonnull(stolen);
    g_assert_cmpstr(db_entry_get_name_raw(stolen), ==, "f_000050");
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 99);
    g_assert_null(fsearch_database_chunked_array_find_slow(arr, query));

    db_entry_free(query);
    db_entry_free_no_unparent(stolen);
}

static void
test_steal_missing_entry_returns_null(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 10);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    FsearchDatabaseEntry *query = make_file("does_not_exist");
    g_assert_null(fsearch_database_chunked_array_steal(arr, query));
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 10);
    db_entry_free(query);
}

static void
test_steal_all_entries_keeps_one_chunk(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    for (uint32_t i = 0; i < 5; i++) {
        g_autofree char *name = g_strdup_printf("f_%06u", i);
        FsearchDatabaseEntry *query = make_file(name);
        FsearchDatabaseEntry *stolen = fsearch_database_chunked_array_steal(arr, query);
        g_assert_nonnull(stolen);
        db_entry_free_no_unparent(stolen);
        db_entry_free(query);
    }
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 0);
    // balance_chunk() refuses to remove the very last chunk, even when empty.
    g_assert_cmpuint(num_chunks(arr), ==, 1);

    // The array must still be fully usable afterwards.
    fsearch_database_chunked_array_insert(arr, make_file("new"));
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
}

static void
test_steal_does_not_merge_shrunken_chunks(void) {
    // Chunks only ever disappear once fully empty; there is no logic that merges two small
    // non-empty chunks back together. Stealing most (but not all) entries out of a
    // multi-chunk array must therefore leave the chunk count unchanged.
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5000);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    const uint32_t chunks_after_construction = num_chunks(arr);
    g_assert_cmpuint(chunks_after_construction, >, 1);

    // Steal every entry except the first item of each original chunk, so every chunk keeps
    // exactly one survivor and none of them become fully empty (which would remove them).
    g_autoptr(DynamicArray) chunks = fsearch_database_chunked_array_get_chunks(arr);
    for (uint32_t c = 0; c < darray_get_num_items(chunks); c++) {
        DynamicArray *chunk = darray_get_item(chunks, c);
        const uint32_t chunk_size = darray_get_num_items(chunk);

        // Snapshot the victims (everything but index 0) before mutating the chunk.
        g_autoptr(DynamicArray) victims = darray_new(chunk_size > 0 ? chunk_size - 1 : 0);
        for (uint32_t i = 1; i < chunk_size; i++) {
            darray_add_item(victims, darray_get_item(chunk, i));
        }
        for (uint32_t i = 0; i < darray_get_num_items(victims); i++) {
            FsearchDatabaseEntry *stolen = fsearch_database_chunked_array_steal(arr, darray_get_item(victims, i));
            g_assert_nonnull(stolen);
            db_entry_free_no_unparent(stolen);
        }
    }

    // Exactly one survivor per original chunk, and no chunk was emptied out and removed.
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, chunks_after_construction);
    g_assert_cmpuint(num_chunks(arr), ==, chunks_after_construction);
}

static void
test_steal_removes_emptied_chunk(void) {
    // Build exactly two chunks by construction (just over TARGET_CHUNK_SIZE), then steal
    // every entry that ends up in the second chunk. Only that chunk should disappear.
    const uint32_t count = TEST_TARGET_CHUNK_SIZE + 100;
    g_autoptr(DynamicArray) input = make_sorted_files("f", count);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    const uint32_t chunks_before = num_chunks(arr);
    g_assert_cmpuint(chunks_before, >, 1);

    g_autoptr(DynamicArray) chunks = fsearch_database_chunked_array_get_chunks(arr);
    DynamicArray *last_chunk = darray_get_item(chunks, darray_get_num_items(chunks) - 1);
    const uint32_t last_chunk_size = darray_get_num_items(last_chunk);

    // Snapshot the entries of the last chunk before mutating anything.
    g_autoptr(DynamicArray) last_chunk_entries = darray_new(last_chunk_size);
    darray_add_array(last_chunk_entries, last_chunk);

    for (uint32_t i = 0; i < last_chunk_size; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(last_chunk_entries, i);
        FsearchDatabaseEntry *stolen = fsearch_database_chunked_array_steal(arr, entry);
        g_assert_nonnull(stolen);
        db_entry_free_no_unparent(stolen);
    }

    g_assert_cmpuint(num_chunks(arr), ==, chunks_before - 1);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, count - last_chunk_size);
}

/* ------------------------------------------------------------------------ *
 * steal_descendants
 * ------------------------------------------------------------------------ */

static void
test_steal_descendants_direct_children(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *a = make_folder("a", root);
    FsearchDatabaseEntry *b = make_folder("b", root);
    FsearchDatabaseEntry *unrelated = make_folder("unrelated", NULL);

    g_autoptr(DynamicArray) input = darray_new(3);
    darray_add_item(input, a);
    darray_add_item(input, b);
    darray_add_item(input, unrelated);

    // Folder hierarchies are intentionally leaked in these tests (the chunked array uses a
    // NULL free_func here, matching the non-owning secondary indices used in production);
    // acceptable since the test process exits right after.
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FOLDER,
                                                                    NULL);

    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, root, -1);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, 2);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
    g_assert_nonnull(fsearch_database_chunked_array_find_slow(arr, unrelated));
}

static void
test_steal_descendants_deeply_nested(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *mid = make_folder("mid", root);
    FsearchDatabaseEntry *leaf1 = make_file_in("leaf1", mid);
    FsearchDatabaseEntry *leaf2 = make_file_in("leaf2", mid);
    FsearchDatabaseEntry *outsider = make_file_in("outsider", NULL);

    g_autoptr(DynamicArray) input = darray_new(3);
    darray_add_item(input, leaf1);
    darray_add_item(input, leaf2);
    darray_add_item(input, outsider);

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);

    // `root` is not leaf1/leaf2's direct parent, but is still their ancestor.
    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, root, -1);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, 2);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
    g_assert_nonnull(fsearch_database_chunked_array_find_slow(arr, outsider));
}

static void
test_steal_descendants_no_descendants_returns_empty(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *unrelated_a = make_file_in("a", NULL);
    FsearchDatabaseEntry *unrelated_b = make_file_in("b", NULL);

    g_autoptr(DynamicArray) input = darray_new(2);
    darray_add_item(input, unrelated_a);
    darray_add_item(input, unrelated_b);

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);

    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, root, -1);
    g_assert_nonnull(descendants);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, 0);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 2);
}

static void
test_steal_descendants_known_count_path_full_fast_path(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *sub = make_folder("sub", root);

    const uint32_t num_children = 50;
    g_autoptr(DynamicArray) input = darray_new(num_children);
    for (uint32_t i = 0; i < num_children; i++) {
        g_autofree char *name = g_strdup_printf("file_%03u", i);
        darray_add_item(input, make_file_in(name, sub));
    }

    // PATH_FULL sort order enables the fast contiguous-region steal path when the exact
    // descendant count is known.
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);

    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, sub, num_children);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, num_children);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 0);
    // Stealing every entry must not remove the very last chunk, same guarantee as steal().
    g_assert_cmpuint(num_chunks(arr), ==, 1);
}

static void
test_steal_descendants_unknown_count_generic_scan(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *sub = make_folder("sub", root);

    const uint32_t num_children = 50;
    g_autoptr(DynamicArray) input = darray_new(num_children);
    for (uint32_t i = 0; i < num_children; i++) {
        g_autofree char *name = g_strdup_printf("file_%03u", i);
        darray_add_item(input, make_file_in(name, sub));
    }

    // NAME sort order forces the generic db_entry_is_descendant() scan path, since
    // descendants of `sub` are no longer guaranteed to be contiguous.
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);

    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, sub, -1);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, num_children);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 0);
}

static void
test_steal_descendants_spanning_multiple_chunks(void) {
    FsearchDatabaseEntry *root = make_folder("root", NULL);
    FsearchDatabaseEntry *sub = make_folder("sub", root);

    // More descendants than fit in a single chunk.
    const uint32_t num_children = TEST_TARGET_CHUNK_SIZE + 500;
    g_autoptr(DynamicArray) input = darray_new(num_children + 2);
    darray_add_item(input, make_file_in("aaa_before", root));
    for (uint32_t i = 0; i < num_children; i++) {
        g_autofree char *name = g_strdup_printf("file_%06u", i);
        darray_add_item(input, make_file_in(name, sub));
    }
    darray_add_item(input, make_file_in("zzz_after", root));

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_PATH_FULL,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);
    g_assert_cmpuint(num_chunks(arr), >, 1);

    g_autoptr(DynamicArray) descendants = fsearch_database_chunked_array_steal_descendants(arr, sub, num_children);
    g_assert_cmpuint(darray_get_num_items(descendants), ==, num_children);
    // Only the two direct children of `root` (not `sub`) remain.
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 2);
}

/* ------------------------------------------------------------------------ *
 * remove_marked_folders / steal_marked_folders
 * ------------------------------------------------------------------------ */

static void
test_remove_marked_none_marked_is_noop(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 20);
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(fsearch_database_chunked_array_remove_marked_folders(arr), ==, 0);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 20);
}

static void
test_remove_marked_contiguous_block(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 20);
    for (uint32_t i = 5; i < 10; i++) {
        db_entry_set_mark(darray_get_item(input, i), 1);
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    const uint32_t removed = fsearch_database_chunked_array_remove_marked_folders(arr);
    g_assert_cmpuint(removed, ==, 5);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 15);
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 4)), ==, "f_000004");
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 5)), ==, "f_000010");
}

static void
test_remove_marked_non_contiguous_blocks(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 20);
    const uint32_t marked_idx[] = {0, 1, 5, 10, 11, 12, 19};
    for (size_t i = 0; i < G_N_ELEMENTS(marked_idx); i++) {
        db_entry_set_mark(darray_get_item(input, marked_idx[i]), 1);
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    const uint32_t removed = fsearch_database_chunked_array_remove_marked_folders(arr);
    g_assert_cmpuint(removed, ==, G_N_ELEMENTS(marked_idx));
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 20 - G_N_ELEMENTS(marked_idx));

    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);
    // The surviving entries are exactly {2,3,4,6,7,8,9,13,...,18}.
    g_assert_cmpstr(db_entry_get_name_raw(fsearch_database_chunked_array_get_entry(arr, 0)), ==, "f_000002");
}

static void
test_remove_marked_files_inherit_mark_from_parent(void) {
    // A file itself is never marked, but its parent folder is: is_marked() must fall back
    // to the parent's mark for FILE-typed chunked arrays.
    FsearchDatabaseEntry *marked_parent = make_folder("dir", NULL);
    db_entry_set_mark(marked_parent, 1);
    FsearchDatabaseEntry *unmarked_parent = make_folder("other_dir", NULL);

    FsearchDatabaseEntry *f1 = make_file_in("a", marked_parent);
    FsearchDatabaseEntry *f2 = make_file_in("b", marked_parent);
    FsearchDatabaseEntry *f3 = make_file_in("c", unmarked_parent);

    g_autoptr(DynamicArray) input = darray_new(3);
    darray_add_item(input, f1);
    darray_add_item(input, f2);
    darray_add_item(input, f3);

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    FALSE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    NULL);
    const uint32_t removed = fsearch_database_chunked_array_remove_marked_folders(arr);
    g_assert_cmpuint(removed, ==, 2);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
    g_assert_nonnull(fsearch_database_chunked_array_find_slow(arr, f3));
}

static void
test_remove_marked_spanning_chunk_boundary(void) {
    const uint32_t count = TEST_TARGET_CHUNK_SIZE + 100;
    g_autoptr(DynamicArray) input = make_sorted_files("f", count);

    // Mark a contiguous run that straddles the chunk boundary.
    const uint32_t mark_start = TEST_TARGET_CHUNK_SIZE - 10;
    const uint32_t mark_end = TEST_TARGET_CHUNK_SIZE + 10; // exclusive
    for (uint32_t i = mark_start; i < mark_end; i++) {
        db_entry_set_mark(darray_get_item(input, i), 1);
    }

    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_assert_cmpuint(num_chunks(arr), >, 1);

    const uint32_t removed = fsearch_database_chunked_array_remove_marked_folders(arr);
    g_assert_cmpuint(removed, ==, mark_end - mark_start);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, count - (mark_end - mark_start));

    assert_sorted_by_property(arr, DATABASE_INDEX_PROPERTY_NAME);

    // Note: can't reuse darray_get_item(input, mark_start) here - remove_marked_folders()
    // already freed that entry via db_entry_free_no_unparent, and `input` only ever held a
    // shallow copy of the same (now-dangling) pointer. Query by an equivalent fresh entry
    // instead.
    g_autofree char *removed_name = g_strdup_printf("f_%06u", mark_start);
    FsearchDatabaseEntry *removed_query = make_file(removed_name);
    g_assert_null(fsearch_database_chunked_array_find_slow(arr, removed_query));
    db_entry_free(removed_query);
}

static void
test_steal_marked_returns_removed_entries(void) {
    g_autoptr(DynamicArray) input = make_sorted_files("f", 20);
    for (uint32_t i = 5; i < 10; i++) {
        db_entry_set_mark(darray_get_item(input, i), 1);
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) stolen = fsearch_database_chunked_array_steal_marked_folders(arr);
    g_assert_cmpuint(darray_get_num_items(stolen), ==, 5);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 15);
    for (uint32_t i = 0; i < darray_get_num_items(stolen); i++) {
        g_assert_cmpuint(db_entry_get_mark(darray_get_item(stolen, i)), ==, 1);
    }
    free_stolen(g_steal_pointer(&stolen));
}

static void
test_steal_marked_all_marked_keeps_one_chunk(void) {
    // Mirrors test_steal_all_entries_keeps_one_chunk(): removing every entry via
    // steal_marked_folders()/remove_marked_folders() must not remove the very last chunk
    // either, same as fsearch_database_chunked_array_steal()'s balance_chunk() guarantee.
    g_autoptr(DynamicArray) input = make_sorted_files("f", 5);
    for (uint32_t i = 0; i < 5; i++) {
        db_entry_set_mark(darray_get_item(input, i), 1);
    }
    g_autoptr(FsearchDatabaseChunkedArray) arr = make_chunked_array(input,
                                                                    TRUE,
                                                                    DATABASE_INDEX_PROPERTY_NAME,
                                                                    DATABASE_ENTRY_TYPE_FILE,
                                                                    (GDestroyNotify)db_entry_free_no_unparent);
    g_autoptr(DynamicArray) stolen = fsearch_database_chunked_array_steal_marked_folders(arr);
    g_assert_cmpuint(darray_get_num_items(stolen), ==, 5);
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 0);
    g_assert_cmpuint(num_chunks(arr), ==, 1);

    free_stolen(g_steal_pointer(&stolen));

    // The array must still be usable afterwards.
    fsearch_database_chunked_array_insert(arr, make_file("new"));
    g_assert_cmpuint(fsearch_database_chunked_array_get_num_entries(arr), ==, 1);
    g_assert_cmpuint(num_chunks(arr), ==, 1);
    g_assert_cmpuint(num_chunks(arr), ==, 1);
}

/* ------------------------------------------------------------------------ *
 * Main
 * ------------------------------------------------------------------------ */

int
main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    // Construction & lifecycle
    g_test_add_func("/FSearch/database/chunked_array/new_empty", test_new_empty_array);
    g_test_add_func("/FSearch/database/chunked_array/new_small_single_chunk", test_new_small_array_single_chunk);
    g_test_add_func("/FSearch/database/chunked_array/new_large_splits_at_construction",
                    test_new_large_array_splits_at_construction);
    g_test_add_func("/FSearch/database/chunked_array/new_unsorted_input_gets_sorted",
                    test_new_unsorted_input_gets_sorted);
    g_test_add_func("/FSearch/database/chunked_array/new_claiming_sorted_is_trusted",
                    test_new_claiming_sorted_when_not_is_trusted);
    g_test_add_func("/FSearch/database/chunked_array/ref_unref_roundtrip", test_ref_unref_roundtrip);
    g_test_add_func("/FSearch/database/chunked_array/get_type_registered", test_get_type_is_registered);

    // get_num_entries / get_entry / get_chunks / get_joined
    g_test_add_func("/FSearch/database/chunked_array/get_entry_out_of_bounds", test_get_entry_out_of_bounds_returns_null);
    g_test_add_func("/FSearch/database/chunked_array/get_entry_first_last", test_get_entry_first_and_last);
    g_test_add_func("/FSearch/database/chunked_array/get_entry_crosses_chunk_boundary",
                    test_get_entry_crosses_chunk_boundary);
    g_test_add_func("/FSearch/database/chunked_array/get_chunks_total_matches_num_entries",
                    test_get_chunks_total_matches_num_entries);
    g_test_add_func("/FSearch/database/chunked_array/get_joined_matches_get_entry",
                    test_get_joined_matches_sequential_get_entry);

    // insert
    g_test_add_func("/FSearch/database/chunked_array/insert_into_empty", test_insert_into_empty_array);
    g_test_add_func("/FSearch/database/chunked_array/insert_maintains_sort_order", test_insert_maintains_sort_order);
    g_test_add_func("/FSearch/database/chunked_array/insert_no_split_below_2x_threshold",
                    test_insert_does_not_split_below_2x_threshold);
    g_test_add_func("/FSearch/database/chunked_array/insert_splits_at_2x_threshold", test_insert_splits_at_2x_threshold);
    g_test_add_func("/FSearch/database/chunked_array/insert_wrong_type_rejected",
                    test_insert_wrong_entry_type_is_rejected);
    g_test_add_func("/FSearch/database/chunked_array/insert_with_size_sort_order", test_insert_with_size_sort_order);

    // insert_array
    g_test_add_func("/FSearch/database/chunked_array/insert_array_empty_noop", test_insert_array_empty_is_noop);
    g_test_add_func("/FSearch/database/chunked_array/insert_array_small_individual_path",
                    test_insert_array_small_uses_individual_path);
    g_test_add_func("/FSearch/database/chunked_array/insert_array_bulk_path_at_threshold",
                    test_insert_array_bulk_path_at_threshold);
    g_test_add_func("/FSearch/database/chunked_array/insert_array_bulk_interleaves_correctly",
                    test_insert_array_bulk_path_interleaves_correctly);
    g_test_add_func("/FSearch/database/chunked_array/insert_array_bulk_into_empty_self",
                    test_insert_array_bulk_path_into_empty_self);

    // find / find_slow
    g_test_add_func("/FSearch/database/chunked_array/find_existing", test_find_existing_entry);
    g_test_add_func("/FSearch/database/chunked_array/find_missing_null", test_find_missing_entry_returns_null);
    g_test_add_func("/FSearch/database/chunked_array/find_empty_array_null", test_find_empty_array_returns_null);
    g_test_add_func("/FSearch/database/chunked_array/find_slow_existing_and_missing",
                    test_find_slow_existing_and_missing);
    g_test_add_func("/FSearch/database/chunked_array/find_duplicate_keys",
                    test_find_duplicate_keys_returns_a_matching_entry);
    g_test_add_func("/FSearch/database/chunked_array/find_duplicates_across_chunk_boundary",
                    test_find_with_duplicates_spanning_chunk_boundary);

    // steal
    g_test_add_func("/FSearch/database/chunked_array/steal_existing", test_steal_existing_entry);
    g_test_add_func("/FSearch/database/chunked_array/steal_missing_null", test_steal_missing_entry_returns_null);
    g_test_add_func("/FSearch/database/chunked_array/steal_all_keeps_one_chunk", test_steal_all_entries_keeps_one_chunk);
    g_test_add_func("/FSearch/database/chunked_array/steal_does_not_merge_shrunken_chunks",
                    test_steal_does_not_merge_shrunken_chunks);
    g_test_add_func("/FSearch/database/chunked_array/steal_removes_emptied_chunk", test_steal_removes_emptied_chunk);

    // steal_descendants
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_direct_children",
                    test_steal_descendants_direct_children);
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_deeply_nested",
                    test_steal_descendants_deeply_nested);
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_none_returns_empty",
                    test_steal_descendants_no_descendants_returns_empty);
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_known_count_fast_path",
                    test_steal_descendants_known_count_path_full_fast_path);
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_unknown_count_generic_scan",
                    test_steal_descendants_unknown_count_generic_scan);
    g_test_add_func("/FSearch/database/chunked_array/steal_descendants_spanning_multiple_chunks",
                    test_steal_descendants_spanning_multiple_chunks);

    // remove_marked_folders / steal_marked_folders
    g_test_add_func("/FSearch/database/chunked_array/remove_marked_none_noop", test_remove_marked_none_marked_is_noop);
    g_test_add_func("/FSearch/database/chunked_array/remove_marked_contiguous_block",
                    test_remove_marked_contiguous_block);
    g_test_add_func("/FSearch/database/chunked_array/remove_marked_non_contiguous_blocks",
                    test_remove_marked_non_contiguous_blocks);
    g_test_add_func("/FSearch/database/chunked_array/remove_marked_files_inherit_from_parent",
                    test_remove_marked_files_inherit_mark_from_parent);
    g_test_add_func("/FSearch/database/chunked_array/remove_marked_spans_chunk_boundary",
                    test_remove_marked_spanning_chunk_boundary);
    g_test_add_func("/FSearch/database/chunked_array/steal_marked_returns_entries",
                    test_steal_marked_returns_removed_entries);
    g_test_add_func("/FSearch/database/chunked_array/steal_marked_all_keeps_one_chunk",
                    test_steal_marked_all_marked_keeps_one_chunk);

    return g_test_run();
}

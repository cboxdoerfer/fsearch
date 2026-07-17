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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

/*
 * Reproduces a bug where a cancelled search silently overwrote a search view's results and query
 * with no indication that the search never actually finished.
 *
 * fsearch_database_index_store_search() never checked whether `cancellable` was cancelled before
 * installing its (possibly empty/partial) results as the new FsearchDatabaseSearchView for that
 * id. Since index_store_search_worker() bails out of its per-entry match loop as soon as the
 * cancellable is cancelled, a search that gets cancelled mid-flight (e.g. via the "cancel current
 * task" UI action) would replace whatever results were there before with an incomplete snapshot,
 * indistinguishable from a genuinely finished search.
 *
 * The fix keeps the partial results (rather than discarding the search's work) but tags the view
 * (and FsearchDatabaseSearchInfo derived from it) as incomplete, so the UI can tell partial
 * results apart from a finished search.
 *
 * To make this deterministic (no thread-timing races), the cancellable passed to the second
 * search below is cancelled *before* the search starts. Every worker thread then bails out on its
 * very first entry, so the "cancelled" search finds zero matches -- the worst case of a partial
 * result set, but still a valid one to assert against: it must be committed and flagged
 * incomplete, not silently treated as if it were a real, finished 0-result search.
 */

#include "fsearch_database_entry.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_filter_manager.h"
#include "fsearch_query.h"

#include <gio/gio.h>
#include <glib.h>

static DynamicArray *
make_named_files(const char *prefix, uint32_t count) {
    DynamicArray *array = darray_new(count);
    for (uint32_t i = 0; i < count; i++) {
        g_autofree char *name = g_strdup_printf("%s_%06u", prefix, i);
        darray_add_item(array, db_entry_new(DATABASE_INDEX_PROPERTY_FLAG_NONE, name, NULL, DATABASE_ENTRY_TYPE_FILE));
    }
    return array;
}

static void
free_entries(DynamicArray *array) {
    const uint32_t n = darray_get_num_items(array);
    for (uint32_t i = 0; i < n; i++) {
        db_entry_free(darray_get_item(array, i));
    }
    darray_unref(array);
}

static FsearchQuery *
make_query(FsearchFilterManager *filters, const char *search_term) {
    return fsearch_query_new(search_term, NULL, filters, 0, "test");
}

static void
test_cancelled_search_keeps_partial_results_marked_incomplete(void) {
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();
    FsearchFilterManager *filters = fsearch_filter_manager_new_with_defaults();

    DynamicArray *files = make_named_files("apple", 100);
    g_autoptr(DynamicArray) folders = darray_new(0);

    DynamicArray *files_by_property[NUM_DATABASE_INDEX_PROPERTIES] = {0};
    DynamicArray *folders_by_property[NUM_DATABASE_INDEX_PROPERTIES] = {0};
    files_by_property[DATABASE_INDEX_PROPERTY_NAME] = files;
    folders_by_property[DATABASE_INDEX_PROPERTY_NAME] = folders;

    g_autoptr(GPtrArray) indices = g_ptr_array_new();
    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new_with_content(
        indices,
        files_by_property,
        folders_by_property,
        include_manager,
        exclude_manager,
        DATABASE_INDEX_PROPERTY_FLAG_NAME,
        NULL,
        NULL);

    const uint32_t view_id = 1;

    // First, a normal, uncancelled search that matches everything.
    g_autoptr(FsearchQuery) query_1 = make_query(filters, "apple");
    g_autoptr(GCancellable) cancellable_1 = g_cancellable_new();
    g_assert_true(fsearch_database_index_store_search(store,
                                                      view_id,
                                                      query_1,
                                                      DATABASE_INDEX_PROPERTY_NAME,
                                                      GTK_SORT_ASCENDING,
                                                      cancellable_1));

    g_autoptr(FsearchDatabaseSearchInfo) info_1 = fsearch_database_index_store_get_search_info(store, view_id);
    g_assert_nonnull(info_1);
    g_assert_cmpuint(fsearch_database_search_info_get_num_files(info_1), ==, 100);
    g_assert_true(fsearch_database_search_info_get_is_complete(info_1));

    // Second search: a different query, but the cancellable is already cancelled *before* the
    // search runs, simulating the user hitting "cancel" while it was in flight.
    g_autoptr(FsearchQuery) query_2 = make_query(filters, "banana");
    g_autoptr(GCancellable) cancellable_2 = g_cancellable_new();
    g_cancellable_cancel(cancellable_2);

    const bool result = fsearch_database_index_store_search(store,
                                                            view_id,
                                                            query_2,
                                                            DATABASE_INDEX_PROPERTY_NAME,
                                                            GTK_SORT_ASCENDING,
                                                            cancellable_2);
    // The search function itself should report that it did not complete.
    g_assert_false(result);

    // The (partial) results still get installed -- here, 0 matches, since every worker bailed on
    // its first entry -- but the view must be marked incomplete, and its query must reflect what
    // was actually (partially) searched, not silently stay on the old query.
    g_autoptr(FsearchDatabaseSearchInfo) info_2 = fsearch_database_index_store_get_search_info(store, view_id);
    g_assert_nonnull(info_2);
    g_assert_cmpuint(fsearch_database_search_info_get_num_files(info_2), ==, 0);
    g_assert_false(fsearch_database_search_info_get_is_complete(info_2));

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, view_id);
    g_assert_nonnull(view);
    g_autoptr(FsearchQuery) active_query = fsearch_database_search_view_get_query(view);
    g_assert_cmpstr(active_query->search_term, ==, "banana");

    free_entries(files);
    fsearch_filter_manager_unref(filters);
}

int
main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/FSearch/database/index_store/cancelled_search_keeps_partial_results_marked_incomplete",
                    test_cancelled_search_keeps_partial_results_marked_incomplete);

    return g_test_run();
}
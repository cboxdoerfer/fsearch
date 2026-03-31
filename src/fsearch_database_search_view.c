#include "fsearch_database_search_view.h"

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_sort.h"
#include "fsearch_query.h"
#include "fsearch_query_match_data.h"
#include "fsearch_selection.h"

#include <glib.h>
#include <glib/gmacros.h>
#include <gtk/gtkenums.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct FsearchDatabaseSearchView {
    FsearchQuery *query;
    FsearchDatabaseEntriesContainer *file_container;
    FsearchDatabaseEntriesContainer *folder_container;
    uint32_t id;
    GtkSortType sort_type;
    FsearchDatabaseIndexProperty sort_order;
    FsearchDatabaseIndexProperty secondardy_sort_order;
    GHashTable *file_selection;
    GHashTable *folder_selection;
};

void
fsearch_database_search_view_free(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);
    g_clear_pointer(&view->query, fsearch_query_unref);
    g_clear_pointer(&view->file_container, fsearch_database_entries_container_unref);
    g_clear_pointer(&view->folder_container, fsearch_database_entries_container_unref);
    g_clear_pointer(&view->file_selection, fsearch_selection_free);
    g_clear_pointer(&view->folder_selection, fsearch_selection_free);
    g_clear_pointer(&view, free);
}

FsearchDatabaseSearchView *
fsearch_database_search_view_new(uint32_t id,
                                 FsearchQuery *query,
                                 DynamicArray *files,
                                 DynamicArray *folders,
                                 GHashTable *old_selection,
                                 FsearchDatabaseIndexProperty sort_order,
                                 FsearchDatabaseIndexProperty secondary_sort_order,
                                 GtkSortType sort_type) {
    FsearchDatabaseSearchView *view = calloc(1, sizeof(FsearchDatabaseSearchView));
    g_assert(view);
    view->id = id;
    view->query = fsearch_query_ref(query);
    view->folder_container = fsearch_database_entries_container_new(folders,
                                                                    TRUE,
                                                                    sort_order,
                                                                    secondary_sort_order,
                                                                    DATABASE_ENTRY_TYPE_FOLDER,
                                                                    NULL);
    view->file_container = fsearch_database_entries_container_new(files,
                                                                  TRUE,
                                                                  sort_order,
                                                                  secondary_sort_order,
                                                                  DATABASE_ENTRY_TYPE_FILE,
                                                                  NULL);
    view->sort_order = sort_order;
    view->secondardy_sort_order = secondary_sort_order;
    view->sort_type = sort_type;
    view->file_selection = fsearch_selection_new();
    view->folder_selection = fsearch_selection_new();

    FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, 0);
    if (entry) {
        if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select(view->file_selection, entry);
        }
        else {
            fsearch_selection_select(view->folder_selection, entry);
        }
    }
    return view;
}

static uint32_t
search_view_get_num_file_results(FsearchDatabaseSearchView *view) {
    return view && view->file_container ? fsearch_database_entries_container_get_num_entries(view->file_container) : 0;
}

static uint32_t
search_view_get_num_folder_results(FsearchDatabaseSearchView *view) {
    return view && view->folder_container
               ? fsearch_database_entries_container_get_num_entries(view->folder_container)
               : 0;
}

static uint32_t
get_idx_for_sort_type(uint32_t idx, uint32_t num_files, uint32_t num_folders, GtkSortType sort_type) {
    if (sort_type == GTK_SORT_DESCENDING) {
        return num_folders + num_files - (idx + 1);
    }
    return idx;
}

FsearchDatabaseEntry *
fsearch_database_search_view_get_entry_for_idx(FsearchDatabaseSearchView *view, uint32_t idx) {
    if (!view->folder_container) {
        return NULL;
    }
    if (!view->file_container) {
        return NULL;
    }
    const uint32_t num_folders = search_view_get_num_folder_results(view);
    const uint32_t num_files = search_view_get_num_file_results(view);

    idx = get_idx_for_sort_type(idx, num_files, num_folders, view->sort_type);

    if (idx < num_folders) {
        return fsearch_database_entries_container_get_entry(view->folder_container, idx);
    }
    idx -= num_folders;
    if (idx < num_files) {
        return fsearch_database_entries_container_get_entry(view->file_container, idx);
    }
    return NULL;
}

bool
fsearch_database_search_view_is_selected(FsearchDatabaseSearchView *view, FsearchDatabaseEntry *entry) {
    if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FILE) {
        return fsearch_selection_is_selected(view->file_selection, entry);
    }
    return fsearch_selection_is_selected(view->folder_selection, entry);
}

void
fsearch_database_search_view_toggle_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx) {
    int32_t tmp = start_idx;
    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = tmp;
    }
    for (int32_t i = start_idx; i <= end_idx; ++i) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) {
            continue;
        }
        FsearchDatabaseEntryType type = db_entry_get_type(entry);
        if (type == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select_toggle(view->file_selection, entry);
        }
        else {
            fsearch_selection_select_toggle(view->folder_selection, entry);
        }
    }
}

void
fsearch_database_search_view_select_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx) {
    int32_t tmp = start_idx;
    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = tmp;
    }
    for (int32_t i = start_idx; i <= end_idx; ++i) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) {
            continue;
        }
        FsearchDatabaseEntryType type = db_entry_get_type(entry);
        if (type == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select(view->file_selection, entry);
        }
        else {
            fsearch_selection_select(view->folder_selection, entry);
        }
    }
}

void
fsearch_database_search_view_select_all(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);
    g_autoptr(DynamicArray) file_container = fsearch_database_entries_container_get_containers(view->file_container);
    g_autoptr(DynamicArray) folder_container =
        fsearch_database_entries_container_get_containers(view->folder_container);

    for (uint32_t i = 0; i < darray_get_num_items(file_container); ++i) {
        fsearch_selection_select_all(view->file_selection, darray_get_item(file_container, i));
    }
    for (uint32_t i = 0; i < darray_get_num_items(folder_container); ++i) {
        fsearch_selection_select_all(view->folder_selection, darray_get_item(folder_container, i));
    }
}

void
fsearch_database_search_view_invert_selection(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);
    g_autoptr(DynamicArray) file_container = fsearch_database_entries_container_get_containers(view->file_container);
    g_autoptr(DynamicArray) folder_container =
        fsearch_database_entries_container_get_containers(view->folder_container);

    for (uint32_t i = 0; i < darray_get_num_items(file_container); ++i) {
        fsearch_selection_invert(view->file_selection, darray_get_item(file_container, i));
    }
    for (uint32_t i = 0; i < darray_get_num_items(folder_container); ++i) {
        fsearch_selection_invert(view->folder_selection, darray_get_item(folder_container, i));
    }
}

void
fsearch_database_search_view_clear_selection(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);

    fsearch_selection_unselect_all(view->file_selection);
    fsearch_selection_unselect_all(view->folder_selection);
}

void
fsearch_database_search_view_selection_foreach(FsearchDatabaseSearchView *view, GHFunc func, gpointer user_data) {
    g_return_if_fail(view);

    g_hash_table_foreach(view->folder_selection, func, user_data);
    g_hash_table_foreach(view->file_selection, func, user_data);
}


void
fsearch_database_search_view_sort(FsearchDatabaseSearchView *view,
                                  DynamicArray *files_fast_sorted,
                                  DynamicArray *folders_fast_sorted,
                                  FsearchDatabaseIndexProperty sort_order,
                                  GtkSortType sort_type,
                                  GCancellable *cancellable) {
    g_return_if_fail(view);

    g_autoptr(DynamicArray) files_new = NULL;
    g_autoptr(DynamicArray) folders_new = NULL;
    g_autoptr(DynamicArray) files_in = fsearch_database_entries_container_get_joined(view->file_container);
    g_autoptr(DynamicArray) folders_in = fsearch_database_entries_container_get_joined(view->folder_container);

    fsearch_database_sort_results(view->sort_order,
                                  view->secondardy_sort_order,
                                  sort_order,
                                  files_in,
                                  folders_in,
                                  files_fast_sorted,
                                  folders_fast_sorted,
                                  &files_new,
                                  &folders_new,
                                  &view->sort_order,
                                  &view->secondardy_sort_order,
                                  cancellable);

    if (files_new) {
        g_clear_pointer(&view->file_container, fsearch_database_entries_container_unref);
        view->file_container = fsearch_database_entries_container_new(files_new,
                                                                      TRUE,
                                                                      view->sort_order,
                                                                      view->secondardy_sort_order,
                                                                      DATABASE_ENTRY_TYPE_FILE,
                                                                      NULL);
        view->sort_type = sort_type;
    }
    if (folders_new) {
        g_clear_pointer(&view->folder_container, fsearch_database_entries_container_unref);
        view->folder_container = fsearch_database_entries_container_new(folders_new,
                                                                        TRUE,
                                                                        view->sort_order,
                                                                        view->secondardy_sort_order,
                                                                        DATABASE_ENTRY_TYPE_FOLDER,
                                                                        NULL);
        view->sort_type = sort_type;
    }
}


static bool
search_view_entry_matches_query(FsearchDatabaseSearchView *view, FsearchDatabaseEntry *entry) {
    FsearchQueryMatchData *match_data = fsearch_query_match_data_new(NULL, NULL);
    fsearch_query_match_data_set_entry(match_data, entry);

    const bool found = fsearch_query_match(view->query, match_data);
    g_clear_pointer(&match_data, fsearch_query_match_data_free);
    return found;
}

static bool
search_view_result_add(FsearchDatabaseEntry *entry, FsearchDatabaseSearchView *view) {
    if (!entry || !search_view_entry_matches_query(view, entry)) {
        return true;
    }

    fsearch_database_entries_container_insert(db_entry_is_folder(entry) ? view->folder_container : view->file_container,
                                              entry);
    return true;
}

static bool
search_view_result_remove(FsearchDatabaseEntry *entry, FsearchDatabaseSearchView *view) {
    if (!entry || !search_view_entry_matches_query(view, entry)) {
        return true;
    }

    FsearchDatabaseEntriesContainer *container = NULL;
    GHashTable *selection = NULL;
    if (db_entry_is_folder(entry)) {
        container = view->folder_container;
        selection = view->folder_selection;
    }
    else {
        container = view->file_container;
        selection = view->file_selection;
    }

    // Remove it from search results
    fsearch_database_entries_container_steal(container, entry);

    // Remove it from the selection
    fsearch_selection_unselect(selection, entry);

    return true;
}

// Manipulation
void
fsearch_database_search_view_add(FsearchDatabaseSearchView *view, DynamicArray *files, DynamicArray *folders) {
    g_return_if_fail(view);
    if (files) {
        darray_for_each(files, (DynamicArrayForEachFunc)search_view_result_add, view);
    }
    if (folders) {
        darray_for_each(folders, (DynamicArrayForEachFunc)search_view_result_add, view);
    }
}

void
fsearch_database_search_view_remove(FsearchDatabaseSearchView *view, DynamicArray *files, DynamicArray *folders) {
    g_return_if_fail(view);
    if (files) {
        darray_for_each(files, (DynamicArrayForEachFunc)search_view_result_remove, view);
    }
    if (folders) {
        darray_for_each(folders, (DynamicArrayForEachFunc)search_view_result_remove, view);
    }
}

// Getters

FsearchDatabaseSearchInfo *
fsearch_database_search_view_get_info(FsearchDatabaseSearchView *view) {
    g_return_val_if_fail(view, NULL);
    return fsearch_database_search_info_new(view->id,
                                            fsearch_query_ref(view->query),
                                            search_view_get_num_file_results(view),
                                            search_view_get_num_folder_results(view),
                                            fsearch_selection_get_num_selected(view->file_selection),
                                            fsearch_selection_get_num_selected(view->folder_selection),
                                            view->sort_order,
                                            view->sort_type);
}

FsearchQuery *
fsearch_database_search_view_get_query(FsearchDatabaseSearchView *view) {
    g_return_val_if_fail(view, NULL);
    return fsearch_query_ref(view->query);
}
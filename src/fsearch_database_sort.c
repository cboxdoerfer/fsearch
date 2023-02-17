#include "fsearch_database_sort.h"

#include "fsearch_database_entry.h"

static bool
is_valid_fast_sort_type(FsearchDatabaseIndexType sort_type) {
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_TYPES) {
        return true;
    }
    return false;
}

static bool
has_entries_sorted_by_type(DynamicArray **sorted_entries, FsearchDatabaseIndexType sort_type) {
    if (!is_valid_fast_sort_type(sort_type)) {
        return false;
    }
    return sorted_entries[sort_type] ? true : false;
}

static bool
sort_order_affects_folders(FsearchDatabaseIndexType sort_order) {
    if (sort_order == DATABASE_INDEX_TYPE_EXTENSION || sort_order == DATABASE_INDEX_TYPE_FILETYPE) {
        // Folders are stored in a different array than files, so they all have the same sort_type and extension (none),
        // therefore we don't need to sort them in such cases.
        return false;
    }
    return true;
}

static DynamicArrayCompareDataFunc
get_sort_func(FsearchDatabaseIndexType sort_order) {
    DynamicArrayCompareDataFunc func = NULL;
    switch (sort_order) {
    case DATABASE_INDEX_TYPE_NAME:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_name;
        break;
    case DATABASE_INDEX_TYPE_PATH:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path;
        break;
    case DATABASE_INDEX_TYPE_SIZE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_size;
        break;
    case DATABASE_INDEX_TYPE_EXTENSION:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_extension;
        break;
    case DATABASE_INDEX_TYPE_FILETYPE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_type;
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_modification_time;
        break;
    default:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_position;
    }
    return func;
}

static DynamicArray *
get_entries_sorted_from_reference_list(DynamicArray *old_list, DynamicArray *sorted_reference_list) {
    const uint32_t num_items = darray_get_num_items(old_list);
    DynamicArray *new = darray_new(num_items);
    for (uint32_t i = 0; i < num_items; ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(old_list, i);
        db_entry_set_mark(entry, 1);
    }
    uint32_t num_marked_found = 0;
    const uint32_t num_items_in_sorted_reference_list = darray_get_num_items(sorted_reference_list);
    for (uint32_t i = 0; i < num_items_in_sorted_reference_list && num_marked_found < num_items; ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(sorted_reference_list, i);
        if (db_entry_get_mark(entry)) {
            db_entry_set_mark(entry, 0);
            darray_add_item(new, entry);
            num_marked_found++;
        }
    }
    return new;
}

static DynamicArray *
sort_entries(DynamicArray *entries_in,
             DynamicArrayCompareDataFunc sort_func,
             GCancellable *cancellable,
             bool parallel_sort,
             void *data) {
    DynamicArray *entries = darray_copy(entries_in);
    if (parallel_sort) {
        darray_sort_multi_threaded(entries, sort_func, cancellable, data);
    }
    else {
        darray_sort(entries, sort_func, cancellable, data);
    }
    return entries;
}

static DynamicArray *
fast_sort(FsearchDatabaseIndexType new_sort_order, DynamicArray *entries_in, DynamicArray **sorted_entries_in) {
    if (darray_get_num_items(entries_in) == darray_get_num_items(sorted_entries_in[new_sort_order])) {
        // We're matching everything, and we have the entries already sorted in our index.
        // So we can just return references to the sorted indices.
        return darray_ref(sorted_entries_in[new_sort_order]);
    }
    else {
        // Another fast path. First we mark all entries we have currently in the view, then we walk the sorted
        // index in order and add all marked entries to a new array.
        return get_entries_sorted_from_reference_list(entries_in, sorted_entries_in[new_sort_order]);
    }
}

void
fsearch_database_sort_results(FsearchDatabaseIndexType old_sort_order,
                              FsearchDatabaseIndexType new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray **sorted_files_in,
                              DynamicArray **sorted_folders_in,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseIndexType *sort_order_out,
                              GCancellable *cancellable) {
    g_return_if_fail(files_in);
    g_return_if_fail(folders_in);
    g_return_if_fail(sorted_files_in);
    g_return_if_fail(sorted_folders_in);
    g_return_if_fail(files_out);
    g_return_if_fail(folders_out);
    g_return_if_fail(sort_order_out);

    if (old_sort_order == new_sort_order) {
        // Sort order didn't change, use the old results
        *files_out = darray_ref(files_in);
        *folders_out = darray_ref(folders_in);
        *sort_order_out = new_sort_order;
        return;
    }

    if (has_entries_sorted_by_type(sorted_files_in, new_sort_order)) {
        // Use the fast-sort indices
        *files_out = fast_sort(new_sort_order, files_in, sorted_files_in);
        *folders_out = fast_sort(new_sort_order, folders_in, sorted_folders_in);
        *sort_order_out = new_sort_order;
        return;
    }

    DynamicArrayCompareDataFunc func = get_sort_func(new_sort_order);
    bool parallel_sort = true;

    FsearchDatabaseEntryCompareContext *comp_ctx = NULL;
    if (new_sort_order == DATABASE_INDEX_TYPE_FILETYPE) {
        // Sorting by type can be really slow, because it accesses the filesystem to determine the type of files
        // To mitigate that issue to a certain degree we cache the filetype for each file
        // To avoid duplicating the filetype in memory for each file, we also store each filetype only once in
        // a separate hash table.
        // We also disable parallel sorting.
        comp_ctx = calloc(1, sizeof(FsearchDatabaseEntryCompareContext));
        comp_ctx->file_type_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        comp_ctx->entry_to_file_type_table = g_hash_table_new(NULL, NULL);
        parallel_sort = false;
    }

    if (sort_order_affects_folders(new_sort_order)) {
        *folders_out = sort_entries(folders_in, func, cancellable, parallel_sort, comp_ctx);
    }
    else {
        *folders_out = darray_ref(folders_in);
    }
    *files_out = sort_entries(files_in, func, cancellable, parallel_sort, comp_ctx);
    *sort_order_out = new_sort_order;

    if (comp_ctx) {
        g_clear_pointer(&comp_ctx->entry_to_file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx->file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx, free);
    }

    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        g_clear_pointer(folders_out, darray_unref);
        g_clear_pointer(files_out, darray_unref);
        *sort_order_out = old_sort_order;
    }
}

void
fsearch_database_sort(FsearchDatabaseIndexType sort_order,
                      DynamicArray *files_in,
                      DynamicArray *folders_in,
                      FsearchDatabaseIndexType *sort_order_out,
                      DynamicArray **files_out,
                      DynamicArray **folders_out,
                      GCancellable *cancellable) {}

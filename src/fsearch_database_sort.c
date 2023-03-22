#include "fsearch_database_sort.h"

#include "fsearch_database_entry.h"

static void
clear_fast_sorted_array(DynamicArray **sorted_entries, FsearchDatabaseIndexProperty property) {
    if (sorted_entries && sorted_entries[property]) {
        g_clear_pointer(&sorted_entries[property], darray_unref);
    }
}

static bool
sort_order_affects_folders(FsearchDatabaseIndexProperty sort_order) {
    if (sort_order == DATABASE_INDEX_PROPERTY_EXTENSION || sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
        // Folders are stored in a different array than files, so they all have the same sort_type and extension (none),
        // therefore we don't need to sort them in such cases.
        return false;
    }
    return true;
}

static DynamicArrayCompareDataFunc
get_sort_func(FsearchDatabaseIndexProperty sort_order) {
    DynamicArrayCompareDataFunc func = NULL;
    switch (sort_order) {
    case DATABASE_INDEX_PROPERTY_NAME:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_name;
        break;
    case DATABASE_INDEX_PROPERTY_PATH:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path;
        break;
    case DATABASE_INDEX_PROPERTY_SIZE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_size;
        break;
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_extension;
        break;
    case DATABASE_INDEX_PROPERTY_FILETYPE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_type;
        break;
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
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
fast_sort(FsearchDatabaseIndexProperty new_sort_order, DynamicArray *entries_in, DynamicArray *fast_sort_index) {
    if (darray_get_num_items(entries_in) == darray_get_num_items(fast_sort_index)) {
        // We're matching everything, and we have the entries already sorted in our index.
        // So we can just return references to the sorted indices.
        return darray_ref(fast_sort_index);
    }
    else {
        // Another fast path. First we mark all entries we have currently in the view, then we walk the sorted
        // index in order and add all marked entries to a new array.
        return get_entries_sorted_from_reference_list(entries_in, fast_sort_index);
    }
}

void
fsearch_database_sort_results(FsearchDatabaseIndexProperty old_sort_order,
                              FsearchDatabaseIndexProperty new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray *files_fast_sort_index,
                              DynamicArray *folders_fast_sort_index,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseIndexProperty *sort_order_out,
                              GCancellable *cancellable) {
    g_return_if_fail(files_in);
    g_return_if_fail(folders_in);
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

    if (files_fast_sort_index && folders_fast_sort_index) {
        // Use the fast-sort indices
        *files_out = fast_sort(new_sort_order, files_in, files_fast_sort_index);
        *folders_out = fast_sort(new_sort_order, folders_in, folders_fast_sort_index);
        *sort_order_out = new_sort_order;
        return;
    }

    DynamicArrayCompareDataFunc func = get_sort_func(new_sort_order);
    bool parallel_sort = true;

    FsearchDatabaseEntryCompareContext *comp_ctx = NULL;
    if (new_sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
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
        *folders_out = darray_copy(folders_in);
    }
    *files_out = sort_entries(files_in, func, cancellable, parallel_sort, comp_ctx);
    *sort_order_out = new_sort_order;

    if (comp_ctx) {
        g_clear_pointer(&comp_ctx->entry_to_file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx->file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx, free);
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        g_clear_pointer(folders_out, darray_unref);
        g_clear_pointer(files_out, darray_unref);
        *sort_order_out = old_sort_order;
    }
}

static void
sort_store_entries(DynamicArray *entries,
                   DynamicArray **sorted_entries,
                   FsearchDatabaseIndexPropertyFlags flags,
                   GCancellable *cancellable) {
    // first sort by path
    darray_sort_multi_threaded(entries, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path, cancellable, NULL);
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }
    clear_fast_sorted_array(sorted_entries, DATABASE_INDEX_PROPERTY_PATH);
    sorted_entries[DATABASE_INDEX_PROPERTY_PATH] = darray_copy(entries);

    // then by name
    darray_sort_multi_threaded(entries, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_name, cancellable, NULL);
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    // now build individual lists sorted by all the indexed metadata
    if ((flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        clear_fast_sorted_array(sorted_entries, DATABASE_INDEX_PROPERTY_SIZE);
        sorted_entries[DATABASE_INDEX_PROPERTY_SIZE] = darray_copy(entries);
        darray_sort_multi_threaded(sorted_entries[DATABASE_INDEX_PROPERTY_SIZE],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_size,
                                   cancellable,
                                   NULL);
        if (g_cancellable_is_cancelled(cancellable)) {
            return;
        }
    }

    if ((flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        clear_fast_sorted_array(sorted_entries, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME);
        sorted_entries[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] = darray_copy(entries);
        darray_sort_multi_threaded(sorted_entries[DATABASE_INDEX_PROPERTY_MODIFICATION_TIME],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_modification_time,
                                   cancellable,
                                   NULL);
        if (g_cancellable_is_cancelled(cancellable)) {
            return;
        }
    }
}

bool
fsearch_database_sort(DynamicArray **files_store,
                      DynamicArray **folders_store,
                      FsearchDatabaseIndexPropertyFlags flags,
                      GCancellable *cancellable) {
    g_return_val_if_fail(files_store, false);
    g_return_val_if_fail(folders_store, false);

    g_autoptr(GTimer) timer = g_timer_new();

    // first we sort all the files
    DynamicArray *files = files_store[DATABASE_INDEX_PROPERTY_NAME];
    if (files) {
        sort_store_entries(files, files_store, flags, cancellable);
        if (g_cancellable_is_cancelled(cancellable)) {
            return false;
        }

        // now build extension sort array
        clear_fast_sorted_array(files_store, DATABASE_INDEX_PROPERTY_EXTENSION);
        files_store[DATABASE_INDEX_PROPERTY_EXTENSION] = darray_copy(files);
        darray_sort_multi_threaded(files_store[DATABASE_INDEX_PROPERTY_EXTENSION],
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_extension,
                                   cancellable,
                                   NULL);
        if (g_cancellable_is_cancelled(cancellable)) {
            return false;
        }

        const double seconds = g_timer_elapsed(timer, NULL);
        g_timer_reset(timer);
        g_debug("[db_sort] sorted files: %f s", seconds);
    }

    // then we sort all the folders
    DynamicArray *folders = folders_store[DATABASE_INDEX_PROPERTY_NAME];
    if (folders) {
        sort_store_entries(folders, folders_store, flags, cancellable);
        if (g_cancellable_is_cancelled(cancellable)) {
            return false;
        }

        // Folders don't have a file extension -> use the name array instead
        clear_fast_sorted_array(folders_store, DATABASE_INDEX_PROPERTY_EXTENSION);
        folders_store[DATABASE_INDEX_PROPERTY_EXTENSION] = darray_copy(folders);

        const double seconds = g_timer_elapsed(timer, NULL);
        g_debug("[db_sort] sorted folders: %f s", seconds);
    }

    return true;
}

static int
compare_by_name(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const int res = db_entry_compare_entries_by_name(a, b);
    if (G_LIKELY(res != 0)) {
        return res;
    }
    else {
        return db_entry_compare_entries_by_path(a, b);
    }
}

static int
compare_by_size(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const int res = db_entry_compare_entries_by_size(a, b);
    if (G_LIKELY(res != 0)) {
        return res;
    }
    return compare_by_name(a, b);
}

static int
compare_by_modification_time(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const int res = db_entry_compare_entries_by_modification_time(a, b);
    if (G_LIKELY(res != 0)) {
        return res;
    }
    else {
        return compare_by_name(a, b);
    }
}

static int
compare_by_extension(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b) {
    const int res = db_entry_compare_entries_by_extension(a, b);
    if (G_LIKELY(res != 0)) {
        return res;
    }
    else {
        return compare_by_name(a, b);
    }
}

DynamicArrayCompareDataFunc
fsearch_database_sort_get_compare_func_for_property(FsearchDatabaseIndexProperty property, bool is_dir) {
    switch (property) {
    case DATABASE_INDEX_PROPERTY_NAME:
        return (DynamicArrayCompareDataFunc)compare_by_name;
    case DATABASE_INDEX_PROPERTY_PATH:
        return (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path;
    case DATABASE_INDEX_PROPERTY_SIZE:
        return (DynamicArrayCompareDataFunc)compare_by_size;
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        return (DynamicArrayCompareDataFunc)compare_by_modification_time;
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        // Folders don't have extensions and hence are simply sorted by name
        if (!is_dir) {
            return (DynamicArrayCompareDataFunc)compare_by_extension;
        }
        else {
            return (DynamicArrayCompareDataFunc)compare_by_name;
        }
    default:
        return NULL;
    }
}

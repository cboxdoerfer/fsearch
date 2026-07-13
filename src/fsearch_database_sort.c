#define G_LOG_DOMAIN "fsearch-database-sort"

#include "fsearch_database_sort.h"

#include "fsearch_database_entry.h"

#include <glib.h>

static bool
sort_order_affects_folders(FsearchDatabaseIndexProperty sort_order) {
    if (sort_order == DATABASE_INDEX_PROPERTY_EXTENSION || sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
        // Folders are stored in a different array than files, so they all have the same sort_order and extension
        // (none), therefore we don't need to sort them in such cases.
        return false;
    }
    return true;
}

FsearchDatabaseSortOrderChain
fsearch_database_sort_order_chain_for_property(FsearchDatabaseIndexProperty property) {
    FsearchDatabaseSortOrderChain chain = {};
    switch (property) {
    case DATABASE_INDEX_PROPERTY_NAME:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_NAME;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH;
        break;
    case DATABASE_INDEX_PROPERTY_PATH:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_NAME;
        break;
    case DATABASE_INDEX_PROPERTY_PATH_FULL:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH_FULL;
        break;
    case DATABASE_INDEX_PROPERTY_SIZE:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_SIZE;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_NAME;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH;
        break;
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_MODIFICATION_TIME;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_NAME;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH;
        break;
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_EXTENSION;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_NAME;
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_PATH;
        break;
    case DATABASE_INDEX_PROPERTY_FILETYPE:
        // No fast index exists for FILETYPE, so it has no natural continuation of its own;
        // callers doing a manual sort must extend this with the array's previous chain via
        // fsearch_database_sort_order_chain_prepend().
        chain.properties[chain.length++] = DATABASE_INDEX_PROPERTY_FILETYPE;
        break;
    default:
        break;
    }
    return chain;
}

FsearchDatabaseSortOrderChain
fsearch_database_sort_order_chain_prepend(FsearchDatabaseSortOrderChain chain, FsearchDatabaseIndexProperty property) {
    FsearchDatabaseSortOrderChain result = {};
    result.properties[result.length++] = property;
    for (uint32_t i = 0; i < chain.length && result.length < G_N_ELEMENTS(result.properties); ++i) {
        if (chain.properties[i] == property) {
            continue;
        }
        result.properties[result.length++] = chain.properties[i];
    }
    return result;
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
sort_entries(DynamicArray *entries_in, FsearchDatabaseSortOrderChain chain, GCancellable *cancellable, bool parallel_sort) {
    DynamicArray *entries = darray_copy(entries_in);
    g_autoptr(FsearchDatabaseEntryCompareContext) ctx = db_entry_compare_context_new(chain);
    if (parallel_sort) {
        darray_sort_multi_threaded(entries,
                                   (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_chain,
                                   cancellable,
                                   ctx);
    }
    else {
        darray_sort(entries, (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_chain, cancellable, ctx);
    }
    return entries;
}

static DynamicArray *
fast_sort(DynamicArray *entries_in, DynamicArray *fast_sort_index) {
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
fsearch_database_sort_results(FsearchDatabaseSortOrderChain old_chain,
                              FsearchDatabaseIndexProperty new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray *files_fast_sort_index,
                              DynamicArray *folders_fast_sort_index,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseSortOrderChain *chain_out,
                              GCancellable *cancellable) {
    g_return_if_fail(files_in);
    g_return_if_fail(folders_in);
    g_return_if_fail(files_out);
    g_return_if_fail(folders_out);
    g_return_if_fail(chain_out);

    const FsearchDatabaseIndexProperty old_sort_order = old_chain.length > 0 ? old_chain.properties[0]
                                                                             : DATABASE_INDEX_PROPERTY_NONE;

    if (old_sort_order == new_sort_order) {
        // Sort order didn't change, use the old results
        *files_out = darray_ref(files_in);
        *folders_out = darray_ref(folders_in);
        *chain_out = old_chain;
        return;
    }

    if (files_fast_sort_index && folders_fast_sort_index) {
        // Use the fast-sort indices; they're authoritative on their own and always fully ordered
        // (each fast-indexed property's comparator already chains down to NAME/PATH).
        *files_out = fast_sort(files_in, files_fast_sort_index);
        *folders_out = fast_sort(folders_in, folders_fast_sort_index);
        *chain_out = fsearch_database_sort_order_chain_for_property(new_sort_order);
        return;
    }

    // No fast index for `new_sort_order`: sort explicitly by the full chain [new_sort_order, ...
    // old_chain] instead of relying on sort stability to (only partially) preserve the previous
    // order. This keeps the array's actual order and the comparator used for later binary
    // searches (insert/steal/find) always in sync, however many manual sorts get layered on top
    // of each other.
    const FsearchDatabaseSortOrderChain new_chain = fsearch_database_sort_order_chain_prepend(old_chain, new_sort_order);

    bool parallel_sort = true;
    if (new_sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
        // Sorting by type can be really slow, because it accesses the filesystem to determine the type of files
        // To mitigate that issue to a certain degree we cache the filetype for each file
        // To avoid duplicating the filetype in memory for each file, we also store each filetype only once in
        // a separate hash table.
        // We also disable parallel sorting.
        parallel_sort = false;
    }

    if (sort_order_affects_folders(new_sort_order)) {
        *folders_out = sort_entries(folders_in, new_chain, cancellable, parallel_sort);
    }
    else {
        *folders_out = darray_copy(folders_in);
    }
    *files_out = sort_entries(files_in, new_chain, cancellable, parallel_sort);
    *chain_out = new_chain;

    if (g_cancellable_is_cancelled(cancellable)) {
        g_clear_pointer(folders_out, darray_unref);
        g_clear_pointer(files_out, darray_unref);
        *chain_out = old_chain;
    }
}

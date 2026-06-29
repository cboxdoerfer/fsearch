#define G_LOG_DOMAIN "fsearch-database-chunked-array"

#include "fsearch_database_chunked_array.h"
#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_sort.h"

#include <gio/giotypes.h>
#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <math.h>

struct _FsearchDatabaseChunkedArray {
    DynamicArray *chunks;

    uint32_t num_entries;
    uint32_t target_chunk_size;

    FsearchDatabaseIndexProperty sort_order;
    FsearchDatabaseIndexProperty secondary_sort_order;

    FsearchDatabaseEntryType entry_type;
    DynamicArrayCompareDataFunc entry_comp_func;
    DynamicArrayCompareDataFunc secondary_entry_comp_func;

    FsearchDatabaseEntryCompareContext *compare_context;

    GDestroyNotify entry_free_func;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseChunkedArray,
                    fsearch_database_chunked_array,
                    fsearch_database_chunked_array_ref,
                    fsearch_database_chunked_array_unref)

#define TARGET_CHUNK_SIZE 2048
#define MIN_ENTRIES_FOR_BULK_INSERT 8192

static int32_t
chunk_compare_func(DynamicArray **chunk_ptr, FsearchDatabaseEntry **entry_ptr, FsearchDatabaseChunkedArray *self) {
    DynamicArray *chunk = *chunk_ptr;
    g_assert(darray_get_num_items(chunk) > 0);

    FsearchDatabaseEntry *first_chunk_entry = darray_get_item(chunk, 0);
    FsearchDatabaseEntry *last_chunk_entry = darray_get_item(chunk, darray_get_num_items(chunk) - 1);

    const int32_t res_a = self->entry_comp_func((void *)&first_chunk_entry, (void *)entry_ptr, self->compare_context);
    const int32_t res_b = self->entry_comp_func((void *)&last_chunk_entry, (void *)entry_ptr, self->compare_context);
    if (res_a <= 0 && res_b >= 0) {
        return 0;
    }
    return res_a;
}

static DynamicArray *
get_chunk_for_entry(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry, uint32_t *chunk_idx_out) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(entry, NULL);

    uint32_t chunk_idx = 0;
    if (darray_get_num_items(self->chunks) == 0) {
        // There's no chunk -> add one
        darray_insert_item(self->chunks, darray_new_full(self->target_chunk_size, self->entry_free_func), 0);
        chunk_idx = 0;
    }
    else if (darray_get_num_items(self->chunks) == 1) {
        // There's only one chunk, use that one
        chunk_idx = 0;
    }
    else {
        darray_binary_search_with_data(self->chunks,
                                       entry,
                                       (DynamicArrayCompareDataFunc)chunk_compare_func,
                                       self,
                                       &chunk_idx);
        chunk_idx = MIN(chunk_idx, darray_get_num_items(self->chunks) - 1);
    }

    DynamicArray *chunk = darray_get_item(self->chunks, chunk_idx);
    g_assert_nonnull(chunk);
    if (chunk_idx_out) {
        *chunk_idx_out = chunk_idx;
    }
    return chunk;
}

static uint32_t
count_num_entries(DynamicArray *chunks) {
    uint32_t num_entries = 0;
    for (uint32_t i = 0; i < darray_get_num_items(chunks); ++i) {
        num_entries += darray_get_num_items(darray_get_item(chunks, i));
    }
    return num_entries;
}

static DynamicArray *
split_chunk(DynamicArray *chunk, uint32_t target_chunk_size, GDestroyNotify entry_free_func) {
    g_assert(chunk);

    const uint32_t num_items_in_chunk = darray_get_num_items(chunk);
    if (num_items_in_chunk <= target_chunk_size) {
        DynamicArray *chunks = darray_new(1);
        DynamicArray *chunk_copy = darray_copy(chunk);
        darray_set_free_func(chunk_copy, entry_free_func);
        darray_add_item(chunks, chunk_copy);
        return chunks;
    }

    const uint32_t num_chunks = ceil(num_items_in_chunk / (double)target_chunk_size);
    const uint32_t num_items_per_chunk = floor(num_items_in_chunk / (double)num_chunks);

    g_debug("[chunk] splitting: %d", num_items_in_chunk);
    g_debug("[chunk] num_chunks: %d", num_chunks);
    g_debug("[chunk] num_items_per_chunk: %d", num_items_per_chunk);

    DynamicArray *chunks = darray_new(num_chunks);
    for (uint32_t n = 0; n < num_chunks; ++n) {
        DynamicArray *chunk_slice = darray_get_range(chunk,
                                                     n * num_items_per_chunk,
                                                     n + 1 == num_chunks ? UINT32_MAX : num_items_per_chunk);
        darray_set_free_func(chunk_slice, entry_free_func);
        darray_add_item(chunks, chunk_slice);
    }

    g_assert(num_items_in_chunk == count_num_entries(chunks));

    return chunks;
}

static void
balance_chunk(FsearchDatabaseChunkedArray *self, DynamicArray *chunk, uint32_t chunk_idx) {
    if (darray_get_num_items(chunk) == 0) {
        if (darray_get_num_items(self->chunks) == 1) {
            // Don't remove the last chunk
            return;
        }
        g_debug("[balance_chunk] remove empty: %d", chunk_idx);
        darray_remove(self->chunks, chunk_idx, 1);
        // Make sure to set free_func to NULL, to avoid entries being freed
        darray_set_free_func(chunk, NULL);
        g_clear_pointer(&chunk, darray_unref);
        return;
    }

    if (darray_get_num_items(chunk) < 2 * self->target_chunk_size) {
        return;
    }

    g_autoptr(DynamicArray) splitted = split_chunk(chunk, self->target_chunk_size, self->entry_free_func);

    g_debug("[balance_chunk] split idx %d with %d entries into %d chunks",
            chunk_idx,
            darray_get_num_items(chunk),
            darray_get_num_items(splitted));

    darray_remove(self->chunks, chunk_idx, 1);
    // Make sure to set free_func to NULL, to avoid entries being freed
    darray_set_free_func(chunk, NULL);
    g_clear_pointer(&chunk, darray_unref);

    for (uint32_t i = 0; i < darray_get_num_items(splitted); ++i) {
        DynamicArray *c = darray_get_item(splitted, i);
        darray_insert_item(self->chunks, c, chunk_idx++);
    }
}

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_new(DynamicArray *array,
                                   gboolean is_array_sorted,
                                   FsearchDatabaseIndexProperty sort_order,
                                   FsearchDatabaseIndexProperty secondary_sort_order,
                                   FsearchDatabaseEntryType entry_type,
                                   GCancellable *cancellable,
                                   GDestroyNotify entry_free_func) {
    g_return_val_if_fail(array, NULL);

    FsearchDatabaseChunkedArray *self = g_new0(FsearchDatabaseChunkedArray, 1);

    self->target_chunk_size = TARGET_CHUNK_SIZE;

    self->sort_order = sort_order;
    self->secondary_sort_order = secondary_sort_order;

    self->entry_type = entry_type;
    self->entry_comp_func = fsearch_database_sort_get_compare_func_for_property(
        self->sort_order,
        self->entry_type == DATABASE_ENTRY_TYPE_FOLDER ? true : false);

    self->secondary_entry_comp_func = fsearch_database_sort_get_compare_func_for_property(
        self->secondary_sort_order,
        self->entry_type == DATABASE_ENTRY_TYPE_FOLDER ? true : false);

    if (self->sort_order == DATABASE_INDEX_PROPERTY_FILETYPE) {
        self->compare_context = db_entry_compare_context_new(self->secondary_entry_comp_func, NULL, NULL);
    }

    if (!is_array_sorted) {
        darray_sort_multi_threaded(array, self->entry_comp_func, cancellable, self->compare_context);
    }

    self->num_entries = darray_get_num_items(array);

    self->entry_free_func = entry_free_func;
    self->chunks = split_chunk(array, self->target_chunk_size, self->entry_free_func);

    self->ref_count = 1;

    return self;
}

FsearchDatabaseChunkedArray *
fsearch_database_chunked_array_ref(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_chunked_array_unref(FsearchDatabaseChunkedArray *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        for (uint32_t i = 0; i < darray_get_num_items(self->chunks); ++i) {
            darray_unref(darray_get_item(self->chunks, i));
        }
        g_clear_pointer(&self->chunks, darray_unref);
        g_clear_pointer(&self->compare_context, db_entry_compare_context_free);
        g_free(self);
    }
}

void
fsearch_database_chunked_array_insert(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry) {
    g_return_if_fail(self);
    g_return_if_fail(db_entry_get_type(entry) == self->entry_type);

    uint32_t chunk_idx = 0;
    DynamicArray *chunk = get_chunk_for_entry(self, entry, &chunk_idx);

    darray_insert_item_sorted(chunk, entry, self->entry_comp_func, self->compare_context);
    self->num_entries++;

    balance_chunk(self, chunk, chunk_idx);
}

static DynamicArray *
get_next_nonempty_chunk(DynamicArray *chunks, uint32_t start_chunk_idx, uint32_t *out_chunk_idx) {
    while (start_chunk_idx < darray_get_num_items(chunks)) {
        DynamicArray *chunk = darray_get_item(chunks, start_chunk_idx);
        if (darray_get_num_items(chunk) > 0) {
            *out_chunk_idx = start_chunk_idx;
            return chunk;
        }
        start_chunk_idx++;
    }
    return NULL;
}

void
fsearch_database_chunked_array_insert_array(FsearchDatabaseChunkedArray *self, DynamicArray *array) {
    g_return_if_fail(self);
    g_return_if_fail(array);

    const uint32_t num_new_entries = darray_get_num_items(array);
    if (num_new_entries == 0) {
        return;
    }

    // If the number of items being inserted is small,
    // the overhead of flattening/merging isn't worth it.
    // Fall back to lookups + memmoves.
    const uint64_t individual_insertion_cost = num_new_entries * self->target_chunk_size;
    const uint64_t bulk_insertion_cost = num_new_entries + self->num_entries;
    if ((num_new_entries < MIN_ENTRIES_FOR_BULK_INSERT || individual_insertion_cost < bulk_insertion_cost)) {
        for (uint32_t i = 0; i < num_new_entries; ++i) {
            FsearchDatabaseEntry *entry = darray_get_item(array, i);
            fsearch_database_chunked_array_insert(self, entry);
        }
        // g_print("slow array insert: %f\n", g_timer_elapsed(timer, NULL));
        return;
    }

    g_autoptr(DynamicArray) sorted_new_entries = darray_copy_borrowed(array);
    darray_sort(sorted_new_entries, self->entry_comp_func, NULL, self->compare_context);

    // Calculate the exact even distribution of our new chunks
    const uint32_t total_entries = self->num_entries + num_new_entries;
    const uint32_t num_chunks_target = ceil(total_entries / (double)self->target_chunk_size);
    const uint32_t num_items_per_chunk = floor(total_entries / (double)num_chunks_target);

    g_autoptr(DynamicArray) new_chunks = darray_new(num_chunks_target);

    // Allocate slightly extra space in case the final chunk absorbs the remainder
    g_autoptr(DynamicArray) current_chunk = darray_new_full(num_items_per_chunk + 2, self->entry_free_func);
    uint32_t chunks_created = 0;

    uint32_t old_chunk_idx = 0;
    uint32_t old_entry_idx = 0;
    uint32_t new_entry_idx = 0;

    // Find the first chunk which actually contains entries
    DynamicArray *current_old_chunk = get_next_nonempty_chunk(self->chunks, old_chunk_idx, &old_chunk_idx);

    // Merge directly into evenly-sized chunks
    while (current_old_chunk != NULL || new_entry_idx < num_new_entries) {
        void *entry_to_add = NULL;

        if (current_old_chunk != NULL && new_entry_idx < num_new_entries) {
            // Find entry to insert
            void *entry_old = darray_get_item(current_old_chunk, old_entry_idx);
            void *entry_new = darray_get_item(sorted_new_entries, new_entry_idx);

            if (self->entry_comp_func(&entry_old, &entry_new, self->compare_context) <= 0) {
                entry_to_add = entry_old;
                old_entry_idx++;
            }
            else {
                entry_to_add = entry_new;
                new_entry_idx++;
            }
        }
        else if (current_old_chunk != NULL) {
            entry_to_add = darray_get_item(current_old_chunk, old_entry_idx);
            old_entry_idx++;
        }
        else {
            entry_to_add = darray_get_item(sorted_new_entries, new_entry_idx);
            new_entry_idx++;
        }

        // Advance to the next old chunk if we've exhausted the current one
        if (current_old_chunk != NULL && old_entry_idx >= darray_get_num_items(current_old_chunk)) {
            old_chunk_idx++;
            current_old_chunk = get_next_nonempty_chunk(self->chunks, old_chunk_idx, &old_chunk_idx);
            old_entry_idx = 0;
        }

        // Add the "smaller" entry to our new chunk
        darray_add_item(current_chunk, entry_to_add);

        // Add the chunk if it hits the calculated even size
        // Ensure the very last chunk is allowed to absorb a few more entries
        if (darray_get_num_items(current_chunk) == num_items_per_chunk && chunks_created < num_chunks_target - 1) {
            darray_add_item(new_chunks, g_steal_pointer(&current_chunk));
            chunks_created++;
            current_chunk = darray_new_full(num_items_per_chunk + 2, self->entry_free_func);
        }
    }

    // Add the final remainder chunk
    if (darray_get_num_items(current_chunk) > 0) {
        darray_add_item(new_chunks, g_steal_pointer(&current_chunk));
    }

    // Clean up old chunks (preventing entries from being freed)
    for (uint32_t c = 0; c < darray_get_num_items(self->chunks); ++c) {
        DynamicArray *chunk = darray_get_item(self->chunks, c);
        darray_set_free_func(chunk, NULL);
    }
    g_clear_pointer(&self->chunks, darray_unref);

    // Commit the new chunk array
    self->chunks = g_steal_pointer(&new_chunks);
    self->num_entries = total_entries;
}

FsearchDatabaseEntry *
fsearch_database_chunked_array_find(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(db_entry_get_type(entry) == self->entry_type, NULL);

    if (self->num_entries == 0) {
        g_debug("[chunks] empty");
        return NULL;
    }
    DynamicArray *chunk = get_chunk_for_entry(self, entry, NULL);

    uint32_t entry_idx = 0;
    if (darray_binary_search_with_data(chunk, entry, self->entry_comp_func, self->compare_context, &entry_idx)) {
        return darray_get_item(chunk, entry_idx);
    }
    return NULL;
}

FsearchDatabaseEntry *
fsearch_database_chunked_array_steal(FsearchDatabaseChunkedArray *self, FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(self, NULL);
    g_assert(db_entry_get_type(entry) == self->entry_type);
    g_return_val_if_fail(db_entry_get_type(entry) == self->entry_type, NULL);

    if (self->num_entries == 0) {
        return NULL;
    }
    uint32_t chunk_idx = 0;
    DynamicArray *chunk = get_chunk_for_entry(self, entry, &chunk_idx);

    uint32_t idx = 0;
    if (darray_binary_search_with_data(chunk, entry, self->entry_comp_func, self->compare_context, &idx)) {
        FsearchDatabaseEntry *e = darray_steal_item(chunk, idx);
        self->num_entries--;

        balance_chunk(self, chunk, chunk_idx);
        return e;
    }
    return NULL;
}

static inline bool
is_marked(FsearchDatabaseEntry *entry, FsearchDatabaseEntryType type) {
    if (type == DATABASE_ENTRY_TYPE_FILE) {
        if (db_entry_get_mark(entry) == 1) {
            return true;
        }
        entry = db_entry_get_parent(entry);
    }
    if (!entry) {
        return false;
    }
    return db_entry_get_mark(entry) == 1 ? true : false;
}

// TODO: Refactor to avoid code duplication
DynamicArray *
fsearch_database_chunked_array_steal_marked_folders(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self, 0);

    uint32_t chunk_idx = 0;
    uint32_t entry_start_idx = 0;

    DynamicArray *marked_entries = darray_new(128);

    while (chunk_idx < darray_get_num_items(self->chunks)) {
        DynamicArray *chunk = darray_get_item(self->chunks, chunk_idx);
        uint32_t entry_idx = entry_start_idx;
        while (entry_idx < darray_get_num_items(chunk)) {
            FsearchDatabaseEntry *maybe_marked = darray_get_item(chunk, entry_idx);
            if (is_marked(maybe_marked, self->entry_type)) {
                uint32_t n_elements = 1;

                // Peek ahead to find the full contiguous block of marked_entries
                while (entry_idx + n_elements < darray_get_num_items(chunk)) {
                    FsearchDatabaseEntry *next_entry = darray_get_item(chunk, entry_idx + n_elements);
                    if (!is_marked(next_entry, self->entry_type)) {
                        break; // End of contiguous block
                    }
                    n_elements++;
                }

                // Steal the entire contiguous block at once to minimize memmoves
                darray_steal(chunk, entry_idx, n_elements, marked_entries);

                // Note: Do NOT increment entry_idx here.
                // Stealing the elements shifts the rest of the array left,
                // so the next unchecked item naturally falls into the current entry_idx.
            }
            else {
                // Only move forward if we didn't remove anything
                entry_idx++;
            }
        }
        // Remove the chunk if it became empty
        if (darray_get_num_items(chunk) == 0) {
            darray_remove(self->chunks, chunk_idx, 1);
            g_clear_pointer(&chunk, darray_unref);
        }
        else {
            chunk_idx++;
        }
        // We must set the start index back to zero before we move on to the next entry chunk
        entry_start_idx = 0;
    }

    self->num_entries -= darray_get_num_items(marked_entries);
    return marked_entries;
}

uint32_t
fsearch_database_chunked_array_remove_marked_folders(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self, 0);

    uint32_t chunk_idx = 0;
    uint32_t entry_start_idx = 0;
    uint32_t removed_entries = 0;
    while (chunk_idx < darray_get_num_items(self->chunks)) {
        DynamicArray *chunk = darray_get_item(self->chunks, chunk_idx);
        uint32_t entry_idx = entry_start_idx;
        while (entry_idx < darray_get_num_items(chunk)) {
            FsearchDatabaseEntry *maybe_marked = darray_get_item(chunk, entry_idx);
            if (is_marked(maybe_marked, self->entry_type)) {
                uint32_t n_elements = 1;

                // Peek ahead to find the full contiguous block of marked_entries
                while (entry_idx + n_elements < darray_get_num_items(chunk)) {
                    FsearchDatabaseEntry *next_entry = darray_get_item(chunk, entry_idx + n_elements);
                    if (!is_marked(next_entry, self->entry_type)) {
                        break; // End of contiguous block
                    }
                    n_elements++;
                }

                // Steal the entire contiguous block at once to minimize memmoves
                removed_entries += darray_remove(chunk, entry_idx, n_elements);

                // Note: Do NOT increment entry_idx here.
                // Stealing the elements shifts the rest of the array left,
                // so the next unchecked item naturally falls into the current entry_idx.
            }
            else {
                // Only move forward if we didn't remove anything
                entry_idx++;
            }
        }
        // Remove the chunk if it became empty
        if (darray_get_num_items(chunk) == 0) {
            darray_remove(self->chunks, chunk_idx, 1);
            g_clear_pointer(&chunk, darray_unref);
        }
        else {
            chunk_idx++;
        }
        // We must set the start index back to zero before we move on to the next entry chunk
        entry_start_idx = 0;
    }

    self->num_entries -= removed_entries;
    return removed_entries;
}

DynamicArray *
fsearch_database_chunked_array_steal_descendants(FsearchDatabaseChunkedArray *self,
                                                 FsearchDatabaseEntry *folder,
                                                 int32_t num_known_descendants) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(folder, NULL);

    uint32_t chunk_idx = 0;
    uint32_t entry_start_idx = 0;
    if (self->sort_order == DATABASE_INDEX_PROPERTY_PATH_FULL) {
        DynamicArray *chunk = get_chunk_for_entry(self, folder, &chunk_idx);
        darray_binary_search_with_data(chunk, folder, self->entry_comp_func, self->compare_context, &entry_start_idx);
    }

    DynamicArray *descendants = darray_new(num_known_descendants >= 0 ? num_known_descendants : 128);

    uint32_t num_known_descendants_stolen = 0;

    while (chunk_idx < darray_get_num_items(self->chunks)) {
        if (num_known_descendants == num_known_descendants_stolen) {
            // We've found all known descendants and are done here.
            break;
        }
        DynamicArray *chunk = darray_get_item(self->chunks, chunk_idx);
        uint32_t entry_idx = entry_start_idx;

        if (num_known_descendants >= 0 && self->sort_order == DATABASE_INDEX_PROPERTY_PATH_FULL) {
            // We know the exact number of descendants, and due to the `DATABASE_INDEX_PROPERTY_PATH_FULL` sort type,
            // it is guaranteed that they are all sorted next to each other. Therefore, we can use an optimized code
            // path where we steal them in large chunks, instead of one by one.
            // It's also safe to not clamp n_elements since darray_steal will only steal the available number of
            // elements and report the actual amount stolen
            num_known_descendants_stolen += darray_steal(chunk,
                                                         entry_start_idx,
                                                         num_known_descendants - num_known_descendants_stolen,
                                                         descendants);
        }
        else {
            // Unfortunately, we have to steal/remove descendants one by one.
            while (entry_idx < darray_get_num_items(chunk)) {
                FsearchDatabaseEntry *maybe_descendant = darray_get_item(chunk, entry_idx);
                if (db_entry_is_descendant(maybe_descendant, folder)) {
                    darray_add_item(descendants, maybe_descendant);
                    darray_remove(chunk, entry_idx, 1);
                    continue;
                }
                entry_idx++;
            }
        }
        // We must set the start index back to zero before we move on to the next entry chunk
        entry_start_idx = 0;

        // Remove the chunk if it became empty
        if (darray_get_num_items(chunk) == 0) {
            darray_remove(self->chunks, chunk_idx, 1);
            g_clear_pointer(&chunk, darray_unref);
        }
        else {
            chunk_idx++;
        }
    }

    if (num_known_descendants >= 0) {
        // Ensure that we got the exact number of descendants
        g_assert(num_known_descendants == darray_get_num_items(descendants));
    }

    self->num_entries -= darray_get_num_items(descendants);

    return descendants;
}

FsearchDatabaseEntry *
fsearch_database_chunked_array_get_entry(FsearchDatabaseChunkedArray *self, uint32_t idx) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(idx < self->num_entries, NULL);

    for (uint32_t i = 0; i < darray_get_num_items(self->chunks); ++i) {
        DynamicArray *chunk = darray_get_item(self->chunks, i);
        const uint32_t num_items = darray_get_num_items(chunk);
        if (idx < num_items) {
            return darray_get_item(chunk, idx);
        }
        idx -= num_items;
    }
    return NULL;
}

uint32_t
fsearch_database_chunked_array_get_num_entries(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self, 0);
    return self->num_entries;
}

DynamicArray *
fsearch_database_chunked_array_get_chunks(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self, NULL);

    return darray_ref(self->chunks);
}

DynamicArray *
fsearch_database_chunked_array_get_joined(FsearchDatabaseChunkedArray *self) {
    g_return_val_if_fail(self, NULL);

    DynamicArray *joined = darray_new(self->num_entries);
    for (uint32_t i = 0; i < darray_get_num_items(self->chunks); ++i) {
        DynamicArray *chunk = darray_get_item(self->chunks, i);
        darray_add_array(joined, chunk);
    }
    return joined;
}
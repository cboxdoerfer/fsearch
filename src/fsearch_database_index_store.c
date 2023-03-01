#include "fsearch_database_index_store.h"

#include <glib.h>

#include "fsearch_database_sort.h"

struct _FsearchDatabaseIndexStore {
    GPtrArray *indices;

    DynamicArray *files_sorted[NUM_DATABASE_INDEX_PROPERTIES];
    DynamicArray *folders_sorted[NUM_DATABASE_INDEX_PROPERTIES];

    FsearchDatabaseIndexPropertyFlags flags;

    bool is_sorted;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndexStore,
                    fsearch_database_index_store,
                    fsearch_database_index_store_ref,
                    fsearch_database_index_store_unref)

static bool
has_flag(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
    g_assert(self);
    g_assert(index);

    const FsearchDatabaseIndexPropertyFlags store_flags = self->flags;
    const FsearchDatabaseIndexPropertyFlags index_flags = fsearch_database_index_get_flags(index);

    return (store_flags & index_flags) == store_flags;
}

static bool
index_store_has_index_with_same_id(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
    g_assert(self);
    g_assert(index);

    for (uint32_t i = 0; i < self->indices->len; ++i) {
        FsearchDatabaseIndex *index_stored = g_ptr_array_index(self->indices, i);
        if (fsearch_database_index_get_id(index_stored) == fsearch_database_index_get_id(index)) {
            return true;
        }
    }
    return false;
}

static void
index_store_free(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self);

    g_clear_pointer(&self->indices, g_ptr_array_unref);

    g_slice_free(FsearchDatabaseIndexStore, self);
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseIndexStore *self;
    self = g_slice_new0(FsearchDatabaseIndexStore);

    self->indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    self->flags = flags;
    self->is_sorted = true;

    self->files_sorted[DATABASE_INDEX_PROPERTY_NAME] = darray_new(1024);
    self->folders_sorted[DATABASE_INDEX_PROPERTY_NAME] = darray_new(1024);

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self, index_store_free);
    }
}

void
fsearch_database_index_store_add(FsearchDatabaseIndexStore *self, FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(index);
    g_return_if_fail(!index_store_has_index_with_same_id(self, index));
    g_return_if_fail(has_flag(self, index));

    g_ptr_array_add(self->indices, fsearch_database_index_ref(index));

    g_autoptr(DynamicArray) files = fsearch_database_index_get_files(index);
    g_autoptr(DynamicArray) folders = fsearch_database_index_get_folders(index);
    darray_add_array(self->files_sorted[DATABASE_INDEX_PROPERTY_NAME], files);
    darray_add_array(self->folders_sorted[DATABASE_INDEX_PROPERTY_NAME], folders);
    self->is_sorted = false;
}

void
fsearch_database_index_store_add_sorted(FsearchDatabaseIndexStore *self,
                                        FsearchDatabaseIndex *index,
                                        GCancellable *cancellable) {
    g_return_if_fail(self);
    g_return_if_fail(index);
    g_return_if_fail(!index_store_has_index_with_same_id(self, index));
    g_return_if_fail(!has_flag(self, index));

    fsearch_database_index_store_add(self, index);

    self->is_sorted = fsearch_database_sort(self->files_sorted, self->folders_sorted, self->flags, cancellable);
}

void
fsearch_database_index_store_sort(FsearchDatabaseIndexStore *self, GCancellable *cancellable) {
    g_return_if_fail(self);
    if (self->is_sorted) {
        return;
    }
    self->is_sorted = fsearch_database_sort(self->files_sorted, self->folders_sorted, self->flags, cancellable);
}

DynamicArray *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(self->is_sorted, NULL);

    return self->files_sorted[sort_order] ? darray_ref(self->files_sorted[sort_order]) : NULL;
}

DynamicArray *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(self->is_sorted, NULL);

    return self->folders_sorted[sort_order] ? darray_ref(self->folders_sorted[sort_order]) : NULL;
}

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    uint32_t num_fast_sort_indices = 0;
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        if (self->files_sorted[i] && self->folders_sorted[i]) {
            num_fast_sort_indices++;
        }
    }

    return num_fast_sort_indices;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return self->flags;
}

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return darray_get_num_items(self->files_sorted[0]);
}

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *self) {
    g_return_val_if_fail(self, 0);

    return darray_get_num_items(self->folders_sorted[0]);
}

void
fsearch_database_index_store_remove_entry(FsearchDatabaseIndexStore *self,
                                          FsearchDatabaseEntry *entry,
                                          FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(entry);
    g_return_if_fail(index);

    if (!g_ptr_array_find(self->indices, index, NULL)) {
        g_debug("[index_store_remove] index does not belong to index store; must be a bug");
        g_assert_not_reached();
    }

    fsearch_database_index_lock(index);
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        DynamicArray *array = NULL;
        if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER) {
            array = self->folders_sorted[i];
        }
        else {
            array = self->files_sorted[i];
        }

        if (!array) {
            continue;
        }
        DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(i);
        g_assert(comp_func);
        uint32_t entry_index = 0;
        if (darray_binary_search_with_data(array, entry, comp_func, NULL, &entry_index)) {
            darray_remove(array, entry_index, 1);
        }
        else {
            // It's most certainly a bug when we reach this section. Still, we try if we can find the entry by simply
            // walking through the array from start to end before we abort.
            g_autoptr(GString) entry_path = db_entry_get_path_full(entry);
            if (darray_get_item_idx(array, entry, NULL, NULL, &entry_index)) {
                g_debug("[index_store_remove] brute force search found entry at %d: %s\n", entry_index, entry_path->str);
            }
            else {
                g_debug("[index_store_remove] didn't find entry: %s", entry_path->str);
            }
            g_assert_not_reached();
        }
    }
    fsearch_database_index_unlock(index);
}

void
fsearch_database_index_store_add_entry(FsearchDatabaseIndexStore *self,
                                       FsearchDatabaseEntry *entry,
                                       FsearchDatabaseIndex *index) {
    g_return_if_fail(self);
    g_return_if_fail(entry);
    g_return_if_fail(index);

    fsearch_database_index_lock(index);

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; ++i) {
        DynamicArray *array = NULL;
        if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER) {
            array = self->folders_sorted[i];
        }
        else {
            array = self->files_sorted[i];
        }

        if (!array) {
            continue;
        }
        DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(i);
        g_assert(comp_func);

        darray_insert_item_sorted(array, entry, comp_func, NULL);
    }

    fsearch_database_index_unlock(index);
}

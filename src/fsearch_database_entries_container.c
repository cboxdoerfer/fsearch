#define G_LOG_DOMAIN "fsearch-database-entries-container"

#include "fsearch_database_entries_container.h"

#include <glib.h>

#include <math.h>

struct _FsearchDatabaseEntriesContainer {
    DynamicArray *container;

    uint32_t num_entries;
    uint32_t ideal_entries_per_container;

    FsearchDatabaseIndexProperty sort_type;
    FsearchDatabaseEntryType entry_type;
    DynamicArrayCompareDataFunc entry_comp_func;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseEntriesContainer,
                    fsearch_database_entries_container,
                    fsearch_database_entries_container_ref,
                    fsearch_database_entries_container_unref)

static int32_t
container_compare_func(DynamicArray **a, FsearchDatabaseEntry **b, DynamicArrayCompareDataFunc comp_func) {
    DynamicArray *array = *a;
    g_assert(darray_get_num_items(array) > 0);

    FsearchDatabaseEntry *entry_a = darray_get_item(array, 0);
    FsearchDatabaseEntry *entry_b = darray_get_item(array, darray_get_num_items(array) - 1);

    const int32_t res_a = comp_func(&entry_a, b, NULL);
    const int32_t res_b = comp_func(&entry_b, b, NULL);
    if (res_a <= 0 && res_b >= 0) {
        return 0;
    }
    return res_a;
}

static DynamicArray *
get_container_for_entry(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntry *entry, uint32_t *container_idx_out) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(entry, NULL);

    if (darray_get_num_items(self->container) == 1) {
        return darray_get_item(self->container, 0);
    }

    uint32_t container_idx = 0;
    darray_binary_search_with_data(self->container,
                                   entry,
                                   (DynamicArrayCompareDataFunc)container_compare_func,
                                   self->entry_comp_func,
                                   &container_idx);
    container_idx = MIN(container_idx, darray_get_num_items(self->container) - 1);
    DynamicArray *container = darray_get_item(self->container, container_idx);
    g_assert_nonnull(container);
    if (container_idx_out) {
        *container_idx_out = container_idx;
    }
    return container;
}

static uint32_t
count_num_entries(DynamicArray *containers) {
    uint32_t n_elements = 0;
    for (uint32_t i = 0; i < darray_get_num_items(containers); ++i) {
        n_elements += darray_get_num_items(darray_get_item(containers, i));
    }
    return n_elements;
}

static DynamicArray *
split_array(DynamicArray *array, uint32_t ideal_entries_per_array) {
    g_assert(array);

    const uint32_t num_items = darray_get_num_items(array);
    if (num_items <= ideal_entries_per_array) {
        DynamicArray *splitted = darray_new(1);
        darray_add_item(splitted, darray_copy(array));
        return splitted;
    }

    const uint32_t num_splits = ceil(num_items / (double)ideal_entries_per_array);
    const uint32_t num_items_per_split = floor(num_items / (double)num_splits);

    g_debug("[container] splitting: %d", num_items);
    g_debug("[container] num_splits: %d", num_splits);
    g_debug("[container] num_items_per_split: %d", num_items_per_split);

    DynamicArray *splitted = darray_new(num_splits);
    for (uint32_t n = 0; n < num_splits; ++n) {
        DynamicArray *a =
            darray_get_range(array, n * num_items_per_split, n + 1 == num_splits ? UINT32_MAX : num_items_per_split);
        darray_add_item(splitted, a);
    }

    g_assert(num_items == count_num_entries(splitted));

    return splitted;
}

static void
balance_container(FsearchDatabaseEntriesContainer *self, DynamicArray *container, uint32_t c_idx) {
    if (darray_get_num_items(container) == 0) {
        g_debug("[balance_container] remove empty: %d", c_idx);
        darray_remove(self->container, c_idx, 1);
        g_clear_pointer(&container, darray_unref);
        return;
    }

    if (darray_get_num_items(container) < 2 * self->ideal_entries_per_container) {
        return;
    }

    g_autoptr(DynamicArray) splitted = split_array(container, self->ideal_entries_per_container);

    g_debug("[balance_container] split idx %d with %d entries into %d containers",
            c_idx,
            darray_get_num_items(container),
            darray_get_num_items(splitted));

    darray_remove(self->container, c_idx, 1);
    g_clear_pointer(&container, darray_unref);

    for (uint32_t i = 0; i < darray_get_num_items(splitted); ++i) {
        DynamicArray *c = darray_get_item(splitted, i);
        darray_insert_item(self->container, c, c_idx++);
    }
}

FsearchDatabaseEntriesContainer *
fsearch_database_entries_container_new(DynamicArray *array,
                                       gboolean is_array_sorted,
                                       FsearchDatabaseIndexProperty sort_type,
                                       FsearchDatabaseEntryType entry_type,
                                       GCancellable *cancellable) {
    FsearchDatabaseEntriesContainer *self;

    g_return_val_if_fail(array, NULL);

    self = g_slice_new0(FsearchDatabaseEntriesContainer);

    self->ideal_entries_per_container = 8192;
    self->sort_type = sort_type;
    self->entry_type = entry_type;
    self->entry_comp_func = fsearch_database_sort_get_compare_func_for_property(
        self->sort_type,
        self->entry_type == DATABASE_ENTRY_TYPE_FOLDER ? true : false);

    if (!is_array_sorted) {
        darray_sort_multi_threaded(array, self->entry_comp_func, cancellable, NULL);
    }

    self->num_entries = darray_get_num_items(array);
    self->container = split_array(array, self->ideal_entries_per_container);

    self->ref_count = 1;

    return self;
}

FsearchDatabaseEntriesContainer *
fsearch_database_entries_container_ref(FsearchDatabaseEntriesContainer *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_entries_container_unref(FsearchDatabaseEntriesContainer *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        for (uint32_t i = 0; i < darray_get_num_items(self->container); ++i) {
            darray_unref(darray_get_item(self->container, i));
        }
        g_clear_pointer(&self->container, darray_unref);
        g_slice_free(FsearchDatabaseEntriesContainer, self);
    }
}

void
fsearch_database_entries_container_insert(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntry *entry) {
    g_return_if_fail(self);
    g_return_if_fail(db_entry_get_type(entry) == self->entry_type);

    uint32_t c_idx = 0;
    DynamicArray *c = get_container_for_entry(self, entry, &c_idx);

    darray_insert_item_sorted(c, entry, self->entry_comp_func, NULL);
    self->num_entries++;

    balance_container(self, c, c_idx);
}

FsearchDatabaseEntry *
fsearch_database_entries_container_find(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(db_entry_get_type(entry) == self->entry_type, NULL);

    if (self->num_entries == 0) {
        g_debug("[container] empty");
        return NULL;
    }
    DynamicArray *c = get_container_for_entry(self, entry, NULL);

    uint32_t idx = 0;
    if (darray_binary_search_with_data(c, entry, self->entry_comp_func, NULL, &idx)) {
        return darray_get_item(c, idx);
    }
    g_autoptr(GString) path = db_entry_get_path_full(entry);
    g_debug("[container_find] entry not found: %s", path->str);
    return NULL;
}

FsearchDatabaseEntry *
fsearch_database_entries_container_steal(FsearchDatabaseEntriesContainer *self, FsearchDatabaseEntry *entry) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(db_entry_get_type(entry) == self->entry_type, NULL);

    if (self->num_entries == 0) {
        return 0;
    }
    uint32_t c_idx = 0;
    DynamicArray *c = get_container_for_entry(self, entry, &c_idx);

    uint32_t idx = 0;
    if (darray_binary_search_with_data(c, entry, self->entry_comp_func, NULL, &idx)) {
        FsearchDatabaseEntry *e = darray_get_item(c, idx);
        darray_remove(c, idx, 1);
        self->num_entries--;

        balance_container(self, c, c_idx);
        return e;
    }
    g_autoptr(GString) path = db_entry_get_path_full(entry);
    g_debug("[container_steal] entry not found: %s", path->str);
    return NULL;
}

DynamicArray *
fsearch_database_entries_container_steal_descendants(FsearchDatabaseEntriesContainer *self,
                                                     FsearchDatabaseEntryFolder *folder,
                                                     int32_t num_known_descendants) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(folder, NULL);

    uint32_t container_idx = 0;
    uint32_t entry_start_idx = 0;
    if (self->sort_type == DATABASE_INDEX_PROPERTY_PATH_FULL) {
        if (self->entry_type == DATABASE_ENTRY_TYPE_FOLDER) {
            DynamicArray *container = get_container_for_entry(self, (FsearchDatabaseEntry *)folder, &container_idx);
            darray_binary_search_with_data(container, folder, self->entry_comp_func, NULL, &entry_start_idx);
        }
    }

    DynamicArray *descendants = darray_new(num_known_descendants >= 0 ? num_known_descendants : 128);

    // NOTE: Unfortunately we don't know how many descendants a folder has in total, so we have to add them one by
    // one
    while (container_idx < darray_get_num_items(self->container)) {
        DynamicArray *container = darray_get_item(self->container, container_idx);
        uint32_t entry_idx = entry_start_idx;
        while (entry_idx < darray_get_num_items(container)) {
            FsearchDatabaseEntry *maybe_descendant = darray_get_item(container, entry_idx);
            if (db_entry_is_descendant(maybe_descendant, folder)) {
                darray_add_item(descendants, maybe_descendant);
                darray_remove(container, entry_idx, 1);
                continue;
            }
            entry_idx++;
        }
        entry_start_idx = 0;

        // Remove the container if it became empty
        if (darray_get_num_items(container) == 0) {
            darray_remove(self->container, container_idx, 1);
            g_clear_pointer(&container, darray_unref);
        }
        else {
            container_idx++;
        }
    }

    return descendants;
}

FsearchDatabaseEntry *
fsearch_database_entries_container_get_entry(FsearchDatabaseEntriesContainer *self, uint32_t idx) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(idx < self->num_entries, NULL);

    for (uint32_t i = 0; i < darray_get_num_items(self->container); ++i) {
        DynamicArray *c = darray_get_item(self->container, i);
        const uint32_t num_items = darray_get_num_items(c);
        if (idx < num_items) {
            return num_items > 0 ? darray_get_item(c, idx) : NULL;
        }
        idx -= num_items;
    }
    return NULL;
}

uint32_t
fsearch_database_entries_container_get_num_entries(FsearchDatabaseEntriesContainer *self) {
    g_return_val_if_fail(self, 0);
    return self->num_entries;
}

DynamicArray *
fsearch_database_entries_container_get_containers(FsearchDatabaseEntriesContainer *self) {
    g_return_val_if_fail(self, NULL);

    return darray_ref(self->container);
}

DynamicArray *
fsearch_database_entries_container_get_joined(FsearchDatabaseEntriesContainer *self) {
    g_return_val_if_fail(self, NULL);

    DynamicArray *joined = darray_new(darray_get_num_items(self->container));
    for (uint32_t i = 0; i < darray_get_num_items(self->container); ++i) {
        DynamicArray *c = darray_get_item(self->container, i);
        darray_add_array(joined, c);
    }
    return joined;
}

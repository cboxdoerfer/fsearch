#include "fsearch_filter_manager.h"

#include <stdint.h>
#include <stdlib.h>

struct FsearchFilterManager {
    GPtrArray *filters;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchFilterManager, fsearch_filter_manager, fsearch_filter_manager_ref, fsearch_filter_manager_unref)

static void
filter_manager_free(FsearchFilterManager *self) {
    if (!self) {
        return;
    }
    g_clear_pointer(&self->filters, g_ptr_array_unref);
    g_clear_pointer(&self, free);
}

FsearchFilterManager *
fsearch_filter_manager_ref(FsearchFilterManager *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_filter_manager_unref(FsearchFilterManager *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self, filter_manager_free);
    }
}

FsearchFilterManager *
fsearch_filter_manager_new(void) {
    FsearchFilterManager *self = calloc(1, sizeof(FsearchFilterManager));
    g_assert(self);

    self->filters = g_ptr_array_new_full(12, (GDestroyNotify)fsearch_filter_unref);

    self->ref_count = 1;
    return self;
}

FsearchFilterManager *
fsearch_filter_manager_new_with_defaults(void) {
    FsearchFilterManager *self = calloc(1, sizeof(FsearchFilterManager));
    g_assert(self);

    self->filters = fsearch_filter_get_default_filters();

    self->ref_count = 1;
    return self;
}

FsearchFilterManager *
fsearch_filter_manager_copy(FsearchFilterManager *self) {
    g_return_val_if_fail(self, NULL);

    FsearchFilterManager *copy = fsearch_filter_manager_new();

    for (uint32_t i = 0; i < self->filters->len; ++i) {
        FsearchFilter *filter = g_ptr_array_index(self->filters, i);
        g_ptr_array_add(copy->filters, fsearch_filter_copy(filter));
    }
    return copy;
}

static gboolean
filter_exists(GPtrArray *filters, FsearchFilter *filter, const char *name) {
    for (uint32_t i = 0; i < filters->len; ++i) {
        FsearchFilter *ff = g_ptr_array_index(filters, i);
        if (!ff || ff == filter) {
            continue;
        }
        if (!strcmp(ff->name, name)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void
update_filter_to_unique_name(GPtrArray *filters, FsearchFilter *filter) {

    g_autofree char *new_name = g_strdup(filter->name);

    uint32_t filter_name_copy = 1;
    while (filter_exists(filters, filter, new_name)) {
        g_clear_pointer(&new_name, g_free);
        new_name = g_strdup_printf("%s (%d)", filter->name, filter_name_copy);
        filter_name_copy++;
    }
    g_clear_pointer(&filter->name, g_free);
    filter->name = g_steal_pointer(&new_name);
}

void
fsearch_filter_manager_append_filter(FsearchFilterManager *self, FsearchFilter *filter) {
    g_return_if_fail(self);
    g_return_if_fail(filter);

    update_filter_to_unique_name(self->filters, filter);
    g_ptr_array_add(self->filters, fsearch_filter_ref(filter));
}

void
fsearch_filter_manager_reorder(FsearchFilterManager *self, gint *new_order, size_t new_order_len) {
    g_return_if_fail(self);
    g_return_if_fail(new_order);

    GPtrArray *new_filters = g_ptr_array_new_full(self->filters->len, (GDestroyNotify)fsearch_filter_unref);

    for (uint32_t i = 0; i < new_order_len; ++i) {
        const gint old_pos = new_order[i];
        FsearchFilter *filter = g_ptr_array_index(self->filters, old_pos);
        g_ptr_array_add(new_filters, fsearch_filter_ref(filter));
    }

    g_clear_pointer(&self->filters, g_ptr_array_unref);
    self->filters = new_filters;
}

void
fsearch_filter_manager_remove(FsearchFilterManager *self, FsearchFilter *filter) {
    g_return_if_fail(self);
    g_return_if_fail(filter);

    g_ptr_array_remove(self->filters, filter);
}

void
fsearch_filter_manager_edit(FsearchFilterManager *self,
                            FsearchFilter *filter,
                            const char *name,
                            const char *macro,
                            const char *query,
                            FsearchQueryFlags flags) {
    g_return_if_fail(self);
    g_return_if_fail(name);

    g_clear_pointer(&filter->name, g_free);
    g_clear_pointer(&filter->query, g_free);
    g_clear_pointer(&filter->macro, g_free);
    filter->name = g_strdup(name);
    filter->query = g_strdup(query ? query : "");
    filter->macro = g_strdup(macro ? macro : "");
    filter->flags = flags;
    update_filter_to_unique_name(self->filters, filter);
}

FsearchFilter *
fsearch_filter_manager_get_filter_for_name(FsearchFilterManager *self, const char *name) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(name, NULL);

    for (uint32_t i = 0; i < self->filters->len; ++i) {
        FsearchFilter *filter = g_ptr_array_index(self->filters, i);
        if (filter && !strcmp(filter->name, name)) {
            return fsearch_filter_ref(filter);
        }
    }
    return NULL;
}

guint
fsearch_filter_manager_get_num_filters(FsearchFilterManager *self) {
    g_return_val_if_fail(self, 0);
    return self->filters->len;
}

FsearchFilter *
fsearch_filter_manager_get_filter(FsearchFilterManager *self, guint idx) {
    g_return_val_if_fail(self, NULL);
    if (idx >= self->filters->len) {
        return NULL;
    }
    return fsearch_filter_ref(g_ptr_array_index(self->filters, idx));
}

bool
fsearch_filter_manager_cmp(FsearchFilterManager *manager_1, FsearchFilterManager *manager_2) {
    g_assert(manager_1);
    g_assert(manager_2);

    if (manager_1->filters->len != manager_2->filters->len) {
        return false;
    }

    for (uint32_t i = 0; i < manager_1->filters->len; ++i) {
        FsearchFilter *f1 = g_ptr_array_index(manager_1->filters, i);
        FsearchFilter *f2 = g_ptr_array_index(manager_2->filters, i);
        if (!fsearch_filter_cmp(f1, f2)) {
            return false;
        }
    }
    return true;
}

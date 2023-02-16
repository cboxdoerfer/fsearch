#include "fsearch_filter_manager.h"

#include <stdint.h>
#include <stdlib.h>

struct FsearchFilterManager {
    GList *filters;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchFilterManager, fsearch_filter_manager, fsearch_filter_manager_ref, fsearch_filter_manager_unref)

static void
filter_manager_free(FsearchFilterManager *self) {
    if (!self) {
        return;
    }
    g_list_free_full(g_steal_pointer(&self->filters), (GDestroyNotify)fsearch_filter_unref);
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
    FsearchFilterManager *self= calloc(1, sizeof(FsearchFilterManager));
    g_assert(self);

    self->filters = NULL;

    self->ref_count = 1;
    return self;
}

FsearchFilterManager *
fsearch_filter_manager_new_with_defaults(void) {
    FsearchFilterManager *self= fsearch_filter_manager_new();

    self->filters = fsearch_filter_get_default();
    return self;
}

FsearchFilterManager *
fsearch_filter_manager_copy(FsearchFilterManager *self) {
    g_return_val_if_fail(self, NULL);

    FsearchFilterManager *copy = fsearch_filter_manager_new();

    for (GList *l = self->filters; l != NULL; l = l->next) {
        FsearchFilter *filter = l->data;
        copy->filters = g_list_append(copy->filters, fsearch_filter_copy(filter));
    }
    return copy;
}

static gboolean
filter_exists(GList *filters, FsearchFilter *filter, const char *name) {
    for (GList *f = filters; f != NULL; f = f->next) {
        FsearchFilter *ff = f->data;
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
update_filter_to_unique_name(GList *filters, FsearchFilter *filter) {
    uint32_t filter_name_copy = 1;

    char *filter_name = g_strdup(filter->name);
    char *new_name = g_strdup(filter->name);

    while (filter_exists(filters, filter, new_name)) {
        g_clear_pointer(&new_name, g_free);
        new_name = g_strdup_printf("%s (%d)", filter_name, filter_name_copy);
        filter_name_copy++;
    }
    g_clear_pointer(&filter->name, g_free);
    g_clear_pointer(&filter_name, g_free);
    filter->name = new_name;
}

void
fsearch_filter_manager_append_filter(FsearchFilterManager *self, FsearchFilter *filter) {
    g_return_if_fail(self);
    g_return_if_fail(filter);

    update_filter_to_unique_name(self->filters, filter);
    self->filters = g_list_append(self->filters, fsearch_filter_ref(filter));
}

void
fsearch_filter_manager_reorder(FsearchFilterManager *self, gint *new_order, size_t new_order_len) {
    g_return_if_fail(self);
    g_return_if_fail(new_order);

    GList *reordered_filters = NULL;
    for (uint32_t i = 0; i < new_order_len; ++i) {
        const gint old_pos = new_order[i];
        GList *f = g_list_nth(self->filters, old_pos);
        reordered_filters = g_list_append(reordered_filters, f->data);
    }
    g_list_free(self->filters);
    self->filters = reordered_filters;
}

void
fsearch_filter_manager_remove(FsearchFilterManager *self, FsearchFilter *filter) {
    g_return_if_fail(self);
    g_return_if_fail(filter);

    self->filters = g_list_remove(self->filters, filter);
    g_clear_pointer(&filter, fsearch_filter_unref);
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

    for (GList *l = self->filters; l != NULL; l = l->next) {
        FsearchFilter *filter = l->data;
        g_assert(filter);
        if (!strcmp(filter->name, name)) {
            return fsearch_filter_ref(filter);
        }
    }
    return NULL;
}

guint
fsearch_filter_manager_get_num_filters(FsearchFilterManager *self) {
    g_return_val_if_fail(self, 0);
    return g_list_length(self->filters);
}

FsearchFilter *
fsearch_filter_manager_get_filter(FsearchFilterManager *self, guint idx) {
    g_return_val_if_fail(self, NULL);
    if (idx >= g_list_length(self->filters)) {
        return NULL;
    }
    GList *l = g_list_nth(self->filters, idx);
    FsearchFilter *filter = l->data;
    return filter ? fsearch_filter_ref(filter) : NULL;
}

bool
fsearch_filter_manager_cmp(FsearchFilterManager *manager_1, FsearchFilterManager *manager_2) {
    g_assert(manager_1);
    g_assert(manager_2);

    guint len1 = g_list_length(manager_1->filters);
    guint len2 = g_list_length(manager_2->filters);
    if (len1 != len2) {
        return false;
    }

    GList *l1 = manager_1->filters;
    GList *l2 = manager_2->filters;
    while (l1 && l2) {
        FsearchFilter *f1 = l1->data;
        FsearchFilter *f2 = l2->data;
        if (!fsearch_filter_cmp(f1, f2)) {
            return false;
        }
        l1 = l1->next;
        l2 = l2->next;
    }
    return true;
}

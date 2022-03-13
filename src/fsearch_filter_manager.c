#include "fsearch_filter_manager.h"

#include <stdint.h>
#include <stdlib.h>

struct FsearchFilterManager {
    GList *filters;
};

void
fsearch_filter_manager_free(FsearchFilterManager *manager) {
    if (!manager) {
        return;
    }
    g_list_free_full(g_steal_pointer(&manager->filters), (GDestroyNotify)fsearch_filter_unref);
    g_clear_pointer(&manager, free);
}

FsearchFilterManager *
fsearch_filter_manager_new(void) {
    FsearchFilterManager *manager = calloc(1, sizeof(FsearchFilterManager));
    g_assert_nonnull(manager);

    manager->filters = NULL;
    return manager;
}

FsearchFilterManager *
fsearch_filter_manager_new_with_defaults(void) {
    FsearchFilterManager *manager = fsearch_filter_manager_new();

    manager->filters = fsearch_filter_get_default();
    return manager;
}

FsearchFilterManager *
fsearch_filter_manager_copy(FsearchFilterManager *manager) {
    FsearchFilterManager *copy = fsearch_filter_manager_new();

    for (GList *l = manager->filters; l != NULL; l = l->next) {
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
fsearch_filter_manager_append_filter(FsearchFilterManager *manager, FsearchFilter *filter) {
    update_filter_to_unique_name(manager->filters, filter);
    manager->filters = g_list_append(manager->filters, fsearch_filter_ref(filter));
}

void
fsearch_filter_manager_reorder(FsearchFilterManager *manager, gint *new_order, size_t new_order_len) {
    if (!new_order) {
        return;
    }
    GList *reordered_filters = NULL;
    for (uint32_t i = 0; i < new_order_len; ++i) {
        const gint old_pos = new_order[i];
        GList *f = g_list_nth(manager->filters, old_pos);
        reordered_filters = g_list_append(reordered_filters, f->data);
    }
    g_list_free(manager->filters);
    manager->filters = reordered_filters;
}

void
fsearch_filter_manager_remove(FsearchFilterManager *manager, FsearchFilter *filter) {
    if (!filter) {
        return;
    }
    manager->filters = g_list_remove(manager->filters, filter);
    g_clear_pointer(&filter, fsearch_filter_unref);
}

void
fsearch_filter_manager_edit(FsearchFilterManager *manager,
                            FsearchFilter *filter,
                            const char *name,
                            const char *macro,
                            const char *query,
                            FsearchQueryFlags flags) {
    if (!name) {
        return;
    }
    g_clear_pointer(&filter->name, g_free);
    g_clear_pointer(&filter->query, g_free);
    filter->name = g_strdup(name);
    filter->query = g_strdup(query ? query : "");
    filter->macro = g_strdup(macro ? macro : "");
    filter->flags = flags;
    update_filter_to_unique_name(manager->filters, filter);
}

FsearchFilter *
fsearch_filter_manager_get_filter_for_name(FsearchFilterManager *manager, const char *name) {
    g_assert_nonnull(name);

    for (GList *l = manager->filters; l != NULL; l = l->next) {
        FsearchFilter *filter = l->data;
        g_assert_nonnull(filter);
        if (!strcmp(filter->name, name)) {
            return fsearch_filter_ref(filter);
        }
    }
    return NULL;
}

guint
fsearch_filter_manager_get_num_filters(FsearchFilterManager *manager) {
    return g_list_length(manager->filters);
}

FsearchFilter *
fsearch_filter_manager_get_filter(FsearchFilterManager *manager, guint idx) {
    if (idx >= g_list_length(manager->filters)) {
        return NULL;
    }
    GList *l = g_list_nth(manager->filters, idx);
    FsearchFilter *filter = l->data;
    return filter ? fsearch_filter_ref(filter) : NULL;
}

bool
fsearch_filter_manager_cmp(FsearchFilterManager *manager_1, FsearchFilterManager *manager_2) {
    g_assert_nonnull(manager_1);
    g_assert_nonnull(manager_2);

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

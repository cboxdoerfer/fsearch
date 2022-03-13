#define G_LOG_DOMAIN "fsearch-selection"

#include "fsearch_selection.h"

void
fsearch_selection_free(GHashTable *selection) {
    g_assert(selection);
    g_clear_pointer(&selection, g_hash_table_destroy);
}

GHashTable *
fsearch_selection_new(void) {
    return g_hash_table_new(g_direct_hash, g_direct_equal);
}

void
fsearch_selection_select_toggle(GHashTable *selection, gpointer item) {
    g_assert(selection);
    g_assert(item);

    if (g_hash_table_steal(selection, item)) {
        return;
    }
    g_hash_table_add(selection, item);
}

void
fsearch_selection_select(GHashTable *selection, gpointer item) {
    g_assert(selection);
    g_assert(item);

    g_hash_table_add(selection, item);
}

bool
fsearch_selection_is_selected(GHashTable *selection, gpointer item) {
    g_assert(selection);
    g_assert(item);

    return g_hash_table_contains(selection, item);
}

void
fsearch_selection_select_all(GHashTable *selection, DynamicArray *items) {
    g_assert(selection);
    g_assert(items);

    const uint32_t num_items = darray_get_num_items(items);

    for (uint32_t i = 0; i < num_items; i++) {
        void *item = darray_get_item(items, i);
        if (!item) {
            g_debug("[select_all] item is NULL");
        }
        g_hash_table_add(selection, item);
    }
}

void
fsearch_selection_unselect_all(GHashTable *selection) {
    g_assert(selection);
    g_hash_table_remove_all(selection);
}

void
fsearch_selection_invert(GHashTable *selection, DynamicArray *items) {
    g_assert(selection);
    g_assert(items);

    const uint32_t num_items = darray_get_num_items(items);

    for (uint32_t i = 0; i < num_items; i++) {
        void *item = darray_get_item(items, i);
        if (!item) {
            g_debug("[select_all] item is NULL");
        }
        if (g_hash_table_steal(selection, item)) {
            continue;
        }
        g_hash_table_add(selection, item);
    }
}

uint32_t
fsearch_selection_get_num_selected(GHashTable *selection) {
    g_assert(selection);
    return g_hash_table_size(selection);
}
void
fsearch_selection_for_each(GHashTable *selection, GHFunc func, gpointer user_data) {
    g_assert(selection);
    g_hash_table_foreach(selection, func, user_data);
}

/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define G_LOG_DOMAIN "fsearch-dynamic-array"

#include "fsearch_array.h"
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#define MAX_SORT_THREADS 8

struct DynamicArray {
    // number of items in array
    uint32_t num_items;
    // total size of array
    uint32_t max_items;
    // data
    void **data;

    volatile int ref_count;
};

static void
darray_free(DynamicArray *array) {
    if (array == NULL) {
        return;
    }

    g_debug("[darray_free] freed");

    g_clear_pointer(&array->data, free);
    g_clear_pointer(&array, free);
}

DynamicArray *
darray_ref(DynamicArray *array) {
    if (!array || g_atomic_int_get(&array->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&array->ref_count);
    // g_debug("[darray_ref] increased to: %d", array->ref_count);
    return array;
}

void
darray_unref(DynamicArray *array) {
    if (!array || g_atomic_int_get(&array->ref_count) <= 0) {
        return;
    }
    // g_debug("[darray_unref] dropped to: %d", array->ref_count - 1);
    if (g_atomic_int_dec_and_test(&array->ref_count)) {
        g_clear_pointer(&array, darray_free);
    }
}

typedef struct {
    DynamicArray *m1;
    DynamicArray *m2;
    DynamicArray *dest;
    gpointer user_data;
    DynamicArrayCompareDataFunc comp_func;

} DynamicArraySortContext;

static void
insertion_sort(DynamicArray *array, DynamicArrayCompareDataFunc comp_func, void *data) {
    for (uint32_t i = 0; i < array->num_items; ++i) {
        void *val_a = array->data[i];
        uint32_t j = i;
        while (j > 0 && comp_func(&array->data[j - 1], &val_a, data) > 0) {
            array->data[j] = array->data[j - 1];
            j--;
        }
        array->data[j] = val_a;
    }
}

static void
merge(DynamicArray *src,
      DynamicArray *dest,
      uint32_t start_idx,
      uint32_t center_idx,
      uint32_t end_idx,
      GCancellable *cancellable,
      DynamicArrayCompareDataFunc comp_func,
      gpointer comp_data) {
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    uint32_t i = start_idx;
    uint32_t j = center_idx;

    for (uint32_t k = start_idx; k < end_idx; k++) {
        if (i < center_idx && (j >= end_idx || comp_func(&src->data[i], &src->data[j], comp_data) < 1)) {
            dest->data[k] = src->data[i];
            i = i + 1;
        }
        else {
            dest->data[k] = src->data[j];
            j = j + 1;
        }
    }
}

static void
split_merge(DynamicArray *src,
            DynamicArray *dest,
            uint32_t start_idx,
            uint32_t end_idx,
            GCancellable *cancellable,
            DynamicArrayCompareDataFunc comp_func,
            gpointer comp_data) {
    if (end_idx - 1 <= start_idx) {
        return;
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    const uint32_t center_idx = (end_idx + start_idx) / 2;
    split_merge(dest, src, start_idx, center_idx, cancellable, comp_func, comp_data);
    split_merge(dest, src, center_idx, end_idx, cancellable, comp_func, comp_data);
    merge(src, dest, start_idx, center_idx, end_idx, cancellable, comp_func, comp_data);
}

static void
merge_sort(DynamicArray *to_sort,
           GCancellable *cancellable,
           DynamicArrayCompareDataFunc comp_func,
           gpointer comp_data) {
    g_assert(to_sort);
    g_assert(comp_func);
    g_autoptr(DynamicArray) tmp = darray_copy(to_sort);
    split_merge(tmp, to_sort, 0, to_sort->num_items, cancellable, comp_func, comp_data);
}

static void
sort_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    merge_sort(ctx->dest, user_data, (DynamicArrayCompareDataFunc)ctx->comp_func, ctx->user_data);
}

static inline void
add_remaining_items(DynamicArray *dest, DynamicArray *source, uint32_t start_idx) {
    const uint32_t remaining_items = source->num_items - start_idx;
    if (remaining_items > 0) {
        darray_add_items(dest, &source->data[start_idx], remaining_items);
    }
}

static void
merge_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    uint32_t left_idx = 0;
    uint32_t right_idx = 0;
    const uint32_t left_size = ctx->m1->num_items;
    const uint32_t right_size = ctx->m2->num_items;

    // Merge arrays while both have elements
    while (left_idx < left_size && right_idx < right_size) {
        void *left_item = darray_get_item(ctx->m1, left_idx);
        void *right_item = darray_get_item(ctx->m2, right_idx);

        const int comparison = ctx->comp_func(&left_item, &right_item, ctx->user_data);

        if (comparison <= 0) {
            darray_add_item(ctx->dest, left_item);
            left_idx++;
        }
        else {
            darray_add_item(ctx->dest, right_item);
            right_idx++;
        }
    }

    // Add remaining elements from either array
    add_remaining_items(ctx->dest, ctx->m1, left_idx);
    add_remaining_items(ctx->dest, ctx->m2, right_idx);
}

DynamicArray *
darray_new(size_t num_items) {
    DynamicArray *new = calloc(1, sizeof(DynamicArray));
    g_assert(new);

    new->max_items = num_items;
    new->num_items = 0;

    new->data = calloc(num_items, sizeof(void *));
    g_assert(new->data);

    new->ref_count = 1;

    return new;
}

static void
darray_expand(DynamicArray *array, size_t min) {
    g_assert(array);
    g_assert(array->data);

    const size_t old_max_items = array->max_items;
    const size_t expand_rate = MAX(array->max_items / 2, min - old_max_items);
    array->max_items += expand_rate;

    void *new_data = realloc(array->data, array->max_items * sizeof(void *));
    g_assert(new_data);
    array->data = new_data;
    memset(array->data + old_max_items, 0, expand_rate * sizeof(void*));
}

void
darray_add_items(DynamicArray *array, void **items, uint32_t num_items) {
    g_assert(array);
    g_assert(array->data);
    g_assert(items);

    if (array->num_items + num_items > array->max_items) {
        darray_expand(array, array->num_items + num_items);
    }

    memcpy(array->data + array->num_items, items, num_items * sizeof(void *));
    array->num_items += num_items;
}

void
darray_add_array(DynamicArray *dest, DynamicArray *source) {
    g_assert(dest);
    g_assert(dest->data);
    g_assert(source);
    g_assert(source->data);

    darray_add_items(dest, source->data, source->num_items);
}

void
darray_add_item(DynamicArray *array, void *data) {
    g_assert(array);
    g_assert(array->data);
    // g_assert(data );

    if (array->num_items >= array->max_items) {
        darray_expand(array, array->num_items + 1);
    }

    array->data[array->num_items++] = data;
}

void
darray_insert_item(DynamicArray *array, void *data, uint32_t index) {
    g_assert(array);
    g_assert(array->data);
    if (index > array->num_items) {
        index = array->num_items;
    }

    if (array->num_items >= array->max_items) {
        darray_expand(array, array->num_items + 1);
    }

    memmove(array->data + index + 1, array->data + index, (array->num_items - index) * sizeof(void *));
    array->data[index] = data;
    array->num_items++;
}

uint32_t
darray_insert_item_sorted(DynamicArray *array, void *item, DynamicArrayCompareDataFunc compare_func, void *data) {
    g_assert(array);
    g_assert(array->data);

    uint32_t insert_at = 0;
    darray_binary_search_with_data(array, item, compare_func, data, &insert_at);

    darray_insert_item(array, item, insert_at);
    return insert_at;
}

DynamicArray *
darray_steal_items(DynamicArray *array, DynamicArrayStealFunc func, void *data) {
    g_assert(array);
    g_assert(array->data);

    g_autoptr(DynamicArray) stolen_entries = darray_new(16);

    uint32_t i = 0;
    while (i < array->num_items) {
        void *item = array->data[i];
        if (item && func(item, data)) {
            darray_add_item(stolen_entries, item);
            darray_remove(array, i, 1);
            continue;
        }
        i++;
    }

    return g_steal_pointer(&stolen_entries);
}

static uint32_t
darray_steal_or_remove(DynamicArray *array, uint32_t index, uint32_t n_elements, DynamicArray *dest) {
    g_assert(array);
    g_assert(array->data);

    if (n_elements == 0) {
        // No need to remove anything
        return 0;
    }

    if (index >= array->num_items) {
        return 0;
    }
    if (index + n_elements >= array->num_items) {
        // The end of the items to be removed is also the end of the array.
        // No need to memmove, just to decrement the number of array items.
        n_elements = array->num_items - index;
        if (dest) {
            darray_add_items(dest, array->data + index, n_elements);
        }
        array->num_items -= n_elements;
        return n_elements;
    }

    if (dest) {
        darray_add_items(dest, array->data + index, n_elements);
    }
    memmove(array->data + index, array->data + index + n_elements, (array->num_items - index - 1) * sizeof(void *));
    array->num_items -= n_elements;

    return n_elements;
}

uint32_t
darray_remove(DynamicArray *array, uint32_t index, uint32_t n_elements) {
    g_assert(array);
    g_assert(array->data);

    return darray_steal_or_remove(array, index, n_elements, NULL);
}

uint32_t
darray_steal(DynamicArray *array, uint32_t index, uint32_t n_elements, DynamicArray *destination) {
    g_assert(destination);

    return darray_steal_or_remove(array, index, n_elements, destination);
}

void
darray_remove_items_sorted(DynamicArray *array, DynamicArray *items, DynamicArrayCompareDataFunc compare_func, void *data) {
    g_assert(array);
    g_assert(items);
    g_assert(compare_func);

    for (uint32_t i = 0; i < items->num_items; ++i) {
        void *item = darray_get_item(items, i);
        int32_t idx = 0;
        if (darray_binary_search_with_data(array, item, compare_func, data, (uint32_t *)&idx)) {
            darray_remove(array, idx, 1);
        }
    }
}

bool
darray_get_item_idx(DynamicArray *array, void *item, DynamicArrayCompareDataFunc compare_func, void *data, uint32_t *index) {
    g_assert(array);
    g_assert(index);

    if (compare_func) {
        return darray_binary_search_with_data(array, item, compare_func, data, index);
    }

    bool found = false;
    for (uint32_t i = 0; i < array->num_items; i++) {
        if (item == array->data[i]) {
            found = true;
            *index = i;
            break;
        }
    }
    return found;
}

void *
darray_get_item_next(DynamicArray *array,
                     void *item,
                     DynamicArrayCompareDataFunc compare_func,
                     void *data,
                     uint32_t *next_idx) {
    g_assert(array);
    uint32_t index = 0;
    if (!darray_get_item_idx(array, item, compare_func, data, &index)) {
        return NULL;
    }
    if (index >= array->num_items - 1) {
        return NULL;
    }
    if (next_idx) {
        *next_idx = index + 1;
    }
    return array->data[index + 1];
}

void *
darray_get_item(DynamicArray *array, uint32_t idx) {
    g_assert(array);
    g_assert(array->data);

    if (idx >= array->num_items) {
        return NULL;
    }

    return array->data[idx];
}

DynamicArray *
darray_get_range(DynamicArray *array, uint32_t start_idx, uint32_t num_items) {
    g_assert(array);
    g_assert(array->data);
    g_assert(start_idx < array->num_items);

    num_items = MIN(array->num_items - start_idx, num_items);

    DynamicArray *range = darray_new(num_items);
    memcpy(range->data, array->data + start_idx, num_items * sizeof(void *));
    range->num_items = num_items;

    return range;
}

uint32_t
darray_get_num_items(DynamicArray *array) {
    g_assert(array);
    g_assert(array->data);

    return array->num_items;
}

uint32_t
darray_get_size(DynamicArray *array) {
    g_assert(array);
    g_assert(array->data);

    return array->max_items;
}

static DynamicArray *
new_array_from_data(void **data, uint32_t num_items) {
    DynamicArray *array = darray_new(num_items);
    darray_add_items(array, data, num_items);
    return array;
}

static GArray *
merge_sorted(GArray *merge_me, DynamicArrayCompareDataFunc comp_func, GCancellable *cancellable) {
    if (merge_me->len == 1) {
        return merge_me;
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        return merge_me;
    }
    const uint32_t num_threads = merge_me->len / 2;

    g_debug("[sort] merge with %d thread(s)", num_threads);

    GArray *merged_data = g_array_sized_new(TRUE, TRUE, sizeof(DynamicArraySortContext), num_threads);
    GThreadPool *merge_pool = g_thread_pool_new(merge_thread, NULL, (gint)num_threads, FALSE, NULL);

    for (int i = 0; i < num_threads; ++i) {
        DynamicArraySortContext *c1 = &g_array_index(merge_me, DynamicArraySortContext, 2 * i);
        DynamicArraySortContext *c2 = &g_array_index(merge_me, DynamicArraySortContext, 2 * i + 1);
        DynamicArray *i1 = c1->dest;
        DynamicArray *i2 = c2->dest;

        DynamicArraySortContext merge_ctx = {};
        merge_ctx.m1 = i1;
        merge_ctx.m2 = i2;
        merge_ctx.comp_func = comp_func;
        merge_ctx.dest = darray_new(i1->num_items + i2->num_items);

        g_array_insert_val(merged_data, i, merge_ctx);

        g_thread_pool_push(merge_pool, &g_array_index(merged_data, DynamicArraySortContext, i), NULL);
    }

    g_thread_pool_free(g_steal_pointer(&merge_pool), FALSE, TRUE);

    for (int i = 0; i < merge_me->len; i++) {
        DynamicArraySortContext *c = &g_array_index(merge_me, DynamicArraySortContext, i);
        if (c && c->dest) {
            g_clear_pointer(&c->dest, darray_free);
        }
    }

    return merge_sorted(merged_data, comp_func, cancellable);
}

static int
get_ideal_thread_count() {
    // int num_processors = 1;
    const int num_processors = (int)g_get_num_processors();

    const int e = floor(log2(num_processors));
    const int num_threads = (int)pow(2, e);
    return MAX(num_threads, MAX_SORT_THREADS);
}

void
darray_sort_multi_threaded(DynamicArray *array,
                           DynamicArrayCompareDataFunc comp_func,
                           GCancellable *cancellable,
                           void *data) {
    const int num_threads = get_ideal_thread_count();
    if (num_threads < 2 || num_threads > array->num_items) {
        return darray_sort(array, comp_func, NULL, data);
    }

    g_debug("[sort] sorting with %d threads", num_threads);

    const int num_items_per_thread = (int)(array->num_items / num_threads);
    GThreadPool *sort_pool = g_thread_pool_new(sort_thread, cancellable, num_threads, FALSE, NULL);

    g_autoptr(GArray) sort_ctx_array = g_array_sized_new(TRUE, TRUE, sizeof(DynamicArraySortContext), num_threads);

    int start = 0;
    for (int i = 0; i < num_threads; ++i) {
        DynamicArraySortContext sort_ctx;
        sort_ctx.dest = new_array_from_data(array->data + start,
                                            i == num_threads - 1 ? array->num_items - start : num_items_per_thread);
        sort_ctx.comp_func = comp_func;
        sort_ctx.user_data = data;
        start += num_items_per_thread;
        g_array_insert_val(sort_ctx_array, i, sort_ctx);
        g_thread_pool_push(sort_pool, &g_array_index(sort_ctx_array, DynamicArraySortContext, i), NULL);
    }
    g_thread_pool_free(g_steal_pointer(&sort_pool), FALSE, TRUE);

    g_autoptr(GArray) result = merge_sorted(sort_ctx_array, comp_func, cancellable);

    if (result) {
        g_clear_pointer(&array->data, free);

        DynamicArraySortContext *c = &g_array_index(result, DynamicArraySortContext, 0);
        array->data = g_steal_pointer(&c->dest->data);
        array->num_items = c->dest->num_items;
        array->max_items = c->dest->max_items;

        g_clear_pointer(&c->dest, free);
    }
}

void
darray_sort(DynamicArray *array, DynamicArrayCompareDataFunc comp_func, GCancellable *cancellable, void *data) {
    g_assert(array);
    g_assert(array->data);
    g_assert(comp_func);

    if (array->num_items < 64) {
        g_debug("[sort] insertion sort: %d\n", array->num_items);
        insertion_sort(array, comp_func, data);
    }
    else {
        g_debug("[sort] merge sort: %d\n", array->num_items);
        merge_sort(array, cancellable, comp_func, data);
    }
}

bool
darray_binary_search_with_data(DynamicArray *array,
                               void *item,
                               DynamicArrayCompareDataFunc comp_func,
                               void *data,
                               uint32_t *matched_index) {
    g_assert(array);
    g_assert(array->data);
    g_assert(comp_func);

    if (array->num_items <= 0) {
        if (matched_index != NULL) {
            *matched_index = 0;
        }
        return false;
    }

    int32_t left = 0;
    int32_t middle = 0;
    int32_t right = (int32_t)array->num_items - 1;

    while (left <= right) {
        middle = left + (right - left) / 2;

        const int32_t match = comp_func(&array->data[middle], &item, data);
        if (match == 0) {
            // We've found an exact match
            if (matched_index) {
                *matched_index = middle;
            }
            return true;
        }
        if (match < 0) {
            // item is to the right of middle
            left = middle + 1;
        }
        else {
            // item is to the left of middle
            right = middle - 1;
        }
    }

    // No match found
    // set matched_index to left, i.e. the first index which is greater than our item
    if (matched_index != NULL) {
        *matched_index = left;
    }

    return false;
}

void
darray_for_each(DynamicArray *array, DynamicArrayForEachFunc func, void *data) {
    g_return_if_fail(array);
    g_return_if_fail(func);

    for (uint32_t i = 0; i < array->num_items; ++i) {
        if (!func(array->data[i], data)) {
            break;
        }
    }
}

DynamicArray *
darray_copy(DynamicArray *array) {
    if (!array) {
        return NULL;
    }
    DynamicArray *new = calloc(1, sizeof(DynamicArray));
    g_assert(new);

    new->max_items = array->max_items;
    new->num_items = array->num_items;

    new->data = calloc(new->max_items, sizeof(void *));
    g_assert(new->data);

    new->ref_count = 1;

    memcpy(new->data, array->data, new->max_items * sizeof(void *));

    return new;
}

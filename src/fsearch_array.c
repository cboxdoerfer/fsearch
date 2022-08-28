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
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
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
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return;
    }

    uint32_t center_idx = (end_idx + start_idx) / 2;
    split_merge(dest, src, start_idx, center_idx, cancellable, comp_func, comp_data);
    split_merge(dest, src, center_idx, end_idx, cancellable, comp_func, comp_data);
    merge(src, dest, start_idx, center_idx, end_idx, cancellable, comp_func, comp_data);
}

static void
merge_sort(DynamicArray *to_sort,
           DynamicArray *tmp,
           GCancellable *cancellable,
           DynamicArrayCompareDataFunc comp_func,
           gpointer comp_data) {
    split_merge(tmp, to_sort, 0, to_sort->num_items, cancellable, comp_func, comp_data);
}

static void
sort_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    DynamicArray *tmp = darray_copy(ctx->dest);
    merge_sort(ctx->dest, tmp, user_data, (DynamicArrayCompareDataFunc)ctx->comp_func, ctx->user_data);
    g_clear_pointer(&tmp, darray_unref);
}

static void
merge_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    int i = 0;
    int j = 0;
    while (true) {
        void *d1 = darray_get_item(ctx->m1, i);
        void *d2 = darray_get_item(ctx->m2, j);

        if (d1 && d2) {
            int res = ctx->comp_func(&d1, &d2, user_data);
            if (res < 0) {
                darray_add_item(ctx->dest, d1);
                i++;
            }
            else if (res > 0) {
                darray_add_item(ctx->dest, d2);
                j++;
            }
            else {
                darray_add_item(ctx->dest, d1);
                darray_add_item(ctx->dest, d2);
                i++;
                j++;
            }
        }
        else {
            if (d1) {
                darray_add_items(ctx->dest, &ctx->m1->data[i], ctx->m1->num_items - i);
                return;
            }
            else if (d2) {
                darray_add_items(ctx->dest, &ctx->m2->data[j], ctx->m2->num_items - j);
                return;
            }
            else {
                return;
            }
        }
    }
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
    memset(array->data + old_max_items, 0, expand_rate + 1);
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
darray_add_item(DynamicArray *array, void *data) {
    g_assert(array);
    g_assert(array->data);
    // g_assert(data );

    if (array->num_items >= array->max_items) {
        darray_expand(array, array->num_items + 1);
    }

    array->data[array->num_items++] = data;
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
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
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
    if (array->num_items <= 100000 || num_threads < 2) {
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
        DynamicArray *src = darray_copy(array);
        merge_sort(array, src, cancellable, comp_func, data);
        g_clear_pointer(&src, darray_unref);
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
        return false;
    }

    bool result = false;

    uint32_t left = 0;
    uint32_t middle = 0;
    uint32_t right = array->num_items - 1;

    while (left <= right) {
        middle = left + (right - left) / 2;

        int32_t match = comp_func(&array->data[middle], &item, data);
        if (match == 0) {
            result = true;
            break;
        }
        else if (match < 0) {
            left = middle + 1;
        }
        else if (middle > 0) {
            // match > 0
            // This means the item is to the left of middle
            // If middle == 0 then the item is not in the array
            right = middle - 1;
        }
        else {
            break;
        }
    }

    if (result && matched_index != NULL)
        *matched_index = middle;

    return result;
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

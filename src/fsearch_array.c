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
#include <assert.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#define MAX_SORT_THREADS 8

struct _DynamicArray {
    // number of items in array
    uint32_t num_items;
    // total size of array
    uint32_t max_items;
    // data
    void **data;
};

void
darray_clear(DynamicArray *array) {
    assert(array != NULL);
    if (array->num_items > 0) {
        for (uint32_t i = 0; i < array->max_items; i++) {
            array->data[i] = NULL;
        }
    }
}

void
darray_free(DynamicArray *array) {
    if (array == NULL) {
        return;
    }

    darray_clear(array);
    if (array->data) {
        free(array->data);
        array->data = NULL;
    }
    free(array);
    array = NULL;
}

typedef struct {
    DynamicArray *m1;
    DynamicArray *m2;
    DynamicArray *dest;
    gpointer user_data;
    DynamicArrayCompareFunc comp_func;

} DynamicArraySortContext;

void
sort_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    g_qsort_with_data(ctx->dest->data, ctx->dest->num_items, sizeof(void *), (GCompareDataFunc)ctx->comp_func, NULL);
    // qsort(ctx->dest->data, ctx->dest->num_items, sizeof(void *), (GCompareFunc)ctx->comp_func);
}

void
merge_thread(gpointer data, gpointer user_data) {
    DynamicArraySortContext *ctx = data;
    int i = 0;
    int j = 0;
    while (true) {
        void *d1 = darray_get_item(ctx->m1, i);
        void *d2 = darray_get_item(ctx->m2, j);

        if (d1 && d2) {
            int res = ctx->comp_func(&d1, &d2);
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
                darray_add_item(ctx->dest, d1);
                i++;
            }
            else if (d2) {
                darray_add_item(ctx->dest, d2);
                j++;
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
    assert(new != NULL);

    new->max_items = num_items;
    new->num_items = 0;

    new->data = calloc(num_items, sizeof(void *));
    assert(new->data != NULL);

    return new;
}

static void
darray_expand(DynamicArray *array, size_t min) {
    assert(array != NULL);
    assert(array->data != NULL);

    size_t old_max_items = array->max_items;
    size_t expand_rate = MAX(array->max_items / 2, min - old_max_items);
    array->max_items += expand_rate;

    void *new_data = realloc(array->data, array->max_items * sizeof(void *));
    assert(new_data != NULL);
    array->data = new_data;
    memset(array->data + old_max_items, 0, expand_rate + 1);
}

void **
darray_get_data(DynamicArray *array, size_t *num_items) {
    assert(array != NULL);
    assert(array->data != NULL);
    if (num_items) {
        *num_items = array->num_items;
    }
    return array->data;
}

void
darray_add_items(DynamicArray *array, void **items, uint32_t num_items) {
    assert(array != NULL);
    assert(array->data != NULL);
    assert(items != NULL);

    if (array->num_items + num_items > array->max_items) {
        darray_expand(array, array->num_items + num_items);
    }

    memcpy(array->data + array->num_items, items, num_items * sizeof(void *));
    array->num_items += num_items;
}

void
darray_add_item(DynamicArray *array, void *data) {
    assert(array != NULL);
    assert(array->data != NULL);
    assert(data != NULL);

    if (array->num_items >= array->max_items) {
        darray_expand(array, array->num_items + 1);
    }

    array->data[array->num_items++] = data;
}

void
darray_set_item(DynamicArray *array, void *data, uint32_t idx) {
    assert(array != NULL);
    assert(array->data != NULL);

    if (idx >= array->max_items) {
        darray_expand(array, idx + 1);
    }

    array->data[idx] = data;
    if (data != NULL) {
        array->num_items++;
    }
}

void
darray_remove_item(DynamicArray *array, uint32_t idx) {
    assert(array != NULL);
    assert(array->data != NULL);

    if (idx >= array->max_items) {
        return;
    }

    array->data[idx] = NULL;
    array->num_items--;
}

bool
darray_get_item_idx(DynamicArray *array,
                    void *item,
                    DynamicArrayCompareDataFunc compare_func,
                    void *data,
                    uint32_t *index) {
    assert(array != NULL);
    assert(item != NULL);
    assert(index != NULL);

    bool found = false;
    if (compare_func) {
        found = darray_binary_search_with_data(array, item, compare_func, data, index);
    }
    else {
        for (uint32_t i = 0; i < array->num_items; i++) {
            if (item == array->data[i]) {
                found = true;
                *index = i;
                break;
            }
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
    assert(array != NULL);
    assert(item != NULL);
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
    assert(array != NULL);
    assert(array->data != NULL);

    if (idx >= array->num_items) {
        return NULL;
    }

    return array->data[idx];
}

uint32_t
darray_get_num_items(DynamicArray *array) {
    assert(array != NULL);
    assert(array->data != NULL);

    return array->num_items;
}

uint32_t
darray_get_size(DynamicArray *array) {
    assert(array != NULL);
    assert(array->data != NULL);

    return array->max_items;
}

static DynamicArray *
darray_new_from_data(void **data, uint32_t num_items) {
    DynamicArray *array = darray_new(num_items);
    darray_add_items(array, data, num_items);
    return array;
}

static GArray *
darray_merge_sorted(GArray *merge_me, DynamicArrayCompareFunc comp_func) {

    if (merge_me->len == 1) {
        return merge_me;
    }
    int num_threads = merge_me->len / 2;

    g_debug("[sort] merge with %d thread(s)", num_threads);

    GArray *merged_data = g_array_sized_new(TRUE, TRUE, sizeof(DynamicArraySortContext), num_threads);
    GThreadPool *merge_pool = g_thread_pool_new(merge_thread, NULL, num_threads, FALSE, NULL);

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

    g_thread_pool_free(merge_pool, FALSE, TRUE);

    for (int i = 0; i < merge_me->len; i++) {
        DynamicArraySortContext *c = &g_array_index(merge_me, DynamicArraySortContext, i);
        if (c && c->dest) {
            darray_free(c->dest);
        }
    }
    g_array_free(merge_me, TRUE);
    merge_me = NULL;

    return darray_merge_sorted(merged_data, comp_func);
}

static int
darray_get_ideal_thread_count() {
    // int num_processors = 1;
    int num_processors = g_get_num_processors();

    int e = floor(log2(num_processors));
    int num_threads = pow(2, e);
    return MAX(num_threads, MAX_SORT_THREADS);
}

void
darray_sort_multi_threaded(DynamicArray *array, DynamicArrayCompareFunc comp_func) {

    const int num_threads = darray_get_ideal_thread_count();
    if (array->num_items <= 100000 || num_threads < 2) {
        return darray_sort(array, comp_func);
    }

    g_debug("[sort] sorting with %d threads", num_threads);

    int num_items_per_thread = array->num_items / num_threads;
    GThreadPool *sort_pool = g_thread_pool_new(sort_thread, NULL, num_threads, FALSE, NULL);

    GArray *sort_ctx_array = g_array_sized_new(TRUE, TRUE, sizeof(DynamicArraySortContext), num_threads);

    int start = 0;
    for (int i = 0; i < num_threads; ++i) {
        DynamicArraySortContext sort_ctx;
        sort_ctx.dest = darray_new_from_data(array->data + start,
                                             i == num_threads - 1 ? array->num_items - start : num_items_per_thread);
        sort_ctx.comp_func = comp_func;
        start += num_items_per_thread;
        g_array_insert_val(sort_ctx_array, i, sort_ctx);
        g_thread_pool_push(sort_pool, &g_array_index(sort_ctx_array, DynamicArraySortContext, i), NULL);
    }
    g_thread_pool_free(sort_pool, FALSE, TRUE);

    GArray *result = darray_merge_sorted(sort_ctx_array, comp_func);

    if (result) {
        free(array->data);
        array->data = NULL;

        DynamicArraySortContext *c = &g_array_index(result, DynamicArraySortContext, 0);
        array->data = c->dest->data;
        array->num_items = c->dest->num_items;
        array->max_items = c->dest->max_items;

        c->dest->data = NULL;
        free(c->dest);

        g_array_free(result, TRUE);
        result = NULL;
    }
}

void
darray_sort(DynamicArray *array, DynamicArrayCompareFunc comp_func) {
    assert(array != NULL);
    assert(array->data != NULL);
    assert(comp_func != NULL);

    g_qsort_with_data(array->data, array->num_items, sizeof(void *), (GCompareDataFunc)comp_func, NULL);
}

bool
darray_binary_search_with_data(DynamicArray *array,
                               void *item,
                               DynamicArrayCompareDataFunc comp_func,
                               void *data,
                               uint32_t *matched_index) {

    assert(array != NULL);
    assert(array->data != NULL);
    assert(comp_func != NULL);

    if (array->num_items <= 0) {
        return false;
    }

    bool result = false;

    uint32_t left = 0;
    uint32_t middle = 0;
    uint32_t right = array->num_items - 1;

    while (left <= right) {
        middle = left + (right - left) / 2;

        int32_t match = comp_func(array->data[middle], item, data);
        if (match == 0) {
            result = true;
            break;
        }
        else if (match < 0) {
            left = middle + 1;
        }
        else if (middle > 0) {
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


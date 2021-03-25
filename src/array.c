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

#include "array.h"
#include <assert.h>
#include <glib.h>
#include <string.h>
#include <sys/param.h>

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

void
darray_sort_with_data(DynamicArray *array, DynamicArrayCompareDataFunc comp_func, void *data) {
    g_qsort_with_data(array->data, array->num_items, sizeof(void *), (GCompareDataFunc)comp_func, data);
}

void
darray_sort(DynamicArray *array, int (*comp_func)(const void *, const void *)) {
    assert(array != NULL);
    assert(array->data != NULL);
    assert(comp_func != NULL);

    qsort(array->data, array->num_items, sizeof(void *), comp_func);
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


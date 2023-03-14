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

#pragma once

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct DynamicArray DynamicArray;

typedef int32_t (*DynamicArrayCompareFunc)(void *a, void *b);
typedef int32_t (*DynamicArrayCompareDataFunc)(void *a, void *b, void *data);
typedef bool (*DynamicArrayForEachFunc)(void *, void *data);

void
darray_for_each(DynamicArray *array, DynamicArrayForEachFunc func, void *data);

bool
darray_binary_search_with_data(DynamicArray *array,
                               void *item,
                               DynamicArrayCompareDataFunc comp_func,
                               void *data,
                               uint32_t *matched_index);

void
darray_sort_multi_threaded(DynamicArray *array,
                           DynamicArrayCompareDataFunc comp_func,
                           GCancellable *cancellable,
                           void *data);

void
darray_sort(DynamicArray *array, DynamicArrayCompareDataFunc comp_func, GCancellable *cancellable, void *data);

uint32_t
darray_get_size(DynamicArray *array);

uint32_t
darray_get_num_items(DynamicArray *array);

void *
darray_get_item(DynamicArray *array, uint32_t idx);

void *
darray_get_item_next(DynamicArray *array,
                     void *item,
                     DynamicArrayCompareDataFunc compare_func,
                     void *data,
                     uint32_t *next_idx);

bool
darray_get_item_idx(DynamicArray *array, void *item, DynamicArrayCompareDataFunc compare_func, void *data, uint32_t *index);

void
darray_add_items(DynamicArray *array, void **items, uint32_t num_items);

void
darray_add_array(DynamicArray *dest, DynamicArray *source);

void
darray_add_item(DynamicArray *array, void *data);

void
darray_insert_item_sorted(DynamicArray *array, void *item, DynamicArrayCompareDataFunc compare_func, void *data);

void
darray_insert_item(DynamicArray *array, void *data, uint32_t index);

void
darray_remove(DynamicArray *array, uint32_t index, uint32_t n_elements);

DynamicArray *
darray_new(size_t num_items);

void
darray_unref(DynamicArray *array);

DynamicArray *
darray_ref(DynamicArray *array);

DynamicArray *
darray_copy(DynamicArray *array);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DynamicArray, darray_unref)

#pragma once

#include "fsearch_array.h"
#include "fsearch_database_index.h"

void
fsearch_database_sort_results(FsearchDatabaseIndexProperty old_sort_order,
                              FsearchDatabaseIndexProperty new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray *files_fast_sort_index,
                              DynamicArray *folders_fast_sort_index,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseIndexProperty *sort_order_out,
                              GCancellable *cancellable);

bool
fsearch_database_sort(DynamicArray **files_store,
                      DynamicArray **folders_store,
                      FsearchDatabaseIndexPropertyFlags flags,
                      GCancellable *cancellable);

DynamicArrayCompareDataFunc
fsearch_database_sort_get_compare_func_for_property(FsearchDatabaseIndexProperty property);

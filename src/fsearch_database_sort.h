#pragma once

#include "fsearch_array.h"
#include "fsearch_database_index.h"

void
fsearch_database_sort_results(FsearchDatabaseIndexType old_sort_order,
                              FsearchDatabaseIndexType new_sort_order,
                              DynamicArray *files_in,
                              DynamicArray *folders_in,
                              DynamicArray **sorted_files_in,
                              DynamicArray **sorted_folders_in,
                              DynamicArray **files_out,
                              DynamicArray **folders_out,
                              FsearchDatabaseIndexType *sort_order_out,
                              GCancellable *cancellable);

void
fsearch_database_sort(FsearchDatabaseIndexType sort_order,
                      DynamicArray *files_in,
                      DynamicArray *folders_in,
                      FsearchDatabaseIndexType *sort_order_out,
                      DynamicArray **files_out,
                      DynamicArray **folders_out,
                      GCancellable *cancellable);

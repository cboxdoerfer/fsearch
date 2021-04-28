#pragma once

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct _FsearchMemoryPool FsearchMemoryPool;

FsearchMemoryPool *
fsearch_memory_pool_new(uint32_t block_size, size_t item_size, GDestroyNotify item_free_func);

void
fsearch_memory_pool_free(FsearchMemoryPool *pool);

void *
fsearch_memory_pool_malloc(FsearchMemoryPool *pool);


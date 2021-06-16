#include "fsearch_memory_pool.h"

#include <assert.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint32_t num_used;
    uint32_t capacity;
    void *items;
} FsearchMemoryPoolBlock;

struct _FsearchMemoryPool {
    GList *blocks;
    uint32_t block_size;
    size_t item_size;
    GDestroyNotify item_free_func;
};

void
fsearch_memory_pool_new_block(FsearchMemoryPool *pool) {
    FsearchMemoryPoolBlock *block = calloc(1, sizeof(FsearchMemoryPoolBlock));
    assert(block != NULL);

    block->items = calloc(pool->block_size + 1, pool->item_size);
    assert(block->items != NULL);

    block->num_used = 0;
    block->capacity = pool->block_size;
    pool->blocks = g_list_prepend(pool->blocks, block);
}

FsearchMemoryPool *
fsearch_memory_pool_new(uint32_t block_size, size_t item_size, GDestroyNotify item_free_func) {
    FsearchMemoryPool *pool = calloc(1, sizeof(FsearchMemoryPool));
    assert(pool != NULL);
    pool->item_free_func = item_free_func;
    pool->block_size = block_size;
    pool->item_size = item_size;
    fsearch_memory_pool_new_block(pool);

    return pool;
}

void
fsearch_memory_pool_free_block(FsearchMemoryPoolBlock *block, FsearchMemoryPool *pool) {
    if (pool->item_free_func) {
        for (int i = 0; i < block->num_used; i++) {
            void *data = block->items + i * pool->item_size;
            if (!data) {
                break;
            }
            pool->item_free_func(data);
        }
    }
    g_clear_pointer(&block->items, free);
    g_clear_pointer(&block, free);
}

void
fsearch_memory_pool_free(FsearchMemoryPool *pool) {
    if (!pool) {
        return;
    }
    for (GList *b = pool->blocks; b != NULL; b = b->next) {
        FsearchMemoryPoolBlock *block = b->data;
        assert(block != NULL);
        fsearch_memory_pool_free_block(g_steal_pointer(&block), pool);
    }

    g_clear_pointer(&pool->blocks, g_list_free);

    g_clear_pointer(&pool, free);
}

bool
fsearch_memory_pool_is_block_full(FsearchMemoryPool *pool) {
    FsearchMemoryPoolBlock *block = pool->blocks->data;
    assert(block != NULL);
    if (block->num_used < block->capacity) {
        return false;
    }
    return true;
}

void *
fsearch_memory_pool_malloc(FsearchMemoryPool *pool) {
    if (!pool) {
        return NULL;
    }
    if (!pool->blocks || fsearch_memory_pool_is_block_full(pool)) {
        fsearch_memory_pool_new_block(pool);
    }
    FsearchMemoryPoolBlock *block = pool->blocks->data;
    assert(block != NULL);

    return block->items + block->num_used++ * pool->item_size;
}


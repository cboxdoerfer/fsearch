#include "fsearch_memory_pool.h"

#include <glib.h>
#include <stdio.h>

typedef struct FsearchMemoryPoolFreed {
    struct FsearchMemoryPoolFreed *next;
} FsearchMemoryPoolFreed;

typedef struct {
    uint32_t num_used;
    uint32_t capacity;
    void *items;
} FsearchMemoryPoolBlock;

struct FsearchMemoryPool {
    GList *blocks;
    FsearchMemoryPoolFreed *freed_items;
    uint32_t block_size;
    size_t item_size;
    GDestroyNotify item_free_func;

    GMutex mutex;
};

static void
fsearch_memory_pool_new_block(FsearchMemoryPool *pool) {
    FsearchMemoryPoolBlock *block = calloc(1, sizeof(FsearchMemoryPoolBlock));
    g_assert(block);

    block->items = calloc(pool->block_size + 1, pool->item_size);
    g_assert(block->items);

    block->num_used = 0;
    block->capacity = pool->block_size;
    pool->blocks = g_list_prepend(pool->blocks, block);
}

FsearchMemoryPool *
fsearch_memory_pool_new(uint32_t block_size, size_t item_size, GDestroyNotify item_free_func) {
    FsearchMemoryPool *pool = calloc(1, sizeof(FsearchMemoryPool));
    g_assert(pool);
    pool->item_free_func = item_free_func;
    pool->block_size = block_size;
    pool->item_size = MAX(item_size, sizeof(FsearchMemoryPoolFreed));

    g_mutex_init(&pool->mutex);

    fsearch_memory_pool_new_block(pool);

    return pool;
}

static void
fsearch_memory_pool_free_block(FsearchMemoryPool *pool, FsearchMemoryPoolBlock *block) {
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
fsearch_memory_pool_free_pool(FsearchMemoryPool *pool) {
    g_return_if_fail(pool);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&pool->mutex);
    g_assert_nonnull(locker);

    for (GList *b = pool->blocks; b != NULL; b = b->next) {
        FsearchMemoryPoolBlock *block = b->data;
        g_assert(block);
        fsearch_memory_pool_free_block(pool, g_steal_pointer(&block));
    }
    pool->freed_items = NULL;

    g_clear_pointer(&pool->blocks, g_list_free);

    g_clear_pointer(&locker, g_mutex_locker_free);

    g_mutex_clear(&pool->mutex);
    g_clear_pointer(&pool, free);
}

static bool
fsearch_memory_pool_is_block_full(FsearchMemoryPool *pool) {
    FsearchMemoryPoolBlock *block = pool->blocks->data;
    g_assert(block);
    if (block->num_used < block->capacity) {
        return false;
    }
    return true;
}

void
fsearch_memory_pool_free(FsearchMemoryPool *pool, void *item, bool item_clear) {
    g_return_if_fail(pool);
    g_return_if_fail(item);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&pool->mutex);
    g_assert_nonnull(locker);

    if (item_clear && pool->item_free_func) {
        pool->item_free_func(item);
    }
    FsearchMemoryPoolFreed *freed_items = pool->freed_items;
    pool->freed_items = item;
    pool->freed_items->next = freed_items;
}

void *
fsearch_memory_pool_malloc(FsearchMemoryPool *pool) {
    g_return_val_if_fail(pool, NULL);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&pool->mutex);
    g_assert_nonnull(locker);

    if (pool->freed_items) {
        void *freed_head = pool->freed_items;
        pool->freed_items = pool->freed_items->next;
        memset(freed_head, 0, pool->item_size);
        return freed_head;
    }

    if (!pool->blocks || fsearch_memory_pool_is_block_full(pool)) {
        fsearch_memory_pool_new_block(pool);
    }
    FsearchMemoryPoolBlock *block = pool->blocks->data;
    g_assert(block);

    return block->items + block->num_used++ * pool->item_size;
}

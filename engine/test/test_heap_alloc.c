#include "test_heap_alloc.h"

#include <stdlib.h>

static void *heap_malloc_fn(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void *heap_realloc_fn(void *ctx, void *ptr, size_t new_size)
{
    (void)ctx;
    return realloc(ptr, new_size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void heap_free_fn(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

Allocator allocator_heap(void)
{
    return (Allocator){
        .ctx = nullptr,
        .malloc_fn = heap_malloc_fn,
        .realloc_fn = heap_realloc_fn,
        .free_fn = heap_free_fn,
    };
}

Allocator test_heap_alloc;

void test_helpers_init(void)
{
    test_heap_alloc = allocator_heap();
}

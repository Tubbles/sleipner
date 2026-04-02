#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "alloc.h"
#include "arena.h"
#include "test_helpers.h"

#include <string.h>

void test_alloc_heap_malloc_and_free(void)
{
    Allocator alloc = allocator_heap();
    void *ptr = alloc.malloc_fn(alloc.ctx, 64);
    TEST_ASSERT_NOT_NULL(ptr);
    alloc.free_fn(alloc.ctx, ptr);
}

void test_alloc_heap_realloc(void)
{
    Allocator alloc = allocator_heap();

    int *ptr = alloc.malloc_fn(alloc.ctx, sizeof(int));
    TEST_ASSERT_NOT_NULL(ptr);
    *ptr = 123;

    int *grown = alloc.realloc_fn(alloc.ctx, ptr, 4 * sizeof(int));
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_EQUAL_INT(123, grown[0]);

    alloc.free_fn(alloc.ctx, grown);
}

void test_alloc_arena_malloc(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    Allocator alloc = allocator_arena(&arena);
    void *ptr = alloc.malloc_fn(alloc.ctx, 64);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(80, arena_used(&arena));

    arena_free(&arena);
}

void test_alloc_arena_free_is_noop(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    Allocator alloc = allocator_arena(&arena);
    void *ptr = alloc.malloc_fn(alloc.ctx, 32);
    size_t used_before = arena_used(&arena);

    /* free is a no-op — usage does not change */
    alloc.free_fn(alloc.ctx, ptr);
    TEST_ASSERT_EQUAL_size_t(used_before, arena_used(&arena));

    arena_free(&arena);
}

void test_alloc_arena_realloc_in_place(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    Allocator alloc = allocator_arena(&arena);

    int *ptr = alloc.malloc_fn(alloc.ctx, 2 * sizeof(int));
    ptr[0] = 7;
    ptr[1] = 8;

    int *grown = alloc.realloc_fn(alloc.ctx, ptr, 4 * sizeof(int));
    TEST_ASSERT_EQUAL_PTR(ptr, grown);
    TEST_ASSERT_EQUAL_INT(7, grown[0]);
    TEST_ASSERT_EQUAL_INT(8, grown[1]);

    arena_free(&arena);
}

#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "alloc.h"
#include "arena.h"

#include <string.h>

void test_alloc_heap_malloc_and_free(void)
{
    Allocator alloc = allocator_heap();
    void *ptr = alloc.malloc_fn(alloc.ctx, alloc.arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_NOT_NULL(ptr);
    alloc.free_fn(alloc.ctx, alloc.arena, ptr);
}

void test_alloc_heap_realloc(void)
{
    Allocator alloc = allocator_heap();

    int *ptr = alloc.malloc_fn(alloc.ctx, alloc.arena, (AllocRequest){.size = sizeof(int), .alignment = _Alignof(int)});
    TEST_ASSERT_NOT_NULL(ptr);
    *ptr = 123;

    int *grown = alloc.realloc_fn(alloc.ctx, alloc.arena, ptr, sizeof(int),
                                  (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_EQUAL_INT(123, grown[0]);

    alloc.free_fn(alloc.ctx, alloc.arena, grown);
}

void test_alloc_arena_malloc(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));

    Allocator alloc = allocator_arena(&ctx, &arena);
    void *ptr = alloc.malloc_fn(alloc.ctx, alloc.arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(64, arena_used(&arena));

    arena_free(&arena);
}

void test_alloc_arena_free_is_noop(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));

    Allocator alloc = allocator_arena(&ctx, &arena);
    void *ptr = alloc.malloc_fn(alloc.ctx, alloc.arena, (AllocRequest){.size = 32, .alignment = 1});
    size_t used_before = arena_used(&arena);

    /* free is a no-op — usage does not change */
    alloc.free_fn(alloc.ctx, alloc.arena, ptr);
    TEST_ASSERT_EQUAL_size_t(used_before, arena_used(&arena));

    arena_free(&arena);
}

void test_alloc_arena_realloc_in_place(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));

    Allocator alloc = allocator_arena(&ctx, &arena);

    int *ptr =
        alloc.malloc_fn(alloc.ctx, alloc.arena, (AllocRequest){.size = 2 * sizeof(int), .alignment = _Alignof(int)});
    ptr[0] = 7;
    ptr[1] = 8;

    int *grown = alloc.realloc_fn(alloc.ctx, alloc.arena, ptr, 2 * sizeof(int),
                                  (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
    TEST_ASSERT_EQUAL_PTR(ptr, grown);
    TEST_ASSERT_EQUAL_INT(7, grown[0]);
    TEST_ASSERT_EQUAL_INT(8, grown[1]);

    arena_free(&arena);
}

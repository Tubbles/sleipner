#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "arena.h"

#include <string.h>
#include <stdint.h>

void test_arena_init_and_free(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));
    TEST_ASSERT_NOT_NULL(arena.buffer);
    TEST_ASSERT_EQUAL_size_t(0, arena.offset);
    arena_free(&arena);
    TEST_ASSERT_NULL(arena.buffer);
}

void test_arena_alloc_basic(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    void *first = arena_alloc(&arena, 32);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    void *second = arena_alloc(&arena, 64);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_size_t(128, arena_used(&arena));

    /* Allocations should not overlap */
    TEST_ASSERT_TRUE((char *)second >= (char *)first + 32);

    arena_free(&arena);
}

void test_arena_alloc_alignment(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    /* Allocate 1 byte to misalign the raw offset */
    (void)arena_alloc(&arena, 1);

    /* Next allocation should still be max_align_t aligned */
    void *aligned = arena_alloc(&arena, 16);
    TEST_ASSERT_NOT_NULL(aligned);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)aligned % _Alignof(max_align_t));

    arena_free(&arena);
}

void test_arena_reset(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    (void)arena_alloc(&arena, 100);
    TEST_ASSERT_EQUAL_size_t(116, arena_used(&arena));

    arena_reset(&arena);
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));

    /* Can allocate again after reset */
    void *pointer = arena_alloc(&arena, 128);
    TEST_ASSERT_NOT_NULL(pointer);

    arena_free(&arena);
}

void test_arena_snapshot_restore(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    /* Write some data */
    int *values = arena_alloc(&arena, 4 * sizeof(int));
    values[0] = 10;
    values[1] = 20;
    values[2] = 30;
    values[3] = 40;

    /* Snapshot: save offset and copy buffer */
    size_t snapshot_offset = arena.offset;
    uint8_t snapshot[256];
    memcpy(snapshot, arena.buffer, snapshot_offset);

    /* Modify data */
    values[0] = 999;
    values[2] = 888;

    /* Allocate more stuff */
    (void)arena_alloc(&arena, 64);

    /* Restore snapshot */
    memcpy(arena.buffer, snapshot, snapshot_offset);
    arena.offset = snapshot_offset;

    /* Original data restored */
    TEST_ASSERT_EQUAL_INT(10, values[0]);
    TEST_ASSERT_EQUAL_INT(20, values[1]);
    TEST_ASSERT_EQUAL_INT(30, values[2]);
    TEST_ASSERT_EQUAL_INT(40, values[3]);
    TEST_ASSERT_EQUAL_size_t(snapshot_offset, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_save_restore_basic(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    (void)arena_alloc(&arena, 32);
    ArenaCheckpoint checkpoint = arena_save(&arena);
    TEST_ASSERT_EQUAL_size_t(48, checkpoint);

    (void)arena_alloc(&arena, 64);
    TEST_ASSERT_EQUAL_size_t(128, arena_used(&arena));

    arena_restore(&arena, checkpoint);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_save_restore_nested(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    (void)arena_alloc(&arena, 16);
    ArenaCheckpoint outer = arena_save(&arena);

    (void)arena_alloc(&arena, 32);
    ArenaCheckpoint inner = arena_save(&arena);

    (void)arena_alloc(&arena, 64);
    TEST_ASSERT_EQUAL_size_t(160, arena_used(&arena));

    arena_restore(&arena, inner);
    TEST_ASSERT_EQUAL_size_t(80, arena_used(&arena));

    arena_restore(&arena, outer);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_save_at_zero(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    ArenaCheckpoint checkpoint = arena_save(&arena);
    TEST_ASSERT_EQUAL_size_t(0, checkpoint);

    (void)arena_alloc(&arena, 64);
    arena_restore(&arena, checkpoint);
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_null_ptr_acts_as_alloc(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    void *ptr = arena_realloc(&arena, nullptr, 32);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_shrink_returns_same_ptr(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    void *ptr = arena_alloc(&arena, 64);
    size_t used_before = arena_used(&arena);

    void *shrunk = arena_realloc(&arena, ptr, 32);
    TEST_ASSERT_EQUAL_PTR(ptr, shrunk);
    TEST_ASSERT_EQUAL_size_t(used_before, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_in_place_when_at_top(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    int *values = arena_alloc(&arena, 2 * sizeof(int));
    values[0] = 10;
    values[1] = 20;

    int *grown = arena_realloc(&arena, values, 4 * sizeof(int));
    TEST_ASSERT_EQUAL_PTR(values, grown);
    TEST_ASSERT_EQUAL_INT(10, grown[0]);
    TEST_ASSERT_EQUAL_INT(20, grown[1]);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_copies_when_not_at_top(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    int *first = arena_alloc(&arena, 2 * sizeof(int));
    first[0] = 42;
    first[1] = 99;

    /* Allocate something else to push first off the top */
    (void)arena_alloc(&arena, 16);

    int *moved = arena_realloc(&arena, first, 4 * sizeof(int));
    TEST_ASSERT_NOT_NULL(moved);
    TEST_ASSERT_NOT_EQUAL(first, moved);
    TEST_ASSERT_EQUAL_INT(42, moved[0]);
    TEST_ASSERT_EQUAL_INT(99, moved[1]);

    arena_free(&arena);
}

static size_t scratch_scope_helper(Arena *arena)
{
    SCRATCH_SCOPE(arena);
    (void)arena_alloc(arena, 64);
    return arena_used(arena);
}

void test_arena_scratch_scope_auto_pop(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    (void)arena_alloc(&arena, 32);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    /* SCRATCH_SCOPE inside helper: should restore to 48 on return */
    size_t used_inside = scratch_scope_helper(&arena);
    TEST_ASSERT_EQUAL_size_t(128, used_inside);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_scratch_scope_nested(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx.error, &arena));

    {
        SCRATCH_SCOPE(&arena);
        (void)arena_alloc(&arena, 16);
        TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));

        {
            SCRATCH_SCOPE(&arena);
            (void)arena_alloc(&arena, 32);
            TEST_ASSERT_EQUAL_size_t(80, arena_used(&arena));
        }
        /* Inner scope popped */
        TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));
    }
    /* Outer scope popped */
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));

    arena_free(&arena);
}

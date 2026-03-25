#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "arena.h"

#include <string.h>
#include <stdint.h>

void test_arena_init_and_free(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 1024));
    TEST_ASSERT_NOT_NULL(arena.buffer);
    TEST_ASSERT_EQUAL_size_t(1024, arena.capacity);
    TEST_ASSERT_EQUAL_size_t(0, arena.offset);
    arena_free(&arena);
    TEST_ASSERT_NULL(arena.buffer);
}

void test_arena_alloc_basic(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 256));

    void *first = arena_alloc(&ctx, &arena, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));
    TEST_ASSERT_EQUAL_size_t(224, arena_remaining(&arena));

    void *second = arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_size_t(96, arena_used(&arena));

    /* Allocations should not overlap */
    TEST_ASSERT_TRUE((char *)second >= (char *)first + 32);

    arena_free(&arena);
}

void test_arena_alloc_alignment(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 256));

    /* Allocate 1 byte to misalign */
    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 1, .alignment = 1});

    /* Next allocation with 8-byte alignment */
    void *aligned = arena_alloc(&ctx, &arena, (AllocRequest){.size = 16, .alignment = 8});
    TEST_ASSERT_NOT_NULL(aligned);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)aligned % 8);

    arena_free(&arena);
}

void test_arena_alloc_returns_null_when_full(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 64));

    void *first = arena_alloc(&ctx, &arena, (AllocRequest){.size = 48, .alignment = 1});
    TEST_ASSERT_NOT_NULL(first);

    /* Not enough room for 32 more bytes */
    void *second = arena_alloc(&ctx, &arena, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_NULL(second);

    /* But smaller allocation still fits */
    void *third = arena_alloc(&ctx, &arena, (AllocRequest){.size = 16, .alignment = 1});
    TEST_ASSERT_NOT_NULL(third);

    arena_free(&arena);
}

void test_arena_reset(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 128));

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 100, .alignment = 1});
    TEST_ASSERT_EQUAL_size_t(100, arena_used(&arena));

    arena_reset(&arena);
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));
    TEST_ASSERT_EQUAL_size_t(128, arena_remaining(&arena));

    /* Can allocate again after reset */
    void *pointer = arena_alloc(&ctx, &arena, (AllocRequest){.size = 128, .alignment = 1});
    TEST_ASSERT_NOT_NULL(pointer);

    arena_free(&arena);
}

void test_arena_snapshot_restore(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 256));

    /* Write some data */
    int *values = arena_alloc(&ctx, &arena, (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
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
    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});

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
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 256));

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 32, .alignment = 1});
    ArenaCheckpoint checkpoint = arena_save(&arena);
    TEST_ASSERT_EQUAL_size_t(32, checkpoint);

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_EQUAL_size_t(96, arena_used(&arena));

    arena_restore(&arena, checkpoint);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_save_restore_nested(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 512));

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 16, .alignment = 1});
    ArenaCheckpoint outer = arena_save(&arena);

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 32, .alignment = 1});
    ArenaCheckpoint inner = arena_save(&arena);

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_EQUAL_size_t(112, arena_used(&arena));

    arena_restore(&arena, inner);
    TEST_ASSERT_EQUAL_size_t(48, arena_used(&arena));

    arena_restore(&arena, outer);
    TEST_ASSERT_EQUAL_size_t(16, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_save_at_zero(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 128));

    ArenaCheckpoint checkpoint = arena_save(&arena);
    TEST_ASSERT_EQUAL_size_t(0, checkpoint);

    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});
    arena_restore(&arena, checkpoint);
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_null_ptr_acts_as_alloc(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 128));

    void *ptr = arena_realloc(&ctx, &arena, NULL, 0, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_shrink_returns_same_ptr(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 128));

    void *ptr = arena_alloc(&ctx, &arena, (AllocRequest){.size = 64, .alignment = 1});
    size_t used_before = arena_used(&arena);

    void *shrunk = arena_realloc(&ctx, &arena, ptr, 64, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_EQUAL_PTR(ptr, shrunk);
    TEST_ASSERT_EQUAL_size_t(used_before, arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_in_place_when_at_top(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 128));

    int *values = arena_alloc(&ctx, &arena, (AllocRequest){.size = 2 * sizeof(int), .alignment = _Alignof(int)});
    values[0] = 10;
    values[1] = 20;

    int *grown = arena_realloc(&ctx, &arena, values, 2 * sizeof(int),
                               (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
    TEST_ASSERT_EQUAL_PTR(values, grown);
    TEST_ASSERT_EQUAL_INT(10, grown[0]);
    TEST_ASSERT_EQUAL_INT(20, grown[1]);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int), arena_used(&arena));

    arena_free(&arena);
}

void test_arena_realloc_copies_when_not_at_top(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 256));

    int *first = arena_alloc(&ctx, &arena, (AllocRequest){.size = 2 * sizeof(int), .alignment = _Alignof(int)});
    first[0] = 42;
    first[1] = 99;

    /* Allocate something else to push first off the top */
    (void)arena_alloc(&ctx, &arena, (AllocRequest){.size = 16, .alignment = 1});

    int *moved = arena_realloc(&ctx, &arena, first, 2 * sizeof(int),
                               (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
    TEST_ASSERT_NOT_NULL(moved);
    TEST_ASSERT_NOT_EQUAL(first, moved);
    TEST_ASSERT_EQUAL_INT(42, moved[0]);
    TEST_ASSERT_EQUAL_INT(99, moved[1]);

    arena_free(&arena);
}

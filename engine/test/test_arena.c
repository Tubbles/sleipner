#include "unity.h"
#include "arena.h"

#include <string.h>

void test_arena_init_and_free(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 1024));
    TEST_ASSERT_NOT_NULL(arena.buffer);
    TEST_ASSERT_EQUAL_size_t(1024, arena.capacity);
    TEST_ASSERT_EQUAL_size_t(0, arena.offset);
    arena_free(&arena);
    TEST_ASSERT_NULL(arena.buffer);
}

void test_arena_alloc_basic(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 256));

    void *first = arena_alloc(&arena, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_size_t(32, arena_used(&arena));
    TEST_ASSERT_EQUAL_size_t(224, arena_remaining(&arena));

    void *second = arena_alloc(&arena, (AllocRequest){.size = 64, .alignment = 1});
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_size_t(96, arena_used(&arena));

    /* Allocations should not overlap */
    TEST_ASSERT_TRUE((char *)second >= (char *)first + 32);

    arena_free(&arena);
}

void test_arena_alloc_alignment(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 256));

    /* Allocate 1 byte to misalign */
    (void)arena_alloc(&arena, (AllocRequest){.size = 1, .alignment = 1});

    /* Next allocation with 8-byte alignment */
    void *aligned = arena_alloc(&arena, (AllocRequest){.size = 16, .alignment = 8});
    TEST_ASSERT_NOT_NULL(aligned);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)aligned % 8);

    arena_free(&arena);
}

void test_arena_alloc_returns_null_when_full(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 64));

    void *first = arena_alloc(&arena, (AllocRequest){.size = 48, .alignment = 1});
    TEST_ASSERT_NOT_NULL(first);

    /* Not enough room for 32 more bytes */
    void *second = arena_alloc(&arena, (AllocRequest){.size = 32, .alignment = 1});
    TEST_ASSERT_NULL(second);

    /* But smaller allocation still fits */
    void *third = arena_alloc(&arena, (AllocRequest){.size = 16, .alignment = 1});
    TEST_ASSERT_NOT_NULL(third);

    arena_free(&arena);
}

void test_arena_reset(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 128));

    (void)arena_alloc(&arena, (AllocRequest){.size = 100, .alignment = 1});
    TEST_ASSERT_EQUAL_size_t(100, arena_used(&arena));

    arena_reset(&arena);
    TEST_ASSERT_EQUAL_size_t(0, arena_used(&arena));
    TEST_ASSERT_EQUAL_size_t(128, arena_remaining(&arena));

    /* Can allocate again after reset */
    void *pointer = arena_alloc(&arena, (AllocRequest){.size = 128, .alignment = 1});
    TEST_ASSERT_NOT_NULL(pointer);

    arena_free(&arena);
}

void test_arena_snapshot_restore(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 256));

    /* Write some data */
    int *values = arena_alloc(&arena, (AllocRequest){.size = 4 * sizeof(int), .alignment = _Alignof(int)});
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
    (void)arena_alloc(&arena, (AllocRequest){.size = 64, .alignment = 1});

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

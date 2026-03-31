#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "alloc.h"
#include "arena.h"
#include "map.h"

/* Custom hash that always maps to the same bucket — used to force probe chains
 * and tombstone scenarios without relying on hash distribution. */
static uint32_t always_same_hash(int key)
{
    (void)key;
    return 7;
}

static bool int_eq(int first, int second)
{
    return first == second;
}

MAP_DECL(probe_test, int, int)
MAP_IMPL(probe_test, int, int, always_same_hash, int_eq)

/* --- Initial state --- */

void test_map_initial_state_count_is_zero(void)
{
    map_int_int map = {0};
    TEST_ASSERT_EQUAL_INT(0, map.count);
}

void test_map_initial_state_capacity_is_zero(void)
{
    map_int_int map = {0};
    TEST_ASSERT_EQUAL_INT(0, map.capacity);
}

void test_map_initial_state_entries_is_null(void)
{
    map_int_int map = {0};
    TEST_ASSERT_NULL(map.entries);
}

/* --- Set and get --- */

void test_map_set_and_get_present(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 42, 100, NULL));
    const int *result = map_int_int_get(&map, 42);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(100, *result);
    map_int_int_free(&map, NULL);
}

void test_map_get_absent_returns_null(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 1, 10, NULL));
    TEST_ASSERT_NULL(map_int_int_get(&map, 99));
    map_int_int_free(&map, NULL);
}

void test_map_get_on_empty_returns_null(void)
{
    map_int_int map = {0};
    TEST_ASSERT_NULL(map_int_int_get(&map, 0));
}

/* --- Update --- */

void test_map_update_existing_replaces_value(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 5, 10, NULL));
    TEST_ASSERT_TRUE(map_int_int_set(&map, 5, 20, NULL));
    const int *result = map_int_int_get(&map, 5);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(20, *result);
    map_int_int_free(&map, NULL);
}

void test_map_update_count_unchanged(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 5, 10, NULL));
    int count_before = map.count;
    TEST_ASSERT_TRUE(map_int_int_set(&map, 5, 20, NULL));
    TEST_ASSERT_EQUAL_INT(count_before, map.count);
    map_int_int_free(&map, NULL);
}

/* --- Remove --- */

void test_map_remove_present(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 7, 77, NULL));
    TEST_ASSERT_TRUE(map_int_int_remove(&map, 7));
    TEST_ASSERT_NULL(map_int_int_get(&map, 7));
    map_int_int_free(&map, NULL);
}

void test_map_remove_decrements_count(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 1, 10, NULL));
    TEST_ASSERT_TRUE(map_int_int_set(&map, 2, 20, NULL));
    TEST_ASSERT_TRUE(map_int_int_remove(&map, 1));
    TEST_ASSERT_EQUAL_INT(1, map.count);
    map_int_int_free(&map, NULL);
}

void test_map_remove_absent_returns_false(void)
{
    map_int_int map = {0};
    TEST_ASSERT_FALSE(map_int_int_remove(&map, 99));
}

void test_map_remove_already_removed_returns_false(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 3, 30, NULL));
    TEST_ASSERT_TRUE(map_int_int_remove(&map, 3));
    TEST_ASSERT_FALSE(map_int_int_remove(&map, 3));
    map_int_int_free(&map, NULL);
}

/* --- Growth / rehash --- */

void test_map_growth_all_entries_retrievable(void)
{
    map_int_int map = {0};
    /* Insert enough entries to trigger multiple rehashes.
     * MAP_INITIAL_CAPACITY=16, so 13 entries crosses 75% and causes first rehash. */
    for (int index = 0; index < 32; index++) {
        TEST_ASSERT_TRUE(map_int_int_set(&map, index, index * 10, NULL));
    }
    TEST_ASSERT_EQUAL_INT(32, map.count);
    for (int index = 0; index < 32; index++) {
        const int *result = map_int_int_get(&map, index);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(index * 10, *result);
    }
    map_int_int_free(&map, NULL);
}

void test_map_growth_capacity_is_power_of_two(void)
{
    map_int_int map = {0};
    for (int index = 0; index < 20; index++) {
        TEST_ASSERT_TRUE(map_int_int_set(&map, index, index, NULL));
    }
    /* After growth, capacity should be a power of two >= 32. */
    TEST_ASSERT_TRUE(map.capacity >= 32);
    TEST_ASSERT_EQUAL_INT(0, map.capacity & (map.capacity - 1));
    map_int_int_free(&map, NULL);
}

/* --- Collision / probe chain (using always_same_hash) --- */

void test_map_collision_probe_chain(void)
{
    map_probe_test map = {0};
    /* All keys hash to slot 7; they fill consecutive slots via linear probing. */
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 100, 1, NULL));
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 200, 2, NULL));
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 300, 3, NULL));
    TEST_ASSERT_EQUAL_INT(3, map.count);
    const int *val_a = map_probe_test_get(&map, 100);
    const int *val_b = map_probe_test_get(&map, 200);
    const int *val_c = map_probe_test_get(&map, 300);
    TEST_ASSERT_NOT_NULL(val_a);
    TEST_ASSERT_NOT_NULL(val_b);
    TEST_ASSERT_NOT_NULL(val_c);
    TEST_ASSERT_EQUAL_INT(1, *val_a);
    TEST_ASSERT_EQUAL_INT(2, *val_b);
    TEST_ASSERT_EQUAL_INT(3, *val_c);
    map_probe_test_free(&map, NULL);
}

/* --- Tombstone --- */

void test_map_tombstone_get_finds_later_entry(void)
{
    map_probe_test map = {0};
    /* Insert A, B, C — all in the same probe chain (always_same_hash). */
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 10, 1, NULL));
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 20, 2, NULL));
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 30, 3, NULL));

    /* Remove the middle entry B — leaves a tombstone in the chain. */
    TEST_ASSERT_TRUE(map_probe_test_remove(&map, 20));

    /* C must still be reachable through the tombstone. */
    const int *result = map_probe_test_get(&map, 30);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(3, *result);

    /* A is still there too. */
    const int *result_a = map_probe_test_get(&map, 10);
    TEST_ASSERT_NOT_NULL(result_a);
    TEST_ASSERT_EQUAL_INT(1, *result_a);

    map_probe_test_free(&map, NULL);
}

void test_map_tombstone_reused_on_insert(void)
{
    map_probe_test map = {0};
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 10, 1, NULL));
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 20, 2, NULL));
    TEST_ASSERT_TRUE(map_probe_test_remove(&map, 10));
    int count_before = map.count;

    /* Inserting a new key should reuse the tombstone slot. */
    TEST_ASSERT_TRUE(map_probe_test_set(&map, 40, 4, NULL));
    TEST_ASSERT_EQUAL_INT(count_before + 1, map.count);

    const int *result = map_probe_test_get(&map, 40);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(4, *result);

    map_probe_test_free(&map, NULL);
}

/* --- Free --- */

void test_map_free_resets_to_zero(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 1, 10, NULL));
    map_int_int_free(&map, NULL);
    TEST_ASSERT_NULL(map.entries);
    TEST_ASSERT_EQUAL_INT(0, map.count);
    TEST_ASSERT_EQUAL_INT(0, map.capacity);
}

void test_map_free_safe_to_reuse(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 1, 10, NULL));
    map_int_int_free(&map, NULL);
    TEST_ASSERT_TRUE(map_int_int_set(&map, 2, 20, NULL));
    const int *result = map_int_int_get(&map, 2);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(20, *result);
    map_int_int_free(&map, NULL);
}

void test_map_free_on_empty_is_safe(void)
{
    map_int_int map = {0};
    map_int_int_free(&map, NULL);
    TEST_ASSERT_EQUAL_INT(0, map.count);
}

/* --- Multiple value types --- */

void test_map_int_float_set_and_get(void)
{
    map_int_float map = {0};
    TEST_ASSERT_TRUE(map_int_float_set(&map, 1, 3.14F, NULL));
    const float *result = map_int_float_get(&map, 1);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 3.14F, *result);
    map_int_float_free(&map, NULL);
}

void test_map_int_bool_set_and_get(void)
{
    map_int_bool map = {0};
    TEST_ASSERT_TRUE(map_int_bool_set(&map, 5, true, NULL));
    const bool *result = map_int_bool_get(&map, 5);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(*result);
    map_int_bool_free(&map, NULL);
}

void test_map_int_i64_set_and_get(void)
{
    map_int_i64 map = {0};
    TEST_ASSERT_TRUE(map_int_i64_set(&map, 99, 123456789012345LL, NULL));
    const int64_t *result = map_int_i64_get(&map, 99);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT64(123456789012345LL, *result);
    map_int_i64_free(&map, NULL);
}

/* --- Pointer-to-value semantics --- */

void test_map_get_returns_const_pointer_to_stored_slot(void)
{
    map_int_int map = {0};
    TEST_ASSERT_TRUE(map_int_int_set(&map, 7, 70, NULL));
    const int *slot = map_int_int_get(&map, 7);
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_INT(70, *slot);
    /* Update via set, then verify get reflects the change. */
    TEST_ASSERT_TRUE(map_int_int_set(&map, 7, 77, NULL));
    const int *check = map_int_int_get(&map, 7);
    TEST_ASSERT_NOT_NULL(check);
    TEST_ASSERT_EQUAL_INT(77, *check);
    map_int_int_free(&map, NULL);
}

/* --- Arena allocator --- */

void test_map_arena_allocator(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator alloc = allocator_arena(&ctx, &arena);

    map_int_int map = {0};
    for (int index = 0; index < 10; index++) {
        TEST_ASSERT_TRUE(map_int_int_set(&map, index, index * 2, &alloc));
    }
    TEST_ASSERT_EQUAL_INT(10, map.count);
    for (int index = 0; index < 10; index++) {
        const int *result = map_int_int_get(&map, index);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(index * 2, *result);
    }
    map_int_int_free(&map, &alloc);
    TEST_ASSERT_EQUAL_INT(0, map.count);
    arena_free(&arena);
}

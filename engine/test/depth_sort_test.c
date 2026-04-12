#include "unity.h"

#include "raylib.h"

#include "../src/depth_sort.c" // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static Entity make_entity_at(Vector2 position)
{
    Entity entity = {0};
    entity.position = position;
    return entity;
}

/* ---- Edge cases --------------------------------------------------------- */

void test_depth_sort_empty_array(void)
{
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(nullptr, 0, &alloc);
    TEST_ASSERT_NOT_NULL(sorted);
    free(sorted);
}

void test_depth_sort_single_entity(void)
{
    Entity entities[] = {make_entity_at((Vector2){5.0F, 10.0F})};
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 1, &alloc);
    TEST_ASSERT_EQUAL_INT(0, sorted[0]);
    free(sorted);
}

/* ---- Primary sort by Y -------------------------------------------------- */

void test_depth_sort_three_entities_by_y(void)
{
    Entity entities[] = {
        make_entity_at((Vector2){0.0F, 100.0F}),
        make_entity_at((Vector2){0.0F, 50.0F}),
        make_entity_at((Vector2){0.0F, 75.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 3, &alloc);
    TEST_ASSERT_EQUAL_INT(1, sorted[0]);
    TEST_ASSERT_EQUAL_INT(2, sorted[1]);
    TEST_ASSERT_EQUAL_INT(0, sorted[2]);
    free(sorted);
}

/* ---- Secondary sort by X (same Y) -------------------------------------- */

void test_depth_sort_same_y_sorted_by_x(void)
{
    Entity entities[] = {
        make_entity_at((Vector2){90.0F, 60.0F}),
        make_entity_at((Vector2){30.0F, 60.0F}),
        make_entity_at((Vector2){60.0F, 60.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 3, &alloc);
    TEST_ASSERT_EQUAL_INT(1, sorted[0]);
    TEST_ASSERT_EQUAL_INT(2, sorted[1]);
    TEST_ASSERT_EQUAL_INT(0, sorted[2]);
    free(sorted);
}

/* ---- Tertiary sort by index (same Y and X) ------------------------------ */

void test_depth_sort_same_position_sorted_by_index(void)
{
    Entity entities[] = {
        make_entity_at((Vector2){40.0F, 60.0F}),
        make_entity_at((Vector2){40.0F, 60.0F}),
        make_entity_at((Vector2){40.0F, 60.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 3, &alloc);
    TEST_ASSERT_EQUAL_INT(0, sorted[0]);
    TEST_ASSERT_EQUAL_INT(1, sorted[1]);
    TEST_ASSERT_EQUAL_INT(2, sorted[2]);
    free(sorted);
}

/* ---- Negative coordinates ----------------------------------------------- */

void test_depth_sort_negative_y_before_positive(void)
{
    Entity entities[] = {
        make_entity_at((Vector2){0.0F, 10.0F}),
        make_entity_at((Vector2){0.0F, -30.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 2, &alloc);
    TEST_ASSERT_EQUAL_INT(1, sorted[0]);
    TEST_ASSERT_EQUAL_INT(0, sorted[1]);
    free(sorted);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_depth_sort_empty_array);
    RUN_TEST(test_depth_sort_single_entity);
    RUN_TEST(test_depth_sort_three_entities_by_y);
    RUN_TEST(test_depth_sort_same_y_sorted_by_x);
    RUN_TEST(test_depth_sort_same_position_sorted_by_index);
    RUN_TEST(test_depth_sort_negative_y_before_positive);
    return UNITY_END();
}

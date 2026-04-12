#include "unity.h"

#include "raylib.h"

#include "../src/depth_sort.c" // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static Entity make_entity_with_collision(Rectangle collision)
{
    Entity entity = {0};
    entity.collision = collision;
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
    Entity entities[] = {make_entity_with_collision((Rectangle){.y = 10.0F, .height = 20.0F})};
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 1, &alloc);
    TEST_ASSERT_EQUAL_INT(0, sorted[0]);
    free(sorted);
}

/* ---- Basic sort --------------------------------------------------------- */

void test_depth_sort_three_entities_by_bottom_edge(void)
{
    Entity entities[] = {
        make_entity_with_collision((Rectangle){.y = 80.0F, .height = 20.0F}),
        make_entity_with_collision((Rectangle){.y = 30.0F, .height = 20.0F}),
        make_entity_with_collision((Rectangle){.y = 55.0F, .height = 20.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 3, &alloc);
    TEST_ASSERT_EQUAL_INT(1, sorted[0]);
    TEST_ASSERT_EQUAL_INT(2, sorted[1]);
    TEST_ASSERT_EQUAL_INT(0, sorted[2]);
    free(sorted);
}

/* ---- Tie handling ------------------------------------------------------- */

void test_depth_sort_same_y_all_indices_present(void)
{
    Entity entities[] = {
        make_entity_with_collision((Rectangle){.y = 40.0F, .height = 20.0F}),
        make_entity_with_collision((Rectangle){.y = 40.0F, .height = 20.0F}),
        make_entity_with_collision((Rectangle){.y = 40.0F, .height = 20.0F}),
    };
    Allocator alloc = allocator_heap();
    int *sorted = sort_entities_by_depth(entities, 3, &alloc);
    bool found[3] = {false, false, false};
    for (int index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(sorted[index] >= 0 && sorted[index] < 3);
        found[sorted[index]] = true;
    }
    TEST_ASSERT_TRUE(found[0]);
    TEST_ASSERT_TRUE(found[1]);
    TEST_ASSERT_TRUE(found[2]);
    free(sorted);
}

/* ---- Negative Y --------------------------------------------------------- */

void test_depth_sort_negative_y_before_positive(void)
{
    Entity entities[] = {
        make_entity_with_collision((Rectangle){.y = 10.0F, .height = 10.0F}),
        make_entity_with_collision((Rectangle){.y = -30.0F, .height = 10.0F}),
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
    RUN_TEST(test_depth_sort_three_entities_by_bottom_edge);
    RUN_TEST(test_depth_sort_same_y_all_indices_present);
    RUN_TEST(test_depth_sort_negative_y_before_positive);
    return UNITY_END();
}

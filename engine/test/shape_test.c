#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "shape.h"

void test_circle_bounds_centered(void)
{
    Rectangle bounds = shape_bounds(SHAPE_CIRCLE, (Vector2){100, 100}, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, bounds.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, bounds.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, bounds.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, bounds.height);
}

void test_square_bounds_same_as_circle(void)
{
    Rectangle circle_bounds = shape_bounds(SHAPE_CIRCLE, (Vector2){50, 50}, 2.0f);
    Rectangle square_bounds = shape_bounds(SHAPE_SQUARE, (Vector2){50, 50}, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, circle_bounds.x, square_bounds.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, circle_bounds.y, square_bounds.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, circle_bounds.width, square_bounds.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, circle_bounds.height, square_bounds.height);
}

void test_bounds_scale_affects_size(void)
{
    Rectangle bounds_small = shape_bounds(SHAPE_TRIANGLE, (Vector2){0, 0}, 1.0f);
    Rectangle bounds_large = shape_bounds(SHAPE_TRIANGLE, (Vector2){0, 0}, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, bounds_small.width * 2.0f, bounds_large.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, bounds_small.height * 2.0f, bounds_large.height);
}

void test_star_bounds_at_origin(void)
{
    Rectangle bounds = shape_bounds(SHAPE_STAR, (Vector2){0, 0}, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, bounds.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, bounds.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, bounds.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, bounds.height);
}

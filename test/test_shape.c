#include "unity.h"
#include "shape.h"

void test_circle_bounds_centered(void)
{
    Rectangle r = shape_bounds(SHAPE_CIRCLE, (Vector2){100, 100}, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, r.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, r.height);
}

void test_square_bounds_same_as_circle(void)
{
    Rectangle rc = shape_bounds(SHAPE_CIRCLE, (Vector2){50, 50}, 2.0f);
    Rectangle rs = shape_bounds(SHAPE_SQUARE, (Vector2){50, 50}, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, rc.x, rs.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, rc.y, rs.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, rc.width, rs.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, rc.height, rs.height);
}

void test_bounds_scale_affects_size(void)
{
    Rectangle r1 = shape_bounds(SHAPE_TRIANGLE, (Vector2){0, 0}, 1.0f);
    Rectangle r2 = shape_bounds(SHAPE_TRIANGLE, (Vector2){0, 0}, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, r1.width * 2.0f, r2.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, r1.height * 2.0f, r2.height);
}

void test_star_bounds_at_origin(void)
{
    Rectangle r = shape_bounds(SHAPE_STAR, (Vector2){0, 0}, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, r.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, r.height);
}

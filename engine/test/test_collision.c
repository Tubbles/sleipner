#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "collision.h"

#include <math.h>

/* --- Pairwise resolver tests --- */

void test_rect_rect_overlap(void)
{
    /* Two axis-aligned 20x20 rects overlapping by 10 on x */
    Vector2 push = resolve_rect_rect((Vector2){0, 0}, 0, 10, 10, (Vector2){15, 0}, 0, 10, 10);
    /* Push should be to the left (negative x), magnitude 5 */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, push.y);
}

void test_rect_rect_no_overlap(void)
{
    Vector2 push = resolve_rect_rect((Vector2){0, 0}, 0, 10, 10, (Vector2){30, 0}, 0, 10, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.y);
}

void test_rect_rect_rotated(void)
{
    /* Rect at 45 degrees overlapping with axis-aligned rect */
    Vector2 push = resolve_rect_rect((Vector2){0, 0}, 45, 10, 10, (Vector2){18, 0}, 0, 10, 10);
    /* Should still produce a non-zero push since they overlap */
    float mag = sqrtf(push.x * push.x + push.y * push.y);
    TEST_ASSERT_TRUE(mag > 0.1f);
}

void test_circle_circle_overlap(void)
{
    Vector2 push = resolve_circle_circle((Vector2){0, 0}, 10, (Vector2){15, 0}, 10);
    /* Overlap = 20 - 15 = 5, push along -x (A at origin pushed left, away from B) */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, push.y);
}

void test_circle_circle_no_overlap(void)
{
    Vector2 push = resolve_circle_circle((Vector2){0, 0}, 10, (Vector2){25, 0}, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.y);
}

void test_rect_circle_overlap(void)
{
    /* Circle at (15, 0) with radius 10 overlaps rect centered at origin, half 10x10 */
    Vector2 push = resolve_rect_circle((Vector2){0, 0}, 0, 10, 10, (Vector2){15, 0}, 10);
    /* Circle overlaps rect by 5 on the right side. Push for rect is to the left. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, push.y);
}

void test_rect_circle_no_overlap(void)
{
    Vector2 push = resolve_rect_circle((Vector2){0, 0}, 0, 10, 10, (Vector2){25, 0}, 5);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.y);
}

void test_circle_rect_is_negated(void)
{
    Vector2 rect_circle_push = resolve_rect_circle((Vector2){0, 0}, 0, 10, 10, (Vector2){15, 0}, 10);
    Vector2 circle_rect_push = resolve_circle_rect((Vector2){15, 0}, 10, (Vector2){0, 0}, 0, 10, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -rect_circle_push.x, circle_rect_push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -rect_circle_push.y, circle_rect_push.y);
}

/* --- Composite resolver tests --- */

void test_composite_single_rect_matches_rect_rect(void)
{
    CollisionShape shape_a = {0};
    (void)vec_collision_prim_push(&shape_a.prims, (CollisionPrimitive){.kind = COLLIDER_RECT, .rect = {10, 10}});
    CollisionShape shape_b = {0};
    (void)vec_collision_prim_push(&shape_b.prims, (CollisionPrimitive){.kind = COLLIDER_RECT, .rect = {10, 10}});

    Vector2 push = resolve_composite(&shape_a, (Vector2){0, 0}, 0, &shape_b, (Vector2){15, 0}, 0);
    Vector2 direct = resolve_rect_rect((Vector2){0, 0}, 0, 10, 10, (Vector2){15, 0}, 0, 10, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, direct.x, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, direct.y, push.y);
    vec_collision_prim_free(&shape_a.prims);
    vec_collision_prim_free(&shape_b.prims);
}

void test_composite_overlap_bool(void)
{
    CollisionShape shape_a = {0};
    (void)vec_collision_prim_push(&shape_a.prims, (CollisionPrimitive){.kind = COLLIDER_CIRCLE, .circle = {20}});
    CollisionShape shape_b = {0};
    (void)vec_collision_prim_push(&shape_b.prims, (CollisionPrimitive){.kind = COLLIDER_CIRCLE, .circle = {20}});

    TEST_ASSERT_TRUE(composite_overlap(&shape_a, (Vector2){0, 0}, 0, &shape_b, (Vector2){30, 0}, 0));
    TEST_ASSERT_FALSE(composite_overlap(&shape_a, (Vector2){0, 0}, 0, &shape_b, (Vector2){50, 0}, 0));
    vec_collision_prim_free(&shape_a.prims);
    vec_collision_prim_free(&shape_b.prims);
}

void test_composite_wall(void)
{
    CollisionShape shape = {0};
    (void)vec_collision_prim_push(&shape.prims, (CollisionPrimitive){.kind = COLLIDER_RECT, .rect = {10, 10}});
    Rectangle wall = {20, -10, 200, 20};
    Vector2 push = resolve_composite_wall(&shape, (Vector2){15, 0}, 0, wall);
    /* Entity at x=15 with hw=10 extends to x=25, wall starts at x=20 */
    TEST_ASSERT_TRUE(push.x < 0); /* should push left */
    vec_collision_prim_free(&shape.prims);
}

/* --- Triangle resolver tests --- */

void test_tri_tri_overlap(void)
{
    Vector2 verts_a[3] = {{0, -10}, {-10, 10}, {10, 10}};
    Vector2 verts_b[3] = {{0, -10}, {-10, 10}, {10, 10}};
    Vector2 push = resolve_tri_tri((Vector2){0, 0}, verts_a, (Vector2){5, 0}, verts_b);
    float mag = sqrtf(push.x * push.x + push.y * push.y);
    TEST_ASSERT_TRUE(mag > 0.1f);
}

void test_tri_tri_no_overlap(void)
{
    Vector2 verts_a[3] = {{0, -10}, {-10, 10}, {10, 10}};
    Vector2 verts_b[3] = {{0, -10}, {-10, 10}, {10, 10}};
    Vector2 push = resolve_tri_tri((Vector2){0, 0}, verts_a, (Vector2){30, 0}, verts_b);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, push.y);
}

void test_tri_circle_overlap(void)
{
    Vector2 verts[3] = {{0, -20}, {-20, 20}, {20, 20}};
    /* Circle at (15,0) r=10 overlaps the triangle's right edge */
    Vector2 push = resolve_tri_circle((Vector2){0, 0}, verts, (Vector2){15, 0}, 10);
    float mag = sqrtf(push.x * push.x + push.y * push.y);
    TEST_ASSERT_TRUE(mag > 0.1f);
}

void test_tri_rect_overlap(void)
{
    Vector2 verts[3] = {{0, -20}, {-20, 20}, {20, 20}};
    /* Rect at (15,0) hw=10 hh=10 overlaps the triangle */
    Vector2 push = resolve_tri_rect((Vector2){0, 0}, verts, (Vector2){15, 0}, 0, 10, 10);
    float mag = sqrtf(push.x * push.x + push.y * push.y);
    TEST_ASSERT_TRUE(mag > 0.1f);
}

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* test_stub.c */
void test_stub_passes(void);

/* test_arena.c */
void test_arena_init_and_free(void);
void test_arena_alloc_basic(void);
void test_arena_alloc_alignment(void);
void test_arena_alloc_returns_null_when_full(void);
void test_arena_reset(void);
void test_arena_snapshot_restore(void);

/* test_shape.c */
void test_circle_bounds_centered(void);
void test_square_bounds_same_as_circle(void);
void test_bounds_scale_affects_size(void);
void test_star_bounds_at_origin(void);

/* test_particle.c */
void test_particle_init(void);
void test_particle_spawn_increases_count(void);
void test_particle_lifetime_expiry(void);
void test_particle_position_updates(void);
void test_particle_capacity_grows(void);
void test_particle_free_cleans_up(void);

/* test_collision.c */
void test_rect_rect_overlap(void);
void test_rect_rect_no_overlap(void);
void test_rect_rect_rotated(void);
void test_circle_circle_overlap(void);
void test_circle_circle_no_overlap(void);
void test_rect_circle_overlap(void);
void test_rect_circle_no_overlap(void);
void test_circle_rect_is_negated(void);
void test_composite_single_rect_matches_rect_rect(void);
void test_composite_overlap_bool(void);
void test_composite_wall(void);
void test_tri_tri_overlap(void);
void test_tri_tri_no_overlap(void);
void test_tri_circle_overlap(void);
void test_tri_rect_overlap(void);

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_stub_passes);

    RUN_TEST(test_arena_init_and_free);
    RUN_TEST(test_arena_alloc_basic);
    RUN_TEST(test_arena_alloc_alignment);
    RUN_TEST(test_arena_alloc_returns_null_when_full);
    RUN_TEST(test_arena_reset);
    RUN_TEST(test_arena_snapshot_restore);

    RUN_TEST(test_circle_bounds_centered);
    RUN_TEST(test_square_bounds_same_as_circle);
    RUN_TEST(test_bounds_scale_affects_size);
    RUN_TEST(test_star_bounds_at_origin);

    RUN_TEST(test_particle_init);
    RUN_TEST(test_particle_spawn_increases_count);
    RUN_TEST(test_particle_lifetime_expiry);
    RUN_TEST(test_particle_position_updates);
    RUN_TEST(test_particle_capacity_grows);
    RUN_TEST(test_particle_free_cleans_up);

    RUN_TEST(test_rect_rect_overlap);
    RUN_TEST(test_rect_rect_no_overlap);
    RUN_TEST(test_rect_rect_rotated);
    RUN_TEST(test_circle_circle_overlap);
    RUN_TEST(test_circle_circle_no_overlap);
    RUN_TEST(test_rect_circle_overlap);
    RUN_TEST(test_rect_circle_no_overlap);
    RUN_TEST(test_circle_rect_is_negated);
    RUN_TEST(test_composite_single_rect_matches_rect_rect);
    RUN_TEST(test_composite_overlap_bool);
    RUN_TEST(test_composite_wall);
    RUN_TEST(test_tri_tri_overlap);
    RUN_TEST(test_tri_tri_no_overlap);
    RUN_TEST(test_tri_circle_overlap);
    RUN_TEST(test_tri_rect_overlap);

    return UNITY_END();
}

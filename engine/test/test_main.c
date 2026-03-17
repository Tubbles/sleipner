#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* test_stub.c */
void test_stub_passes(void);

/* test_attribute.c */
void test_attr_set_and_get_float(void);
void test_attr_set_and_get_int(void);
void test_attr_set_and_get_bool(void);
void test_attr_set_and_get_string(void);
void test_attr_overwrite_existing(void);
void test_attr_get_missing_returns_fallback(void);
void test_attr_full_set_returns_false(void);
void test_attr_type_change(void);
void test_attr_scoped_instance_overrides_blueprint(void);
void test_attr_multiple_types(void);

/* test_arena.c */
void test_arena_init_and_free(void);
void test_arena_alloc_basic(void);
void test_arena_alloc_alignment(void);
void test_arena_alloc_returns_null_when_full(void);
void test_arena_reset(void);
void test_arena_snapshot_restore(void);

/* test_blueprint.c */
void test_blueprint_load_single(void);
void test_blueprint_load_multiple(void);
void test_blueprint_find(void);
void test_blueprint_skip_nameless(void);
void test_blueprint_no_blueprints_section(void);

/* test_game.c */
void test_game_init_defaults(void);
void test_game_update_increments_frame(void);
void test_game_update_accumulates_elapsed(void);
void test_game_update_player_moves_right(void);
void test_game_update_player_moves_left(void);
void test_game_update_no_input_no_movement(void);
void test_game_player_clamps_to_bounds(void);
void test_game_player_hitbox_position(void);
void test_game_update_resolves_obstacle_collision(void);

/* test_level.c */
void test_level_load_first(void);
void test_level_load_by_name(void);
void test_level_load_nonexistent(void);
void test_level_entity_positions(void);
void test_level_entity_source_rects(void);

/* test_integration.c */
void test_integration_load_gamedata(void);
void test_integration_load_specific_level(void);
void test_integration_walk_and_collide(void);
void test_integration_walk_freely(void);
void test_integration_boundary_all_directions(void);

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

/* test_toml_emitter.c */
void test_toml_emit_blueprints(void);
void test_toml_emit_level_with_entities(void);
void test_toml_emit_round_trip(void);
void test_toml_emit_buffer_too_small(void);
void test_toml_emit_no_music(void);

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

    RUN_TEST(test_attr_set_and_get_float);
    RUN_TEST(test_attr_set_and_get_int);
    RUN_TEST(test_attr_set_and_get_bool);
    RUN_TEST(test_attr_set_and_get_string);
    RUN_TEST(test_attr_overwrite_existing);
    RUN_TEST(test_attr_get_missing_returns_fallback);
    RUN_TEST(test_attr_full_set_returns_false);
    RUN_TEST(test_attr_type_change);
    RUN_TEST(test_attr_scoped_instance_overrides_blueprint);
    RUN_TEST(test_attr_multiple_types);

    RUN_TEST(test_arena_init_and_free);
    RUN_TEST(test_arena_alloc_basic);
    RUN_TEST(test_arena_alloc_alignment);
    RUN_TEST(test_arena_alloc_returns_null_when_full);
    RUN_TEST(test_arena_reset);
    RUN_TEST(test_arena_snapshot_restore);

    RUN_TEST(test_blueprint_load_single);
    RUN_TEST(test_blueprint_load_multiple);
    RUN_TEST(test_blueprint_find);
    RUN_TEST(test_blueprint_skip_nameless);
    RUN_TEST(test_blueprint_no_blueprints_section);

    RUN_TEST(test_game_init_defaults);
    RUN_TEST(test_game_update_increments_frame);
    RUN_TEST(test_game_update_accumulates_elapsed);
    RUN_TEST(test_game_update_player_moves_right);
    RUN_TEST(test_game_update_player_moves_left);
    RUN_TEST(test_game_update_no_input_no_movement);
    RUN_TEST(test_game_player_clamps_to_bounds);
    RUN_TEST(test_game_player_hitbox_position);
    RUN_TEST(test_game_update_resolves_obstacle_collision);

    RUN_TEST(test_integration_load_gamedata);
    RUN_TEST(test_integration_load_specific_level);
    RUN_TEST(test_integration_walk_and_collide);
    RUN_TEST(test_integration_walk_freely);
    RUN_TEST(test_integration_boundary_all_directions);

    RUN_TEST(test_level_load_first);
    RUN_TEST(test_level_load_by_name);
    RUN_TEST(test_level_load_nonexistent);
    RUN_TEST(test_level_entity_positions);
    RUN_TEST(test_level_entity_source_rects);

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

    RUN_TEST(test_toml_emit_blueprints);
    RUN_TEST(test_toml_emit_level_with_entities);
    RUN_TEST(test_toml_emit_round_trip);
    RUN_TEST(test_toml_emit_buffer_too_small);
    RUN_TEST(test_toml_emit_no_music);

    return UNITY_END();
}

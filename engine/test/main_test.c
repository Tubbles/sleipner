#include "unity.h"
#include "test_helpers.h"

void setUp(void) {}
void tearDown(void) {}

/* blueprint_test.c */
void test_blueprint_load_single(void);
void test_blueprint_load_multiple(void);
void test_blueprint_find(void);
void test_blueprint_skip_nameless(void);
void test_blueprint_no_blueprints_section(void);
void test_blueprint_custom_attrs(void);
void test_blueprint_health_parsed(void);
void test_blueprint_extends(void);
void test_blueprint_extends_chain(void);
void test_blueprint_child_parsed(void);
void test_blueprint_multiple_children(void);
void test_blueprint_child_no_tag(void);
void test_blueprint_child_default_offset(void);

/* game_test.c */
void test_game_init_defaults(void);
void test_game_update_increments_frame(void);
void test_game_update_accumulates_elapsed(void);
void test_game_update_player_moves_right(void);
void test_game_update_player_moves_left(void);
void test_game_update_no_input_no_movement(void);
void test_game_player_clamps_to_bounds(void);
void test_game_player_collision_from_blueprint(void);
void test_game_update_resolves_obstacle_collision(void);
void test_camera_follows_player(void);
void test_camera_clamped_to_level_bounds(void);
void test_camera_centers_small_level(void);
void test_camera_snaps_on_load(void);

/* level_test.c */
void test_level_load_first(void);
void test_level_load_by_name(void);
void test_level_load_nonexistent(void);
void test_level_entity_positions(void);
void test_level_entity_source_rects(void);
void test_level_child_entities_instantiated(void);
void test_level_child_entity_positions(void);
void test_level_child_entity_tags(void);
void test_level_nested_children(void);

/* integration_test.c */
void test_integration_load_gamedata(void);
void test_integration_load_specific_level(void);
void test_integration_walk_and_collide(void);
void test_integration_walk_freely(void);
void test_integration_boundary_all_directions(void);
void test_integration_player_entity_spawns(void);
void test_integration_on_spawn_trigger_fires_on_load(void);
void test_integration_enter_trigger_fires_on_overlap(void);
void test_integration_enter_trigger_fires_only_once(void);
void test_integration_real_gamedata_loads(void);
void test_integration_real_gamedata_all_levels_load(void);
void test_integration_transition_changes_level(void);
void test_integration_editor_pan_does_not_reset_player_position(void);
void test_integration_editor_undo_at_left_edge_preserves_play_state(void);
void test_integration_editor_attr_edit_tap_decrements_by_one(void);
void test_integration_editor_attr_edit_hold_repeats_after_delay(void);
void test_integration_menu_navigation_and_quit(void);
void test_integration_menu_escape_returns_resume(void);
void test_integration_menu_gamepad_navigation(void);
void test_integration_settings_tab_switch(void);
void test_integration_settings_path_edit_commit(void);
void test_integration_settings_path_edit_parent_lists_contents(void);
void test_integration_settings_path_edit_drive_select_round_trip(void);
void test_integration_settings_path_edit_commit_normalizes_separators(void);

/* toml_emitter_test.c */
void test_toml_emit_blueprints(void);
void test_toml_emit_level_with_entities(void);
void test_toml_emit_round_trip(void);
void test_toml_emit_buffer_too_small(void);
void test_toml_emit_no_music(void);
void test_toml_emit_blueprint_children(void);
void test_toml_emit_skips_child_entities(void);
void test_toml_emit_custom_attrs(void);
void test_toml_emit_health(void);
void test_toml_emit_persisted_attrs(void);
void test_toml_emit_no_persisted_attrs(void);
void test_toml_emit_rules(void);
void test_toml_emit_bindings_round_trip_defaults(void);
void test_toml_emit_bindings_round_trip_after_mutation(void);
void test_toml_load_bindings_missing_file_keeps_defaults(void);

/* sprite_offset_integration_test.c */
void test_integration_sprite_offset_edit_updates_entity_live(void);
void test_integration_sprite_offset_edit_emitted_to_toml(void);

/* collision_offset_integration_test.c */
void test_integration_collision_offset_edit_updates_entity_live(void);
void test_integration_collision_offset_edit_emitted_to_toml(void);

/* undo_test.c */
void test_undo_new_entry_and_step_back(void);
void test_undo_step_forward(void);
void test_undo_truncate_on_new_edit(void);
void test_undo_clear(void);
void test_undo_dirty_tracking(void);
void test_undo_description(void);
void test_undo_discard(void);
void test_undo_discard_preserves_previous(void);
void test_undo_dirty_invalidated_by_truncation(void);
void test_undo_entity_spawn_and_undo(void);
void test_undo_entity_move_and_undo(void);
void test_undo_attribute_change_and_undo(void);

/* rule_integration_test.c */
void test_rules_parse_from_toml(void);
void test_rules_parse_no_rules(void);
void test_rules_parse_multiple_rules(void);
void test_integration_interact_rule(void);
void test_integration_condition_blocks_interact(void);
void test_integration_for_each_no_bind_iterates_all_entities(void);
void test_integration_for_each_condition_filter(void);
void test_integration_for_each_bind_mode(void);
void test_integration_subroutine_call(void);
void test_integration_subroutine_inherits_self(void);
void test_integration_subroutine_missing_is_soft_fail(void);
void test_integration_timer_oneshot_fires_once(void);
void test_integration_timer_periodic_fires_repeatedly(void);
void test_integration_timer_destroy_cancels(void);
void test_integration_on_destroy_fires(void);
void test_integration_defeat_fires_when_health_drops_to_zero(void);
void test_integration_collide_fires_on_overlap(void);

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_blueprint_load_single);
    RUN_TEST(test_blueprint_load_multiple);
    RUN_TEST(test_blueprint_find);
    RUN_TEST(test_blueprint_skip_nameless);
    RUN_TEST(test_blueprint_no_blueprints_section);
    RUN_TEST(test_blueprint_custom_attrs);
    RUN_TEST(test_blueprint_health_parsed);
    RUN_TEST(test_blueprint_extends);
    RUN_TEST(test_blueprint_extends_chain);
    RUN_TEST(test_blueprint_child_parsed);
    RUN_TEST(test_blueprint_multiple_children);
    RUN_TEST(test_blueprint_child_no_tag);
    RUN_TEST(test_blueprint_child_default_offset);

    RUN_TEST(test_game_init_defaults);
    RUN_TEST(test_game_update_increments_frame);
    RUN_TEST(test_game_update_accumulates_elapsed);
    RUN_TEST(test_game_update_player_moves_right);
    RUN_TEST(test_game_update_player_moves_left);
    RUN_TEST(test_game_update_no_input_no_movement);
    RUN_TEST(test_game_player_clamps_to_bounds);
    RUN_TEST(test_game_player_collision_from_blueprint);
    RUN_TEST(test_game_update_resolves_obstacle_collision);
    RUN_TEST(test_camera_follows_player);
    RUN_TEST(test_camera_clamped_to_level_bounds);
    RUN_TEST(test_camera_centers_small_level);
    RUN_TEST(test_camera_snaps_on_load);

    RUN_TEST(test_integration_load_gamedata);
    RUN_TEST(test_integration_load_specific_level);
    RUN_TEST(test_integration_walk_and_collide);
    RUN_TEST(test_integration_walk_freely);
    RUN_TEST(test_integration_boundary_all_directions);
    RUN_TEST(test_integration_player_entity_spawns);
    RUN_TEST(test_integration_on_spawn_trigger_fires_on_load);
    RUN_TEST(test_integration_enter_trigger_fires_on_overlap);
    RUN_TEST(test_integration_enter_trigger_fires_only_once);
    RUN_TEST(test_integration_real_gamedata_loads);
    RUN_TEST(test_integration_real_gamedata_all_levels_load);
    RUN_TEST(test_integration_transition_changes_level);
    RUN_TEST(test_integration_editor_pan_does_not_reset_player_position);
    RUN_TEST(test_integration_editor_undo_at_left_edge_preserves_play_state);
    RUN_TEST(test_integration_editor_attr_edit_tap_decrements_by_one);
    RUN_TEST(test_integration_editor_attr_edit_hold_repeats_after_delay);
    RUN_TEST(test_integration_menu_navigation_and_quit);
    RUN_TEST(test_integration_menu_escape_returns_resume);
    RUN_TEST(test_integration_menu_gamepad_navigation);
    RUN_TEST(test_integration_settings_tab_switch);
    RUN_TEST(test_integration_settings_path_edit_commit);
    RUN_TEST(test_integration_settings_path_edit_parent_lists_contents);
    RUN_TEST(test_integration_settings_path_edit_drive_select_round_trip);
    RUN_TEST(test_integration_settings_path_edit_commit_normalizes_separators);

    RUN_TEST(test_level_load_first);
    RUN_TEST(test_level_load_by_name);
    RUN_TEST(test_level_load_nonexistent);
    RUN_TEST(test_level_entity_positions);
    RUN_TEST(test_level_entity_source_rects);
    RUN_TEST(test_level_child_entities_instantiated);
    RUN_TEST(test_level_child_entity_positions);
    RUN_TEST(test_level_child_entity_tags);
    RUN_TEST(test_level_nested_children);

    RUN_TEST(test_rules_parse_from_toml);
    RUN_TEST(test_rules_parse_no_rules);
    RUN_TEST(test_rules_parse_multiple_rules);
    RUN_TEST(test_integration_interact_rule);
    RUN_TEST(test_integration_condition_blocks_interact);
    RUN_TEST(test_integration_for_each_no_bind_iterates_all_entities);
    RUN_TEST(test_integration_for_each_condition_filter);
    RUN_TEST(test_integration_for_each_bind_mode);
    RUN_TEST(test_integration_subroutine_call);
    RUN_TEST(test_integration_subroutine_inherits_self);
    RUN_TEST(test_integration_subroutine_missing_is_soft_fail);
    RUN_TEST(test_integration_timer_oneshot_fires_once);
    RUN_TEST(test_integration_timer_periodic_fires_repeatedly);
    RUN_TEST(test_integration_timer_destroy_cancels);
    RUN_TEST(test_integration_on_destroy_fires);
    RUN_TEST(test_integration_defeat_fires_when_health_drops_to_zero);
    RUN_TEST(test_integration_collide_fires_on_overlap);

    RUN_TEST(test_integration_sprite_offset_edit_updates_entity_live);
    RUN_TEST(test_integration_sprite_offset_edit_emitted_to_toml);

    RUN_TEST(test_integration_collision_offset_edit_updates_entity_live);
    RUN_TEST(test_integration_collision_offset_edit_emitted_to_toml);

    RUN_TEST(test_undo_new_entry_and_step_back);
    RUN_TEST(test_undo_step_forward);
    RUN_TEST(test_undo_truncate_on_new_edit);
    RUN_TEST(test_undo_clear);
    RUN_TEST(test_undo_dirty_tracking);
    RUN_TEST(test_undo_description);
    RUN_TEST(test_undo_discard);
    RUN_TEST(test_undo_discard_preserves_previous);
    RUN_TEST(test_undo_dirty_invalidated_by_truncation);
    RUN_TEST(test_undo_entity_spawn_and_undo);
    RUN_TEST(test_undo_entity_move_and_undo);
    RUN_TEST(test_undo_attribute_change_and_undo);

    RUN_TEST(test_toml_emit_blueprints);
    RUN_TEST(test_toml_emit_level_with_entities);
    RUN_TEST(test_toml_emit_round_trip);
    RUN_TEST(test_toml_emit_buffer_too_small);
    RUN_TEST(test_toml_emit_no_music);
    RUN_TEST(test_toml_emit_blueprint_children);
    RUN_TEST(test_toml_emit_skips_child_entities);
    RUN_TEST(test_toml_emit_custom_attrs);
    RUN_TEST(test_toml_emit_health);
    RUN_TEST(test_toml_emit_persisted_attrs);
    RUN_TEST(test_toml_emit_no_persisted_attrs);
    RUN_TEST(test_toml_emit_rules);
    RUN_TEST(test_toml_emit_bindings_round_trip_defaults);
    RUN_TEST(test_toml_emit_bindings_round_trip_after_mutation);
    RUN_TEST(test_toml_load_bindings_missing_file_keeps_defaults);

    return UNITY_END();
}

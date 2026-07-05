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
void test_blueprint_animation_parsed(void);
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
void test_camera_pan_position_at_start_is_from(void);
void test_camera_pan_position_at_half_duration_is_midpoint(void);
void test_camera_pan_position_at_duration_is_target(void);
void test_camera_pan_position_clamps_past_duration(void);
void test_camera_shake_magnitude_full_at_zero_elapsed(void);
void test_camera_shake_magnitude_half_at_half_duration(void);
void test_camera_shake_magnitude_zero_at_and_past_duration(void);
void test_camera_shake_magnitude_never_negative(void);

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
void test_level_tile_dims_ceil_division(void);
void test_level_tile_index_row_major(void);

/* render_test.c */
void test_render_split_camera_target_separates_integer_and_fraction(void);
void test_render_split_camera_target_exact_integer_has_zero_fraction(void);
void test_render_upscale_dest_rect_matches_formula(void);
void test_render_world_to_screen_position_continuous_sweeping_x_at_wall(void);
void test_render_world_to_screen_position_continuous_sweeping_y_at_wall(void);

/* integration_test.c */
void test_integration_load_gamedata(void);
void test_integration_load_specific_level(void);
void test_integration_walk_and_collide(void);
void test_integration_walk_freely(void);
void test_integration_boundary_all_directions(void);
void test_integration_wall_press_no_position_jump(void);
void test_integration_player_entity_spawns(void);
void test_integration_on_spawn_trigger_fires_on_load(void);
void test_integration_enter_trigger_fires_on_overlap(void);
void test_integration_enter_trigger_fires_only_once(void);
void test_integration_change_sprite_action_updates_source_rect(void);
void test_integration_play_sound_enqueues(void);
void test_integration_camera_pan_moves_target(void);
void test_integration_spawn_creates_entity(void);
void test_integration_spawn_inside_for_each_deferred(void);
void test_integration_real_gamedata_loads(void);
void test_integration_real_gamedata_all_levels_load(void);
void test_integration_transition_changes_level(void);
void test_integration_progression_survives_transition(void);
void test_integration_progression_survives_hot_reload(void);
void test_integration_progression_restore_clears_progression(void);
void test_integration_give_item_then_has_item(void);
void test_integration_give_item_shows_toast(void);
void test_integration_give_item_toast_fades_in_play_mode(void);
void test_integration_remove_item_at_zero(void);
void test_integration_item_survives_transition(void);
void test_integration_restore_clears_items(void);
void test_integration_editor_pan_does_not_reset_player_position(void);
void test_integration_editor_undo_at_left_edge_preserves_play_state(void);
void test_integration_editor_attr_edit_tap_decrements_by_one(void);
void test_integration_editor_attr_edit_hold_repeats_after_delay(void);
void test_integration_editor_selection_survives_deleting_different_entity(void);
void test_integration_editor_selection_survives_undo_of_edit(void);
void test_integration_editor_multiselect_group_move(void);
void test_integration_editor_copy_paste(void);
void test_integration_editor_watch_list_removes_focused_entry(void);
void test_integration_editor_radial_keyboard_nav(void);
void test_integration_editor_radial_handles_entry(void);
void test_integration_editor_radial_handles_requires_selection(void);
void test_integration_editor_level_switch_round_trip(void);
void test_integration_editor_level_create_round_trip(void);
void test_integration_editor_level_edit_detail_round_trip(void);
void test_integration_editor_tile_paint_round_trip(void);
void test_integration_editor_atlas_region_create_round_trip(void);
void test_integration_editor_animation_edit_round_trip(void);
void test_integration_editor_rule_tree_navigation(void);
void test_integration_editor_rule_leaf_edit_round_trip(void);
void test_integration_editor_rule_structural_edit(void);
void test_integration_editor_subroutine_edit_round_trip(void);
void test_integration_menu_navigation_and_quit(void);
void test_integration_menu_escape_returns_resume(void);
void test_integration_menu_gamepad_navigation(void);
void test_integration_settings_tab_switch(void);
void test_integration_settings_path_edit_commit(void);
void test_integration_settings_path_edit_parent_lists_contents(void);
void test_integration_settings_path_edit_drive_select_round_trip(void);
void test_integration_settings_path_edit_commit_normalizes_separators(void);
void test_integration_settings_path_edit_buffer_row_enters_keyboard_mode(void);

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
void test_toml_emit_animation_round_trip(void);
void test_toml_emit_persisted_attrs(void);
void test_toml_emit_no_persisted_attrs(void);
void test_toml_emit_child_persisted_attrs_round_trip(void);
void test_toml_emit_rules(void);
void test_toml_emit_camera_pan_shake_round_trip(void);
void test_toml_emit_spawn_round_trip(void);
void test_toml_emit_collision_composite_round_trip(void);
void test_toml_emit_nested_control_flow_round_trip(void);
void test_toml_emit_subroutines_round_trip(void);
void test_toml_emit_tiles_round_trip(void);
void test_toml_emit_atlas_sprite_round_trip(void);
void test_toml_emit_bindings_round_trip_defaults(void);
void test_toml_emit_bindings_round_trip_after_mutation(void);
void test_toml_load_bindings_missing_file_keeps_defaults(void);

/* sprite_offset_integration_test.c */
void test_integration_sprite_offset_edit_updates_entity_live(void);
void test_integration_sprite_offset_edit_emitted_to_toml(void);

/* collision_offset_integration_test.c */
void test_integration_collision_offset_edit_updates_entity_live(void);
void test_integration_collision_offset_edit_emitted_to_toml(void);

/* collision_composite_integration_test.c */
void test_integration_composite_collision_entity_uses_authored_shape(void);
void test_integration_composite_collision_narrower_than_one_rect_fallback(void);

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
void test_integration_nested_bind_for_each_repeat_order_and_scope(void);
void test_integration_subroutine_for_each_pool_switch_and_scope(void);
void test_integration_repeat_preserves_execution_order(void);
void test_integration_wait_delays_subsequent_actions(void);
void test_integration_wait_inside_if_else(void);
void test_integration_wait_inside_for_each(void);
void test_integration_two_entities_wait_independently(void);
void test_integration_wait_entity_destroyed_drops_continuation(void);
void test_integration_dialogue_blocks_until_closed(void);
void test_integration_dialogue_world_frozen(void);
void test_integration_dialogue_pages_and_typewriter(void);

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
    RUN_TEST(test_blueprint_animation_parsed);
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
    RUN_TEST(test_camera_pan_position_at_start_is_from);
    RUN_TEST(test_camera_pan_position_at_half_duration_is_midpoint);
    RUN_TEST(test_camera_pan_position_at_duration_is_target);
    RUN_TEST(test_camera_pan_position_clamps_past_duration);
    RUN_TEST(test_camera_shake_magnitude_full_at_zero_elapsed);
    RUN_TEST(test_camera_shake_magnitude_half_at_half_duration);
    RUN_TEST(test_camera_shake_magnitude_zero_at_and_past_duration);
    RUN_TEST(test_camera_shake_magnitude_never_negative);

    RUN_TEST(test_integration_load_gamedata);
    RUN_TEST(test_integration_load_specific_level);
    RUN_TEST(test_integration_walk_and_collide);
    RUN_TEST(test_integration_walk_freely);
    RUN_TEST(test_integration_boundary_all_directions);
    RUN_TEST(test_integration_wall_press_no_position_jump);
    RUN_TEST(test_integration_player_entity_spawns);
    RUN_TEST(test_integration_on_spawn_trigger_fires_on_load);
    RUN_TEST(test_integration_enter_trigger_fires_on_overlap);
    RUN_TEST(test_integration_enter_trigger_fires_only_once);
    RUN_TEST(test_integration_change_sprite_action_updates_source_rect);
    RUN_TEST(test_integration_play_sound_enqueues);
    RUN_TEST(test_integration_camera_pan_moves_target);
    RUN_TEST(test_integration_spawn_creates_entity);
    RUN_TEST(test_integration_spawn_inside_for_each_deferred);
    RUN_TEST(test_integration_real_gamedata_loads);
    RUN_TEST(test_integration_real_gamedata_all_levels_load);
    RUN_TEST(test_integration_transition_changes_level);
    RUN_TEST(test_integration_progression_survives_transition);
    RUN_TEST(test_integration_progression_survives_hot_reload);
    RUN_TEST(test_integration_progression_restore_clears_progression);
    RUN_TEST(test_integration_give_item_then_has_item);
    RUN_TEST(test_integration_give_item_shows_toast);
    RUN_TEST(test_integration_give_item_toast_fades_in_play_mode);
    RUN_TEST(test_integration_remove_item_at_zero);
    RUN_TEST(test_integration_item_survives_transition);
    RUN_TEST(test_integration_restore_clears_items);
    RUN_TEST(test_integration_editor_pan_does_not_reset_player_position);
    RUN_TEST(test_integration_editor_undo_at_left_edge_preserves_play_state);
    RUN_TEST(test_integration_editor_attr_edit_tap_decrements_by_one);
    RUN_TEST(test_integration_editor_attr_edit_hold_repeats_after_delay);
    RUN_TEST(test_integration_editor_selection_survives_deleting_different_entity);
    RUN_TEST(test_integration_editor_selection_survives_undo_of_edit);
    RUN_TEST(test_integration_editor_multiselect_group_move);
    RUN_TEST(test_integration_editor_copy_paste);
    RUN_TEST(test_integration_editor_watch_list_removes_focused_entry);
    RUN_TEST(test_integration_editor_radial_keyboard_nav);
    RUN_TEST(test_integration_editor_radial_handles_entry);
    RUN_TEST(test_integration_editor_radial_handles_requires_selection);
    RUN_TEST(test_integration_editor_level_switch_round_trip);
    RUN_TEST(test_integration_editor_level_create_round_trip);
    RUN_TEST(test_integration_editor_level_edit_detail_round_trip);
    RUN_TEST(test_integration_editor_tile_paint_round_trip);
    RUN_TEST(test_integration_editor_atlas_region_create_round_trip);
    RUN_TEST(test_integration_editor_animation_edit_round_trip);
    RUN_TEST(test_integration_editor_rule_tree_navigation);
    RUN_TEST(test_integration_editor_rule_leaf_edit_round_trip);
    RUN_TEST(test_integration_editor_rule_structural_edit);
    RUN_TEST(test_integration_editor_subroutine_edit_round_trip);
    RUN_TEST(test_integration_menu_navigation_and_quit);
    RUN_TEST(test_integration_menu_escape_returns_resume);
    RUN_TEST(test_integration_menu_gamepad_navigation);
    RUN_TEST(test_integration_settings_tab_switch);
    RUN_TEST(test_integration_settings_path_edit_commit);
    RUN_TEST(test_integration_settings_path_edit_parent_lists_contents);
    RUN_TEST(test_integration_settings_path_edit_drive_select_round_trip);
    RUN_TEST(test_integration_settings_path_edit_commit_normalizes_separators);
    RUN_TEST(test_integration_settings_path_edit_buffer_row_enters_keyboard_mode);

    RUN_TEST(test_level_load_first);
    RUN_TEST(test_level_load_by_name);
    RUN_TEST(test_level_load_nonexistent);
    RUN_TEST(test_level_entity_positions);
    RUN_TEST(test_level_entity_source_rects);
    RUN_TEST(test_level_child_entities_instantiated);
    RUN_TEST(test_level_child_entity_positions);
    RUN_TEST(test_level_child_entity_tags);
    RUN_TEST(test_level_nested_children);
    RUN_TEST(test_level_tile_dims_ceil_division);
    RUN_TEST(test_level_tile_index_row_major);

    RUN_TEST(test_render_split_camera_target_separates_integer_and_fraction);
    RUN_TEST(test_render_split_camera_target_exact_integer_has_zero_fraction);
    RUN_TEST(test_render_upscale_dest_rect_matches_formula);
    RUN_TEST(test_render_world_to_screen_position_continuous_sweeping_x_at_wall);
    RUN_TEST(test_render_world_to_screen_position_continuous_sweeping_y_at_wall);

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
    RUN_TEST(test_integration_nested_bind_for_each_repeat_order_and_scope);
    RUN_TEST(test_integration_subroutine_for_each_pool_switch_and_scope);
    RUN_TEST(test_integration_repeat_preserves_execution_order);
    RUN_TEST(test_integration_wait_delays_subsequent_actions);
    RUN_TEST(test_integration_wait_inside_if_else);
    RUN_TEST(test_integration_wait_inside_for_each);
    RUN_TEST(test_integration_two_entities_wait_independently);
    RUN_TEST(test_integration_wait_entity_destroyed_drops_continuation);
    RUN_TEST(test_integration_dialogue_blocks_until_closed);
    RUN_TEST(test_integration_dialogue_world_frozen);
    RUN_TEST(test_integration_dialogue_pages_and_typewriter);

    RUN_TEST(test_integration_sprite_offset_edit_updates_entity_live);
    RUN_TEST(test_integration_sprite_offset_edit_emitted_to_toml);

    RUN_TEST(test_integration_collision_offset_edit_updates_entity_live);
    RUN_TEST(test_integration_collision_offset_edit_emitted_to_toml);

    RUN_TEST(test_integration_composite_collision_entity_uses_authored_shape);
    RUN_TEST(test_integration_composite_collision_narrower_than_one_rect_fallback);

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
    RUN_TEST(test_toml_emit_animation_round_trip);
    RUN_TEST(test_toml_emit_persisted_attrs);
    RUN_TEST(test_toml_emit_no_persisted_attrs);
    RUN_TEST(test_toml_emit_child_persisted_attrs_round_trip);
    RUN_TEST(test_toml_emit_rules);
    RUN_TEST(test_toml_emit_camera_pan_shake_round_trip);
    RUN_TEST(test_toml_emit_spawn_round_trip);
    RUN_TEST(test_toml_emit_collision_composite_round_trip);
    RUN_TEST(test_toml_emit_nested_control_flow_round_trip);
    RUN_TEST(test_toml_emit_subroutines_round_trip);
    RUN_TEST(test_toml_emit_tiles_round_trip);
    RUN_TEST(test_toml_emit_atlas_sprite_round_trip);
    RUN_TEST(test_toml_emit_bindings_round_trip_defaults);
    RUN_TEST(test_toml_emit_bindings_round_trip_after_mutation);
    RUN_TEST(test_toml_load_bindings_missing_file_keeps_defaults);

    return UNITY_END();
}

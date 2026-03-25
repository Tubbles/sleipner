#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

void setUp(void) {}
void tearDown(void) {}

/* test_stub.c */
void test_stub_passes(void);

/* test_map.c */
void test_map_initial_state_count_is_zero(void);
void test_map_initial_state_capacity_is_zero(void);
void test_map_initial_state_entries_is_null(void);
void test_map_set_and_get_present(void);
void test_map_get_absent_returns_null(void);
void test_map_get_on_empty_returns_null(void);
void test_map_update_existing_replaces_value(void);
void test_map_update_count_unchanged(void);
void test_map_remove_present(void);
void test_map_remove_decrements_count(void);
void test_map_remove_absent_returns_false(void);
void test_map_remove_already_removed_returns_false(void);
void test_map_growth_all_entries_retrievable(void);
void test_map_growth_capacity_is_power_of_two(void);
void test_map_collision_probe_chain(void);
void test_map_tombstone_get_finds_later_entry(void);
void test_map_tombstone_reused_on_insert(void);
void test_map_free_resets_to_zero(void);
void test_map_free_safe_to_reuse(void);
void test_map_free_on_empty_is_safe(void);
void test_map_int_float_set_and_get(void);
void test_map_int_bool_set_and_get(void);
void test_map_int_i64_set_and_get(void);
void test_map_get_returns_pointer_to_stored_slot(void);
void test_map_arena_allocator(void);

/* test_vec.c */
void test_vec_initial_state_count_is_zero(void);
void test_vec_initial_state_capacity_is_zero(void);
void test_vec_initial_state_data_is_null(void);
void test_vec_push_increments_count(void);
void test_vec_push_single_value_readable(void);
void test_vec_push_multiple_values_in_order(void);
void test_vec_push_returns_true_on_success(void);
void test_vec_first_push_allocates_data(void);
void test_vec_first_push_sets_initial_capacity(void);
void test_vec_push_up_to_capacity_no_extra_alloc(void);
void test_vec_push_one_past_capacity_triggers_growth(void);
void test_vec_growth_preserves_existing_values(void);
void test_vec_multiple_growths_all_values_intact(void);
void test_vec_clear_resets_count_to_zero(void);
void test_vec_clear_preserves_allocation(void);
void test_vec_clear_allows_repush(void);
void test_vec_clear_on_empty_is_safe(void);
void test_vec_free_resets_count_to_zero(void);
void test_vec_free_resets_capacity_to_zero(void);
void test_vec_free_resets_data_to_null(void);
void test_vec_free_on_empty_is_safe(void);
void test_vec_free_allows_repush(void);
void test_vec_data_is_contiguous_in_memory(void);
void test_vec_float_push_and_read(void);
void test_vec_bool_push_and_read(void);
void test_vec_i64_push_and_read(void);
void test_vec_u32_push_and_read(void);
void test_vec_struct_push_and_read(void);
void test_vec_struct_growth_preserves_fields(void);

/* test_attribute.c */
void test_attr_set_and_get_float(void);
void test_attr_set_and_get_int(void);
void test_attr_set_and_get_bool(void);
void test_attr_set_and_get_string(void);
void test_attr_overwrite_existing(void);
void test_attr_get_missing_returns_fallback(void);
void test_attr_push_many_entries(void);
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
void test_arena_save_restore_basic(void);
void test_arena_save_restore_nested(void);
void test_arena_save_at_zero(void);
void test_arena_realloc_null_ptr_acts_as_alloc(void);
void test_arena_realloc_shrink_returns_same_ptr(void);
void test_arena_realloc_in_place_when_at_top(void);
void test_arena_realloc_copies_when_not_at_top(void);

/* test_alloc.c */
void test_alloc_heap_malloc_and_free(void);
void test_alloc_heap_realloc(void);
void test_alloc_arena_malloc(void);
void test_alloc_arena_free_is_noop(void);
void test_alloc_arena_realloc_in_place(void);

/* test_blueprint.c */
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

/* test_error.c */
void test_error_initially_null(void);
void test_error_set_and_get(void);
void test_error_set_with_format(void);
void test_error_wrap_prepends_context(void);
void test_error_wrap_on_empty_is_noop(void);
void test_error_clear_resets(void);
void test_error_set_overwrites_previous(void);

/* test_entity.c */
void test_entity_init_from_blueprint(void);
void test_entity_get_attr_from_blueprint(void);
void test_entity_instance_overrides_blueprint(void);
void test_entity_get_missing_attr(void);
void test_entity_int_float_coercion(void);
void test_entity_no_blueprint(void);
void test_entity_solid_from_collision(void);
void test_entity_find_by_tag_self(void);
void test_entity_find_by_tag_parent(void);
void test_entity_find_by_tag_parent_of_root(void);
void test_entity_find_by_tag_root(void);
void test_entity_find_by_tag_custom(void);
void test_entity_find_by_tag_not_found(void);
void test_entity_is_visible_standalone(void);
void test_entity_is_visible_parent_hidden(void);
void test_entity_is_visible_both_visible(void);
void test_entity_is_active_parent_inactive(void);
void test_entity_is_active_both_active(void);

/* test_game.c */
void test_game_init_defaults(void);
void test_game_update_increments_frame(void);
void test_game_update_accumulates_elapsed(void);
void test_game_update_player_moves_right(void);
void test_game_update_player_moves_left(void);
void test_game_update_no_input_no_movement(void);
void test_game_player_clamps_to_bounds(void);
void test_game_player_collision_from_blueprint(void);
void test_game_update_resolves_obstacle_collision(void);

/* test_level.c */
void test_level_load_first(void);
void test_level_load_by_name(void);
void test_level_load_nonexistent(void);
void test_level_entity_positions(void);
void test_level_entity_source_rects(void);
void test_level_child_entities_instantiated(void);
void test_level_child_entity_positions(void);
void test_level_child_entity_tags(void);
void test_level_nested_children(void);

/* test_integration.c */
void test_integration_load_gamedata(void);
void test_integration_load_specific_level(void);
void test_integration_walk_and_collide(void);
void test_integration_walk_freely(void);
void test_integration_boundary_all_directions(void);
void test_integration_player_entity_spawns(void);

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

/* test_str.c */
void test_str_from_cstr_len(void);
void test_str_from_cstr_content(void);
void test_str_from_cstr_null_terminated(void);
void test_str_from_cstr_empty_nonnull_ptr(void);
void test_str_from_strv_len(void);
void test_str_from_strv_content(void);
void test_str_from_strv_subview(void);
void test_str_to_strv_len_excludes_null(void);
void test_str_to_strv_ptr_matches(void);
void test_str_push_char_len(void);
void test_str_push_char_content(void);
void test_str_push_char_null_terminated(void);
void test_str_append_cstr_content(void);
void test_str_append_strv_content(void);
void test_str_append_strv_subview(void);
void test_str_append_grows_beyond_initial_cap(void);
void test_str_clear_resets_len(void);
void test_str_clear_keeps_allocation(void);
void test_str_clear_null_terminates(void);
void test_str_free_zeros_struct(void);
void test_str_free_on_zero_is_safe(void);

/* test_strv.c */
void test_strv_from_cstr(void);
void test_strv_shrink_left(void);
void test_strv_shrink_left_clamp(void);
void test_strv_shrink_right(void);
void test_strv_shrink_right_clamp(void);
void test_strv_trim_left(void);
void test_strv_trim_right(void);
void test_strv_trim(void);
void test_strv_trim_all_whitespace(void);
void test_strv_split_found(void);
void test_strv_split_not_found(void);
void test_strv_split_delim_first(void);
void test_strv_split_delim_last(void);
void test_strv_split_multi(void);
void test_strv_eq_equal(void);
void test_strv_eq_different_content(void);
void test_strv_eq_different_length(void);
void test_strv_eq_subview(void);
void test_strv_eq_cstr_match(void);
void test_strv_eq_cstr_no_match(void);
void test_strv_eq_cstr_subview(void);
void test_strv_starts_with_cstr_match(void);
void test_strv_starts_with_cstr_no_match(void);
void test_strv_starts_with_cstr_exact(void);
void test_strv_starts_with_cstr_longer_prefix(void);
void test_strv_copy_to_cstr(void);
void test_strv_copy_to_cstr_truncates(void);
void test_strv_copy_to_cstr_subview(void);

/* test_toml_str.c */
void test_toml_str_content_is_copied(void);
void test_toml_str_datum_nulled_after_call(void);

/* test_toml_emitter.c */
void test_toml_emit_blueprints(void);
void test_toml_emit_level_with_entities(void);
void test_toml_emit_round_trip(void);
void test_toml_emit_buffer_too_small(void);
void test_toml_emit_no_music(void);
void test_toml_emit_blueprint_children(void);
void test_toml_emit_skips_child_entities(void);

/* test_rule.c */
void test_flag_set_and_get(void);
void test_flag_clear(void);
void test_flag_unset_returns_false(void);
void test_flag_set_idempotent(void);
void test_flag_clear_nonexistent(void);
void test_trigger_parse_interact(void);
void test_trigger_parse_enter(void);
void test_trigger_parse_on_spawn(void);
void test_trigger_parse_event(void);
void test_trigger_parse_attr_changed(void);
void test_trigger_parse_unknown(void);
void test_condition_parse_flag(void);
void test_condition_parse_not_flag(void);
void test_condition_parse_attr_truthy(void);
void test_condition_parse_attr_short_form(void);
void test_condition_parse_not_attr(void);
void test_condition_parse_attr_less_than(void);
void test_condition_parse_attr_greater_than(void);
void test_condition_parse_attr_equals(void);
void test_condition_parse_has_item(void);
void test_condition_parse_unknown(void);
void test_action_parse_set_flag(void);
void test_action_parse_clear_flag(void);
void test_action_parse_set_attr(void);
void test_action_parse_add_attr(void);
void test_action_parse_toggle_attr(void);
void test_action_parse_destroy(void);
void test_action_parse_fire_event(void);
void test_action_parse_unknown(void);
void test_trigger_matches_simple(void);
void test_trigger_no_match_different_type(void);
void test_trigger_matches_event_with_argument(void);
void test_trigger_no_match_event_wrong_argument(void);
void test_condition_flag_true(void);
void test_condition_flag_false(void);
void test_condition_not_flag(void);
void test_condition_attr_truthy(void);
void test_condition_attr_falsy(void);
void test_condition_attr_missing(void);
void test_condition_attr_less_than(void);
void test_condition_attr_greater_than(void);
void test_condition_and_logic_all_pass(void);
void test_condition_and_logic_one_fails(void);
void test_action_set_flag_executes(void);
void test_action_clear_flag_executes(void);
void test_action_set_attr_bool(void);
void test_action_set_attr_int(void);
void test_action_add_attr(void);
void test_action_toggle_attr(void);
void test_action_destroy(void);
void test_action_fire_event_queues(void);
void test_action_execution_order(void);
void test_rules_parse_from_toml(void);
void test_rules_parse_no_rules(void);
void test_rules_parse_multiple_rules(void);
void test_evaluate_interact_sets_flag(void);
void test_evaluate_condition_blocks_action(void);
void test_evaluate_fire_event_cascading(void);
void test_integration_interact_rule(void);
void test_integration_condition_blocks_interact(void);
void test_var_set_local(void);
void test_var_set_global(void);
void test_var_condition_truthy(void);
void test_var_condition_falsy_when_unset(void);
void test_var_substitution_in_set_attr(void);
void test_local_var_scoped_per_rule(void);

/* test_touch.c */
void test_touch_initial_state_is_none(void);
void test_touch_first_touch_outside_button_sets_stick_mode(void);
void test_touch_first_touch_sets_origin(void);
void test_touch_continue_updates_current(void);
void test_touch_continue_preserves_origin(void);
void test_touch_release_resets_mode(void);
void test_touch_button_area_sets_button_mode(void);
void test_touch_button_triggers_on_press(void);
void test_touch_button_not_triggered_on_hold(void);
void test_touch_get_stick_no_touch_returns_zero(void);
void test_touch_get_stick_right_returns_positive_x(void);
void test_touch_get_stick_clamps_to_one(void);
void test_touch_not_triggered_outside_button(void);

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

    RUN_TEST(test_map_initial_state_count_is_zero);
    RUN_TEST(test_map_initial_state_capacity_is_zero);
    RUN_TEST(test_map_initial_state_entries_is_null);
    RUN_TEST(test_map_set_and_get_present);
    RUN_TEST(test_map_get_absent_returns_null);
    RUN_TEST(test_map_get_on_empty_returns_null);
    RUN_TEST(test_map_update_existing_replaces_value);
    RUN_TEST(test_map_update_count_unchanged);
    RUN_TEST(test_map_remove_present);
    RUN_TEST(test_map_remove_decrements_count);
    RUN_TEST(test_map_remove_absent_returns_false);
    RUN_TEST(test_map_remove_already_removed_returns_false);
    RUN_TEST(test_map_growth_all_entries_retrievable);
    RUN_TEST(test_map_growth_capacity_is_power_of_two);
    RUN_TEST(test_map_collision_probe_chain);
    RUN_TEST(test_map_tombstone_get_finds_later_entry);
    RUN_TEST(test_map_tombstone_reused_on_insert);
    RUN_TEST(test_map_free_resets_to_zero);
    RUN_TEST(test_map_free_safe_to_reuse);
    RUN_TEST(test_map_free_on_empty_is_safe);
    RUN_TEST(test_map_int_float_set_and_get);
    RUN_TEST(test_map_int_bool_set_and_get);
    RUN_TEST(test_map_int_i64_set_and_get);
    RUN_TEST(test_map_get_returns_pointer_to_stored_slot);
    RUN_TEST(test_map_arena_allocator);

    RUN_TEST(test_vec_initial_state_count_is_zero);
    RUN_TEST(test_vec_initial_state_capacity_is_zero);
    RUN_TEST(test_vec_initial_state_data_is_null);
    RUN_TEST(test_vec_push_increments_count);
    RUN_TEST(test_vec_push_single_value_readable);
    RUN_TEST(test_vec_push_multiple_values_in_order);
    RUN_TEST(test_vec_push_returns_true_on_success);
    RUN_TEST(test_vec_first_push_allocates_data);
    RUN_TEST(test_vec_first_push_sets_initial_capacity);
    RUN_TEST(test_vec_push_up_to_capacity_no_extra_alloc);
    RUN_TEST(test_vec_push_one_past_capacity_triggers_growth);
    RUN_TEST(test_vec_growth_preserves_existing_values);
    RUN_TEST(test_vec_multiple_growths_all_values_intact);
    RUN_TEST(test_vec_clear_resets_count_to_zero);
    RUN_TEST(test_vec_clear_preserves_allocation);
    RUN_TEST(test_vec_clear_allows_repush);
    RUN_TEST(test_vec_clear_on_empty_is_safe);
    RUN_TEST(test_vec_free_resets_count_to_zero);
    RUN_TEST(test_vec_free_resets_capacity_to_zero);
    RUN_TEST(test_vec_free_resets_data_to_null);
    RUN_TEST(test_vec_free_on_empty_is_safe);
    RUN_TEST(test_vec_free_allows_repush);
    RUN_TEST(test_vec_data_is_contiguous_in_memory);
    RUN_TEST(test_vec_float_push_and_read);
    RUN_TEST(test_vec_bool_push_and_read);
    RUN_TEST(test_vec_i64_push_and_read);
    RUN_TEST(test_vec_u32_push_and_read);
    RUN_TEST(test_vec_struct_push_and_read);
    RUN_TEST(test_vec_struct_growth_preserves_fields);

    RUN_TEST(test_attr_set_and_get_float);
    RUN_TEST(test_attr_set_and_get_int);
    RUN_TEST(test_attr_set_and_get_bool);
    RUN_TEST(test_attr_set_and_get_string);
    RUN_TEST(test_attr_overwrite_existing);
    RUN_TEST(test_attr_get_missing_returns_fallback);
    RUN_TEST(test_attr_push_many_entries);
    RUN_TEST(test_attr_type_change);
    RUN_TEST(test_attr_scoped_instance_overrides_blueprint);
    RUN_TEST(test_attr_multiple_types);

    RUN_TEST(test_arena_init_and_free);
    RUN_TEST(test_arena_alloc_basic);
    RUN_TEST(test_arena_alloc_alignment);
    RUN_TEST(test_arena_alloc_returns_null_when_full);
    RUN_TEST(test_arena_reset);
    RUN_TEST(test_arena_snapshot_restore);
    RUN_TEST(test_arena_save_restore_basic);
    RUN_TEST(test_arena_save_restore_nested);
    RUN_TEST(test_arena_save_at_zero);
    RUN_TEST(test_arena_realloc_null_ptr_acts_as_alloc);
    RUN_TEST(test_arena_realloc_shrink_returns_same_ptr);
    RUN_TEST(test_arena_realloc_in_place_when_at_top);
    RUN_TEST(test_arena_realloc_copies_when_not_at_top);

    RUN_TEST(test_alloc_heap_malloc_and_free);
    RUN_TEST(test_alloc_heap_realloc);
    RUN_TEST(test_alloc_arena_malloc);
    RUN_TEST(test_alloc_arena_free_is_noop);
    RUN_TEST(test_alloc_arena_realloc_in_place);

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

    RUN_TEST(test_error_initially_null);
    RUN_TEST(test_error_set_and_get);
    RUN_TEST(test_error_set_with_format);
    RUN_TEST(test_error_wrap_prepends_context);
    RUN_TEST(test_error_wrap_on_empty_is_noop);
    RUN_TEST(test_error_clear_resets);
    RUN_TEST(test_error_set_overwrites_previous);

    RUN_TEST(test_entity_init_from_blueprint);
    RUN_TEST(test_entity_get_attr_from_blueprint);
    RUN_TEST(test_entity_instance_overrides_blueprint);
    RUN_TEST(test_entity_get_missing_attr);
    RUN_TEST(test_entity_int_float_coercion);
    RUN_TEST(test_entity_no_blueprint);
    RUN_TEST(test_entity_solid_from_collision);
    RUN_TEST(test_entity_find_by_tag_self);
    RUN_TEST(test_entity_find_by_tag_parent);
    RUN_TEST(test_entity_find_by_tag_parent_of_root);
    RUN_TEST(test_entity_find_by_tag_root);
    RUN_TEST(test_entity_find_by_tag_custom);
    RUN_TEST(test_entity_find_by_tag_not_found);
    RUN_TEST(test_entity_is_visible_standalone);
    RUN_TEST(test_entity_is_visible_parent_hidden);
    RUN_TEST(test_entity_is_visible_both_visible);
    RUN_TEST(test_entity_is_active_parent_inactive);
    RUN_TEST(test_entity_is_active_both_active);

    RUN_TEST(test_game_init_defaults);
    RUN_TEST(test_game_update_increments_frame);
    RUN_TEST(test_game_update_accumulates_elapsed);
    RUN_TEST(test_game_update_player_moves_right);
    RUN_TEST(test_game_update_player_moves_left);
    RUN_TEST(test_game_update_no_input_no_movement);
    RUN_TEST(test_game_player_clamps_to_bounds);
    RUN_TEST(test_game_player_collision_from_blueprint);
    RUN_TEST(test_game_update_resolves_obstacle_collision);

    RUN_TEST(test_integration_load_gamedata);
    RUN_TEST(test_integration_load_specific_level);
    RUN_TEST(test_integration_walk_and_collide);
    RUN_TEST(test_integration_walk_freely);
    RUN_TEST(test_integration_boundary_all_directions);
    RUN_TEST(test_integration_player_entity_spawns);

    RUN_TEST(test_level_load_first);
    RUN_TEST(test_level_load_by_name);
    RUN_TEST(test_level_load_nonexistent);
    RUN_TEST(test_level_entity_positions);
    RUN_TEST(test_level_entity_source_rects);
    RUN_TEST(test_level_child_entities_instantiated);
    RUN_TEST(test_level_child_entity_positions);
    RUN_TEST(test_level_child_entity_tags);
    RUN_TEST(test_level_nested_children);

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

    RUN_TEST(test_flag_set_and_get);
    RUN_TEST(test_flag_clear);
    RUN_TEST(test_flag_unset_returns_false);
    RUN_TEST(test_flag_set_idempotent);
    RUN_TEST(test_flag_clear_nonexistent);
    RUN_TEST(test_trigger_parse_interact);
    RUN_TEST(test_trigger_parse_enter);
    RUN_TEST(test_trigger_parse_on_spawn);
    RUN_TEST(test_trigger_parse_event);
    RUN_TEST(test_trigger_parse_attr_changed);
    RUN_TEST(test_trigger_parse_unknown);
    RUN_TEST(test_condition_parse_flag);
    RUN_TEST(test_condition_parse_not_flag);
    RUN_TEST(test_condition_parse_attr_truthy);
    RUN_TEST(test_condition_parse_attr_short_form);
    RUN_TEST(test_condition_parse_not_attr);
    RUN_TEST(test_condition_parse_attr_less_than);
    RUN_TEST(test_condition_parse_attr_greater_than);
    RUN_TEST(test_condition_parse_attr_equals);
    RUN_TEST(test_condition_parse_has_item);
    RUN_TEST(test_condition_parse_unknown);
    RUN_TEST(test_action_parse_set_flag);
    RUN_TEST(test_action_parse_clear_flag);
    RUN_TEST(test_action_parse_set_attr);
    RUN_TEST(test_action_parse_add_attr);
    RUN_TEST(test_action_parse_toggle_attr);
    RUN_TEST(test_action_parse_destroy);
    RUN_TEST(test_action_parse_fire_event);
    RUN_TEST(test_action_parse_unknown);
    RUN_TEST(test_trigger_matches_simple);
    RUN_TEST(test_trigger_no_match_different_type);
    RUN_TEST(test_trigger_matches_event_with_argument);
    RUN_TEST(test_trigger_no_match_event_wrong_argument);
    RUN_TEST(test_condition_flag_true);
    RUN_TEST(test_condition_flag_false);
    RUN_TEST(test_condition_not_flag);
    RUN_TEST(test_condition_attr_truthy);
    RUN_TEST(test_condition_attr_falsy);
    RUN_TEST(test_condition_attr_missing);
    RUN_TEST(test_condition_attr_less_than);
    RUN_TEST(test_condition_attr_greater_than);
    RUN_TEST(test_condition_and_logic_all_pass);
    RUN_TEST(test_condition_and_logic_one_fails);
    RUN_TEST(test_action_set_flag_executes);
    RUN_TEST(test_action_clear_flag_executes);
    RUN_TEST(test_action_set_attr_bool);
    RUN_TEST(test_action_set_attr_int);
    RUN_TEST(test_action_add_attr);
    RUN_TEST(test_action_toggle_attr);
    RUN_TEST(test_action_destroy);
    RUN_TEST(test_action_fire_event_queues);
    RUN_TEST(test_action_execution_order);
    RUN_TEST(test_rules_parse_from_toml);
    RUN_TEST(test_rules_parse_no_rules);
    RUN_TEST(test_rules_parse_multiple_rules);
    RUN_TEST(test_evaluate_interact_sets_flag);
    RUN_TEST(test_evaluate_condition_blocks_action);
    RUN_TEST(test_evaluate_fire_event_cascading);
    RUN_TEST(test_integration_interact_rule);
    RUN_TEST(test_integration_condition_blocks_interact);
    RUN_TEST(test_var_set_local);
    RUN_TEST(test_var_set_global);
    RUN_TEST(test_var_condition_truthy);
    RUN_TEST(test_var_condition_falsy_when_unset);
    RUN_TEST(test_var_substitution_in_set_attr);
    RUN_TEST(test_local_var_scoped_per_rule);

    RUN_TEST(test_touch_initial_state_is_none);
    RUN_TEST(test_touch_first_touch_outside_button_sets_stick_mode);
    RUN_TEST(test_touch_first_touch_sets_origin);
    RUN_TEST(test_touch_continue_updates_current);
    RUN_TEST(test_touch_continue_preserves_origin);
    RUN_TEST(test_touch_release_resets_mode);
    RUN_TEST(test_touch_button_area_sets_button_mode);
    RUN_TEST(test_touch_button_triggers_on_press);
    RUN_TEST(test_touch_button_not_triggered_on_hold);
    RUN_TEST(test_touch_get_stick_no_touch_returns_zero);
    RUN_TEST(test_touch_get_stick_right_returns_positive_x);
    RUN_TEST(test_touch_get_stick_clamps_to_one);
    RUN_TEST(test_touch_not_triggered_outside_button);

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

    RUN_TEST(test_str_from_cstr_len);
    RUN_TEST(test_str_from_cstr_content);
    RUN_TEST(test_str_from_cstr_null_terminated);
    RUN_TEST(test_str_from_cstr_empty_nonnull_ptr);
    RUN_TEST(test_str_from_strv_len);
    RUN_TEST(test_str_from_strv_content);
    RUN_TEST(test_str_from_strv_subview);
    RUN_TEST(test_str_to_strv_len_excludes_null);
    RUN_TEST(test_str_to_strv_ptr_matches);
    RUN_TEST(test_str_push_char_len);
    RUN_TEST(test_str_push_char_content);
    RUN_TEST(test_str_push_char_null_terminated);
    RUN_TEST(test_str_append_cstr_content);
    RUN_TEST(test_str_append_strv_content);
    RUN_TEST(test_str_append_strv_subview);
    RUN_TEST(test_str_append_grows_beyond_initial_cap);
    RUN_TEST(test_str_clear_resets_len);
    RUN_TEST(test_str_clear_keeps_allocation);
    RUN_TEST(test_str_clear_null_terminates);
    RUN_TEST(test_str_free_zeros_struct);
    RUN_TEST(test_str_free_on_zero_is_safe);

    RUN_TEST(test_strv_from_cstr);
    RUN_TEST(test_strv_shrink_left);
    RUN_TEST(test_strv_shrink_left_clamp);
    RUN_TEST(test_strv_shrink_right);
    RUN_TEST(test_strv_shrink_right_clamp);
    RUN_TEST(test_strv_trim_left);
    RUN_TEST(test_strv_trim_right);
    RUN_TEST(test_strv_trim);
    RUN_TEST(test_strv_trim_all_whitespace);
    RUN_TEST(test_strv_split_found);
    RUN_TEST(test_strv_split_not_found);
    RUN_TEST(test_strv_split_delim_first);
    RUN_TEST(test_strv_split_delim_last);
    RUN_TEST(test_strv_split_multi);
    RUN_TEST(test_strv_eq_equal);
    RUN_TEST(test_strv_eq_different_content);
    RUN_TEST(test_strv_eq_different_length);
    RUN_TEST(test_strv_eq_subview);
    RUN_TEST(test_strv_eq_cstr_match);
    RUN_TEST(test_strv_eq_cstr_no_match);
    RUN_TEST(test_strv_eq_cstr_subview);
    RUN_TEST(test_strv_starts_with_cstr_match);
    RUN_TEST(test_strv_starts_with_cstr_no_match);
    RUN_TEST(test_strv_starts_with_cstr_exact);
    RUN_TEST(test_strv_starts_with_cstr_longer_prefix);
    RUN_TEST(test_strv_copy_to_cstr);
    RUN_TEST(test_strv_copy_to_cstr_truncates);
    RUN_TEST(test_strv_copy_to_cstr_subview);

    RUN_TEST(test_toml_str_content_is_copied);
    RUN_TEST(test_toml_str_datum_nulled_after_call);

    RUN_TEST(test_toml_emit_blueprints);
    RUN_TEST(test_toml_emit_level_with_entities);
    RUN_TEST(test_toml_emit_round_trip);
    RUN_TEST(test_toml_emit_buffer_too_small);
    RUN_TEST(test_toml_emit_no_music);
    RUN_TEST(test_toml_emit_blueprint_children);
    RUN_TEST(test_toml_emit_skips_child_entities);

    return UNITY_END();
}

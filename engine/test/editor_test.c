/* fff.h must come first — its macros must be visible before editor.c is preprocessed. */
#include "fff.h"

/* Redirect raylib input functions to fff.h fakes before compiling editor.c.
 * Draw/utility functions are NOT redirected — they still resolve to real raylib at link
 * time (tests never call draw functions). */
// NOLINTBEGIN(readability-identifier-naming,readability-else-after-return)
#define IsKeyPressed IsKeyPressed_mock                     // NOLINT
#define IsKeyDown IsKeyDown_mock                           // NOLINT
#define IsGamepadButtonPressed IsGamepadButtonPressed_mock // NOLINT
#define IsGamepadButtonDown IsGamepadButtonDown_mock       // NOLINT

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(bool, IsKeyPressed_mock, int);
FAKE_VALUE_FUNC(bool, IsKeyDown_mock, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed_mock, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown_mock, int, int);
// NOLINTEND(readability-identifier-naming,readability-else-after-return)

/* Include the implementation file to access static functions directly. */
#include "../src/editor.c" // NOLINT(bugprone-suspicious-include)

#include "unity.h"

#include "test_helpers.h"

#include <string.h>

/* ---- Helpers ------------------------------------------------------------ */

static Blueprint make_named_blueprint(const char *name)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", name}));
    return blueprint;
}

/* ---- radial_sector_from_stick ------------------------------------------- */

void test_editor_radial_dead_zone_returns_negative_one(void)
{
    Vector2 tiny_stick = {0.1F, 0.1F};
    TEST_ASSERT_EQUAL_INT(-1, radial_sector_from_stick(tiny_stick, 4));
}

void test_editor_radial_stick_up_four_items(void)
{
    Vector2 stick_up = {0.0F, -1.0F};
    TEST_ASSERT_EQUAL_INT(0, radial_sector_from_stick(stick_up, 4));
}

void test_editor_radial_stick_right_four_items(void)
{
    Vector2 right = {1.0F, 0.0F};
    TEST_ASSERT_EQUAL_INT(1, radial_sector_from_stick(right, 4));
}

void test_editor_radial_stick_down_four_items(void)
{
    /* Slightly west of south to avoid exact sector boundary at 180 degrees */
    Vector2 down = {-0.1F, 1.0F};
    TEST_ASSERT_EQUAL_INT(2, radial_sector_from_stick(down, 4));
}

void test_editor_radial_stick_left_four_items(void)
{
    Vector2 left = {-1.0F, 0.0F};
    TEST_ASSERT_EQUAL_INT(3, radial_sector_from_stick(left, 4));
}

/* ---- radial_label ------------------------------------------------------- */

void test_editor_radial_label_grab(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("Grab", radial_label(&editor_state, 0));
}

void test_editor_radial_label_delete(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("Delete", radial_label(&editor_state, 3));
}

void test_editor_radial_label_out_of_bounds(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("", radial_label(&editor_state, 4));
}

/* ---- word_builder_append ------------------------------------------------ */

void test_editor_word_builder_append_to_empty(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    TEST_ASSERT_EQUAL_STRING("chest", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(5, editor_state.word_builder_len);
}

void test_editor_word_builder_append_with_underscore(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_append(&editor_state, "locked");
    TEST_ASSERT_EQUAL_STRING("chest_locked", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(12, editor_state.word_builder_len);
}

void test_editor_word_builder_append_overflow_noop(void)
{
    EditorState editor_state = {0};
    memset(editor_state.word_builder_buf, 'a', WORD_BUILDER_BUF_SIZE - 2);
    editor_state.word_builder_buf[WORD_BUILDER_BUF_SIZE - 2] = '\0';
    editor_state.word_builder_len = WORD_BUILDER_BUF_SIZE - 2;

    word_builder_append(&editor_state, "x");

    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_BUF_SIZE - 2, editor_state.word_builder_len);
}

void test_editor_word_builder_append_multiple(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "fire");
    word_builder_append(&editor_state, "ice");
    word_builder_append(&editor_state, "water");
    TEST_ASSERT_EQUAL_STRING("fire_ice_water", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(14, editor_state.word_builder_len);
}

/* ---- word_builder_pop --------------------------------------------------- */

void test_editor_word_builder_pop_last_word(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_append(&editor_state, "locked");
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("chest", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(5, editor_state.word_builder_len);
}

void test_editor_word_builder_pop_single_word(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_len);
}

void test_editor_word_builder_pop_empty_noop(void)
{
    EditorState editor_state = {0};
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_len);
}

/* ---- word_builder_total_count ------------------------------------------- */

void test_editor_word_builder_total_count_no_blueprints(void)
{
    GameState state = {0};
    int total = word_builder_total_count(&state);
    TEST_ASSERT_EQUAL_INT(1 + WORD_BUILDER_BUILTIN_COUNT, total);
}

void test_editor_word_builder_total_count_with_blueprints(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;
    int base = word_builder_total_count(&state);

    Blueprint blueprint = make_named_blueprint("player");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.blueprints.entries, blueprint));

    TEST_ASSERT_EQUAL_INT(base + 1, word_builder_total_count(&state));

    test_blueprint_table_free(&state.blueprints);
}

/* ---- word_builder_item -------------------------------------------------- */

void test_editor_word_builder_item_zero_is_done(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("[ DONE ]", word_builder_item(&state, 0));
}

void test_editor_word_builder_item_first_builtin(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("chest", word_builder_item(&state, 1));
}

void test_editor_word_builder_item_blueprint_name(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("player");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.blueprints.entries, blueprint));

    int blueprint_index = 1 + WORD_BUILDER_BUILTIN_COUNT;
    TEST_ASSERT_EQUAL_STRING("player", word_builder_item(&state, blueprint_index));

    test_blueprint_table_free(&state.blueprints);
}

void test_editor_word_builder_item_negative_index(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("[ DONE ]", word_builder_item(&state, -1));
}

/* ---- place_visible_count ------------------------------------------------ */

void test_editor_place_visible_count_known_height(void)
{
    int screen_height = 480;
    int expected = (screen_height - HINTS_BAR_HEIGHT - EDITOR_PANEL_LINE_HEIGHT) / EDITOR_PANEL_LINE_HEIGHT;
    TEST_ASSERT_EQUAL_INT(expected, place_visible_count(screen_height));
}

void test_editor_place_visible_count_small_height(void)
{
    int result = place_visible_count(HINTS_BAR_HEIGHT + EDITOR_PANEL_LINE_HEIGHT);
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* ---- total_attr_count --------------------------------------------------- */

void test_editor_total_attr_count_instance_only(void)
{
    GameState state = {0};
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5));

    TEST_ASSERT_EQUAL_INT(2, total_attr_count(&state, &entity));

    test_attr_set_free(&entity.attrs);
}

void test_editor_total_attr_count_with_blueprint(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "bp_attr", 42));
    (void)vec_blueprint_push(&state.blueprints.entries, blueprint);

    Entity entity = {0};
    entity.id = 1;
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_attr", 10));
    Str bp_name = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &bp_name, "test_bp"));
    (void)map_int_str_set(&state.entity_blueprints, entity.id, bp_name, &test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(2 + 1, total_attr_count(&state, &entity));

    test_attr_set_free(&entity.attrs);
    test_attr_set_free(&blueprint.attrs);
    str_free(&test_heap_alloc, &entity.blueprint_name);
    str_free(&test_heap_alloc, &bp_name);
    map_int_str_free(&state.entity_blueprints, &test_heap_alloc);
    vec_blueprint_free(&state.blueprints.entries);
}

/* ---- is_blueprint_attr -------------------------------------------------- */

void test_editor_is_blueprint_attr_false_for_instance(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    TEST_ASSERT_FALSE(is_blueprint_attr(&entity, 0));

    test_attr_set_free(&entity.attrs);
}

void test_editor_is_blueprint_attr_true_for_blueprint(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    TEST_ASSERT_TRUE(is_blueprint_attr(&entity, 1));

    test_attr_set_free(&entity.attrs);
}

/* ---- find_nearest_entity ------------------------------------------------ */

void test_editor_find_nearest_single_entity(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity entity = {.parent_index = -1, .position = {10.0F, 10.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, entity));

    TEST_ASSERT_EQUAL_INT(0, find_nearest_entity(&level, (Vector2){15.0F, 15.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_closer_wins(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity far_entity = {.parent_index = -1, .position = {100.0F, 100.0F}};
    Entity near_entity = {.parent_index = -1, .position = {10.0F, 10.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, far_entity));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, near_entity));

    TEST_ASSERT_EQUAL_INT(1, find_nearest_entity(&level, (Vector2){12.0F, 12.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_skips_children(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity child = {.parent_index = 0, .position = {5.0F, 5.0F}};
    Entity root = {.parent_index = -1, .position = {100.0F, 100.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root));

    TEST_ASSERT_EQUAL_INT(1, find_nearest_entity(&level, (Vector2){0.0F, 0.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_empty_level(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    TEST_ASSERT_EQUAL_INT(-1, find_nearest_entity(&level, (Vector2){0.0F, 0.0F}));
}

/* ---- entity_outline_rect ------------------------------------------------ */

void test_editor_entity_outline_rect_with_collision(void)
{
    GameState state = {0};
    Entity entity = {0};
    entity.collision = (Rectangle){10.0F, 20.0F, 32.0F, 16.0F};

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 20.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.height);
}

void test_editor_entity_outline_rect_without_collision(void)
{
    GameState state = {0};
    Entity entity = {0};
    entity.position = (Vector2){50.0F, 60.0F};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "src_w", 16.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "src_h", 24.0F));

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 60.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 24.0F, rect.height);

    test_attr_set_free(&entity.attrs);
}

/* ---- find_blueprint_by_name --------------------------------------------- */

void test_editor_find_blueprint_by_name_found(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("chest");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.blueprints.entries, blueprint));

    Blueprint *result = find_blueprint_by_name(&state, "chest");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("chest", attr_get_string(&result->attrs, "name"));

    test_blueprint_table_free(&state.blueprints);
}

void test_editor_find_blueprint_by_name_not_found(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("chest");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.blueprints.entries, blueprint));

    TEST_ASSERT_NULL(find_blueprint_by_name(&state, "nonexistent"));

    test_blueprint_table_free(&state.blueprints);
}

void test_editor_find_blueprint_by_name_empty_table(void)
{
    GameState state = {0};
    TEST_ASSERT_NULL(find_blueprint_by_name(&state, "anything"));
}

/* ---- mark_deleted_descendants ------------------------------------------- */

void test_editor_mark_deleted_root_marks_child(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity root = {.parent_index = -1};
    Entity child = {.parent_index = 0};
    Entity other = {.parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, other));

    bool is_deleted[] = {true, false, false};
    mark_deleted_descendants(&level, is_deleted, 3);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_FALSE(is_deleted[2]);

    vec_entity_free(&level.entities);
}

void test_editor_mark_deleted_chain(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity grandparent = {.parent_index = -1};
    Entity parent_entity = {.parent_index = 0};
    Entity grandchild = {.parent_index = 1};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, grandparent));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, parent_entity));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, grandchild));

    bool is_deleted[] = {true, false, false};
    mark_deleted_descendants(&level, is_deleted, 3);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_TRUE(is_deleted[2]);

    vec_entity_free(&level.entities);
}

void test_editor_mark_deleted_sibling_untouched(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity root_a = {.parent_index = -1};
    Entity child_a = {.parent_index = 0};
    Entity root_b = {.parent_index = -1};
    Entity child_b = {.parent_index = 2};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root_a));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child_a));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root_b));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child_b));

    bool is_deleted[] = {true, false, false, false};
    mark_deleted_descendants(&level, is_deleted, 4);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_FALSE(is_deleted[2]);
    TEST_ASSERT_FALSE(is_deleted[3]);

    vec_entity_free(&level.entities);
}

/* ---- apply_attr_delta --------------------------------------------------- */

void test_editor_apply_attr_delta_int(void)
{
    GameState state = {0};
    state.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(vec_entity_push(&state.current_level.entities, entity));

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 5);

    Attribute *attr = &state.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_EQUAL_INT(15, attr->value.i);

    test_attr_set_free(&state.current_level.entities.data[0].attrs);
    vec_entity_free(&state.current_level.entities);
}

void test_editor_apply_attr_delta_float(void)
{
    GameState state = {0};
    state.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "speed", 10.0F));
    TEST_ASSERT_TRUE(vec_entity_push(&state.current_level.entities, entity));

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 3);

    Attribute *attr = &state.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 13.0F, attr->value.f);

    test_attr_set_free(&state.current_level.entities.data[0].attrs);
    vec_entity_free(&state.current_level.entities);
}

void test_editor_apply_attr_delta_null_attr_no_crash(void)
{
    GameState state = {0};
    state.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.current_level.entities, entity));

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 99};
    apply_attr_delta(&state, &editor_state, 1);

    vec_entity_free(&state.current_level.entities);
}

/* ---- attr_at_display_index ---------------------------------------------- */

void test_editor_attr_at_display_index_instance(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 42));

    Attribute *attr = attr_at_display_index(&state, &entity, 0);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(42, attr->value.i);

    test_attr_set_free(&entity.attrs);
}

void test_editor_attr_at_display_index_blueprint(void)
{
    GameState state = {0};
    state.blueprints.entries.alloc = test_heap_alloc;

    TEST_ASSERT_TRUE(vec_blueprint_push(&state.blueprints.entries, (Blueprint){0}));
    Blueprint *blueprint = &state.blueprints.entries.data[0];
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint->attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint->attrs, "bp_val", 99));

    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_val", 10));

    int blueprint_attr_index = entity.attrs.entries.count + 1;
    Attribute *attr = attr_at_display_index(&state, &entity, blueprint_attr_index);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(99, attr->value.i);

    str_free(&test_heap_alloc, &entity.blueprint_name);
    test_attr_set_free(&entity.attrs);
    test_blueprint_table_free(&state.blueprints);
}

void test_editor_attr_at_display_index_out_of_range(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    Attribute *attr = attr_at_display_index(&state, &entity, 99);
    TEST_ASSERT_NULL(attr);

    test_attr_set_free(&entity.attrs);
}

/* ==== Phase 2: mocked raylib input tests ================================= */

static void reset_input_fakes(void)
{
    RESET_FAKE(IsKeyPressed_mock);
    RESET_FAKE(IsKeyDown_mock);
    RESET_FAKE(IsGamepadButtonPressed_mock);
    RESET_FAKE(IsGamepadButtonDown_mock);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

static int target_key_for_press;
static bool press_specific_key(int key)
{
    return key == target_key_for_press;
}

static int target_key_for_down;
static bool down_specific_key(int key)
{
    return key == target_key_for_down;
}

static int target_button_for_down;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool down_specific_button(int gamepad, int button)
{
    (void)gamepad;
    return button == target_button_for_down;
}

/* ---- toggle_pressed ----------------------------------------------------- */

void test_editor_toggle_pressed_key(void)
{
    reset_input_fakes();
    IsKeyPressed_mock_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_mock_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, IsGamepadButtonPressed_mock_fake.call_count);
}

void test_editor_toggle_pressed_gamepad(void)
{
    reset_input_fakes();
    IsKeyPressed_mock_fake.return_val = false;
    IsGamepadButtonPressed_mock_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_mock_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, IsGamepadButtonPressed_mock_fake.call_count);
}

void test_editor_toggle_pressed_neither(void)
{
    reset_input_fakes();
    TEST_ASSERT_FALSE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
}

/* ---- read_value_delta --------------------------------------------------- */

void test_editor_read_value_delta_no_input(void)
{
    reset_input_fakes();
    TEST_ASSERT_EQUAL_INT(0, read_value_delta());
}

void test_editor_read_value_delta_large_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_LEFT_BRACKET;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_large_plus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_RIGHT_BRACKET;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_huge_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_PAGE_DOWN;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_HUGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_combined(void)
{
    reset_input_fakes();
    IsKeyPressed_mock_fake.return_val = true;
    int expected = -EDITOR_ATTR_LARGE_STEP + EDITOR_ATTR_LARGE_STEP - EDITOR_ATTR_HUGE_STEP + EDITOR_ATTR_HUGE_STEP;
    TEST_ASSERT_EQUAL_INT(expected, read_value_delta());
}

/* ---- read_held_dir ------------------------------------------------------ */

void test_editor_read_held_dir_left_key(void)
{
    reset_input_fakes();
    target_key_for_down = KEY_LEFT;
    IsKeyDown_mock_fake.custom_fake = down_specific_key;
    TEST_ASSERT_EQUAL_INT(-1, read_held_dir());
}

void test_editor_read_held_dir_right_gamepad(void)
{
    reset_input_fakes();
    target_button_for_down = GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    IsGamepadButtonDown_mock_fake.custom_fake = down_specific_button;
    TEST_ASSERT_EQUAL_INT(1, read_held_dir());
}

void test_editor_read_held_dir_none(void)
{
    reset_input_fakes();
    TEST_ASSERT_EQUAL_INT(0, read_held_dir());
}

/* ---- word_builder_navigate ---------------------------------------------- */

void test_editor_word_builder_nav_up(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_UP;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 5};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(4, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_up_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_UP;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_DOWN;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(1, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_DOWN;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 9};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(9, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_up(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_Q;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 8};
    word_builder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(8 - WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_down(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_E;
    IsKeyPressed_mock_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

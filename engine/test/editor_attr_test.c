#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor/attr.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib input fakes (read_held_dir calls IsKeyDown/IsGamepadButtonDown directly) */
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

/* Cross-file editor fakes: draw.c */
FAKE_VALUE_FUNC(bool, toggle_pressed, ToggleBinding);

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
FAKE_VALUE_FUNC(bool, is_blueprint_attr, const Entity *, int);
FAKE_VALUE_FUNC(Attribute *, attr_at_display_index, GameState *, Entity *, int);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(propagate_child_tag, GameState *, const Blueprint *, int, const char *);
FAKE_VOID_FUNC(propagate_child_offset, GameState *, const Blueprint *, int);

/* External module fakes */
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_offset, const Blueprint *);
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_size, const Blueprint *);
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

#include "test_heap_alloc.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

static void reset_input_fakes(void)
{
    RESET_FAKE(IsKeyDown);
    RESET_FAKE(IsGamepadButtonDown);
    RESET_FAKE(toggle_pressed);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

static int target_key_for_press;
static bool press_specific_toggle(ToggleBinding binding)
{
    return binding.key == target_key_for_press;
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

/* ---- apply_attr_delta --------------------------------------------------- */

void test_editor_apply_attr_delta_int(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    /* Point attr_at_display_index fake at the actual attribute */
    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 5);

    Attribute *attr = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_EQUAL_INT(15, attr->value.i);

    test_attr_set_free_local(&state.gamedata.current_level.entities.data[0].attrs);
    vec_entity_free(&state.gamedata.current_level.entities);
}

void test_editor_apply_attr_delta_float(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "speed", 10.0F));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 3);

    Attribute *attr = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 13.0F, attr->value.f);

    test_attr_set_free_local(&state.gamedata.current_level.entities.data[0].attrs);
    vec_entity_free(&state.gamedata.current_level.entities);
}

void test_editor_apply_attr_delta_null_attr_no_crash(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    attr_at_display_index_fake.return_val = nullptr;

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 99};
    apply_attr_delta(&state, &editor_state, 1);

    vec_entity_free(&state.gamedata.current_level.entities);
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
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_large_plus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_RIGHT_BRACKET;
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_huge_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_PAGE_DOWN;
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_HUGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_combined(void)
{
    reset_input_fakes();
    toggle_pressed_fake.return_val = true;
    int expected = -EDITOR_ATTR_LARGE_STEP + EDITOR_ATTR_LARGE_STEP - EDITOR_ATTR_HUGE_STEP + EDITOR_ATTR_HUGE_STEP;
    TEST_ASSERT_EQUAL_INT(expected, read_value_delta());
}

/* ---- read_held_dir ------------------------------------------------------ */

void test_editor_read_held_dir_left_key(void)
{
    reset_input_fakes();
    target_key_for_down = KEY_LEFT;
    IsKeyDown_fake.custom_fake = down_specific_key;
    TEST_ASSERT_EQUAL_INT(-1, read_held_dir());
}

void test_editor_read_held_dir_right_gamepad(void)
{
    reset_input_fakes();
    target_button_for_down = GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    IsGamepadButtonDown_fake.custom_fake = down_specific_button;
    TEST_ASSERT_EQUAL_INT(1, read_held_dir());
}

void test_editor_read_held_dir_none(void)
{
    reset_input_fakes();
    TEST_ASSERT_EQUAL_INT(0, read_held_dir());
}

/* ---- attr type conversion ----------------------------------------------- */

void test_attr_type_change_int_to_float(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 42}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_FLOAT, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, attr.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 42.0F, attr.value.f);
}

void test_attr_type_change_float_to_int(void)
{
    Attribute attr = {.type = ATTR_FLOAT, .value = {.f = 3.7F}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_INT, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr.type);
    TEST_ASSERT_EQUAL_INT(3, attr.value.i);
}

void test_attr_type_change_int_to_bool_nonzero(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 5}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_BOOL, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr.type);
    TEST_ASSERT_TRUE(attr.value.b);
}

void test_attr_type_change_int_to_bool_zero(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 0}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_BOOL, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr.type);
    TEST_ASSERT_FALSE(attr.value.b);
}

void test_attr_type_change_string_to_int(void)
{
    Str test_str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &test_str, "42"));
    Attribute attr = {.type = ATTR_STRING, .value = {.str = test_str}};
    AttrConvertedValues values = attr_extract_values(&attr, &test_heap_alloc);
    attr_apply_converted(&attr, ATTR_INT, values, &test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr.type);
    TEST_ASSERT_EQUAL_INT(42, attr.value.i);
}

void test_attr_type_radial_order_matches_enum(void)
{
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, attr_type_radial_order[0]);
    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr_type_radial_order[1]);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr_type_radial_order[2]);
    TEST_ASSERT_EQUAL_INT(ATTR_STRING, attr_type_radial_order[3]);
}

/* ---- attr_remove -------------------------------------------------------- */

void test_attr_remove_instance_attr(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5));

    attr_remove(&test_heap_alloc, &entity.attrs, "speed");

    TEST_ASSERT_EQUAL_INT(1, entity.attrs.entries.count);
    TEST_ASSERT_EQUAL_STRING("health", entity.attrs.entries.data[0].name.ptr);

    test_attr_set_free_local(&entity.attrs);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_apply_attr_delta_int);
    RUN_TEST(test_editor_apply_attr_delta_float);
    RUN_TEST(test_editor_apply_attr_delta_null_attr_no_crash);
    RUN_TEST(test_editor_read_value_delta_no_input);
    RUN_TEST(test_editor_read_value_delta_large_minus);
    RUN_TEST(test_editor_read_value_delta_large_plus);
    RUN_TEST(test_editor_read_value_delta_huge_minus);
    RUN_TEST(test_editor_read_value_delta_combined);
    RUN_TEST(test_editor_read_held_dir_left_key);
    RUN_TEST(test_editor_read_held_dir_right_gamepad);
    RUN_TEST(test_editor_read_held_dir_none);
    RUN_TEST(test_attr_type_change_int_to_float);
    RUN_TEST(test_attr_type_change_float_to_int);
    RUN_TEST(test_attr_type_change_int_to_bool_nonzero);
    RUN_TEST(test_attr_type_change_int_to_bool_zero);
    RUN_TEST(test_attr_type_change_string_to_int);
    RUN_TEST(test_attr_type_radial_order_matches_enum);
    RUN_TEST(test_attr_remove_instance_attr);

    return UNITY_END();
}

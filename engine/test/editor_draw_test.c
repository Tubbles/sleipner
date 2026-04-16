#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor/draw.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib input fakes */
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);

/* raylib draw fakes */
FAKE_VOID_FUNC(DrawLine, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VOID_FUNC(DrawRectangleLinesEx, Rectangle, float, Color);
FAKE_VOID_FUNC(DrawTextureRec, Texture2D, Rectangle, Vector2, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

/* External module fakes */
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_offset, const Blueprint *);
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_size, const Blueprint *);
FAKE_VALUE_FUNC(const AttrSet *, entity_resolve_defaults, const GameState *, int);
FAKE_VALUE_FUNC(const Blueprint *, blueprint_find, const BlueprintTable *, const char *);

/* Cross-file editor fakes (functions from other editor split files) */
FAKE_VALUE_FUNC(int, total_attr_count, const GameState *, const Entity *);
FAKE_VALUE_FUNC(bool, entity_has_persisted_section, const Entity *);
FAKE_VALUE_FUNC(int, place_visible_count, int);

/* Cross-file editor fakes: keybindings.c (used by draw_hints_bar) */
FAKE_VALUE_FUNC(int, binding_table_render, const EditorBindingTable *, char *, int);
FAKE_VALUE_FUNC(const EditorBindingTable *, browse_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, drag_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, handles_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, place_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, attr_edit_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, radial_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, word_builder_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, fuzzy_finder_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, gamepad_kb_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, blueprint_list_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, blueprint_detail_bindings);
FAKE_VALUE_FUNC(const EditorBindingTable *, play_mode_bindings);

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
    RESET_FAKE(IsKeyPressed);
    RESET_FAKE(IsGamepadButtonPressed);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

/* ---- toggle_pressed ----------------------------------------------------- */

void test_editor_toggle_pressed_key(void)
{
    reset_input_fakes();
    IsKeyPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, IsGamepadButtonPressed_fake.call_count);
}

void test_editor_toggle_pressed_gamepad(void)
{
    reset_input_fakes();
    IsKeyPressed_fake.return_val = false;
    IsGamepadButtonPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, IsGamepadButtonPressed_fake.call_count);
}

void test_editor_toggle_pressed_neither(void)
{
    reset_input_fakes();
    TEST_ASSERT_FALSE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
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
    entity.position = (Vector2){10.0F, 20.0F};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_offset_x", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_offset_y", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_w", 32.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_h", 16.0F));

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 20.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.height);

    test_attr_set_free_local(&entity.attrs);
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

    test_attr_set_free_local(&entity.attrs);
}

/* ---- diff_view color logic ---------------------------------------------- */

void test_diff_view_override_detection(void)
{
    AttrSet instance = {0};
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &instance, "speed", 20));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint, "speed", 10));

    const Attribute *found = attr_get(&blueprint, "speed");
    TEST_ASSERT_NOT_NULL(found);

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&blueprint);
}

void test_diff_view_custom_detection(void)
{
    AttrSet instance = {0};
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &instance, "custom_flag", 1));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint, "speed", 10));

    TEST_ASSERT_NULL(attr_get(&blueprint, "custom_flag"));

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&blueprint);
}

/* ---- get_source_rect ---------------------------------------------------- */

void test_get_source_rect_from_instance(void)
{
    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_x", 10.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_y", 20.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_w", 32.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_h", 48.0F));

    Rectangle rect = get_source_rect(&instance, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(10.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(20.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(32.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(48.0F, rect.height);

    test_attr_set_free_local(&instance);
}

void test_get_source_rect_from_defaults(void)
{
    AttrSet instance = {0};
    AttrSet defaults = {0};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &defaults, "src_x", 5.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &defaults, "src_w", 16.0F));

    Rectangle rect = get_source_rect(&instance, &defaults);
    TEST_ASSERT_EQUAL_FLOAT(5.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(16.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.height);

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&defaults);
}

void test_get_source_rect_zero_when_missing(void)
{
    AttrSet instance = {0};
    Rectangle rect = get_source_rect(&instance, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.height);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_toggle_pressed_key);
    RUN_TEST(test_editor_toggle_pressed_gamepad);
    RUN_TEST(test_editor_toggle_pressed_neither);
    RUN_TEST(test_editor_find_nearest_single_entity);
    RUN_TEST(test_editor_find_nearest_closer_wins);
    RUN_TEST(test_editor_find_nearest_skips_children);
    RUN_TEST(test_editor_find_nearest_empty_level);
    RUN_TEST(test_editor_entity_outline_rect_with_collision);
    RUN_TEST(test_editor_entity_outline_rect_without_collision);
    RUN_TEST(test_diff_view_override_detection);
    RUN_TEST(test_diff_view_custom_detection);
    RUN_TEST(test_get_source_rect_from_instance);
    RUN_TEST(test_get_source_rect_from_defaults);
    RUN_TEST(test_get_source_rect_zero_when_missing);

    return UNITY_END();
}

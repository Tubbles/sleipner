#include "fff.h"
#include "unity.h"

#include "../src/editor/keybindings.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

void setUp(void)
{
    RESET_FAKE(IsKeyPressed);
    RESET_FAKE(IsKeyDown);
    RESET_FAKE(IsGamepadButtonPressed);
    RESET_FAKE(IsGamepadButtonDown);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

void tearDown(void) {}

/* ---- binding_pressed — simple (no chord) ------------------------------- */

void test_binding_pressed_keyboard_only(void)
{
    EditorBinding action = {.binding = {KEY_A, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}};
    IsKeyPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(binding_pressed(&action));
}

void test_binding_pressed_gamepad_only(void)
{
    EditorBinding action = {.binding = {KEY_A, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}};
    IsGamepadButtonPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(binding_pressed(&action));
}

void test_binding_pressed_nothing_held(void)
{
    EditorBinding action = {.binding = {KEY_A, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}};
    TEST_ASSERT_FALSE(binding_pressed(&action));
}

/* ---- binding_pressed — chord (modifier) -------------------------------- */

void test_binding_pressed_chord_keyboard_modifier_held(void)
{
    EditorBinding action = {
        .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    IsKeyPressed_fake.return_val = true;
    IsKeyDown_fake.return_val = true;
    TEST_ASSERT_TRUE(binding_pressed(&action));
}

void test_binding_pressed_chord_keyboard_modifier_not_held(void)
{
    EditorBinding action = {
        .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    IsKeyPressed_fake.return_val = true;
    IsKeyDown_fake.return_val = false;
    IsGamepadButtonDown_fake.return_val = false;
    TEST_ASSERT_FALSE(binding_pressed(&action));
}

void test_binding_pressed_chord_gamepad_modifier_held(void)
{
    EditorBinding action = {
        .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    IsGamepadButtonPressed_fake.return_val = true;
    IsGamepadButtonDown_fake.return_val = true;
    TEST_ASSERT_TRUE(binding_pressed(&action));
}

void test_binding_pressed_chord_gamepad_modifier_not_held(void)
{
    EditorBinding action = {
        .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    IsGamepadButtonPressed_fake.return_val = true;
    IsGamepadButtonDown_fake.return_val = false;
    IsKeyDown_fake.return_val = false;
    TEST_ASSERT_FALSE(binding_pressed(&action));
}

/* ---- binding_held ------------------------------------------------------- */

void test_binding_held_fires_while_key_down(void)
{
    EditorBinding action = {.binding = {KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT}};
    IsKeyDown_fake.return_val = true;
    TEST_ASSERT_TRUE(binding_held(&action));
}

void test_binding_held_chord_requires_modifier(void)
{
    EditorBinding action = {
        .binding = {KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    /* Key down but no modifier → held=false */
    static const int key_left_code = KEY_LEFT;
    static const int key_ctrl_code = KEY_LEFT_CONTROL;
    IsKeyDown_fake.custom_fake = nullptr;
    bool key_down_returns[] = {true, false}; /* first call: main, second: modifier */
    SET_RETURN_SEQ(IsKeyDown, key_down_returns, 2);
    (void)key_left_code;
    (void)key_ctrl_code;
    TEST_ASSERT_FALSE(binding_held(&action));
}

/* ---- binding_label render ----------------------------------------------- */

void test_binding_label_plain_keyboard_and_gamepad(void)
{
    EditorBinding action = {.binding = {KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}};
    char out[32];
    int len = binding_label(&action, out, (int)sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("A/Ent", out);
}

void test_binding_label_chord(void)
{
    EditorBinding action = {
        .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
    };
    char out[32];
    (void)binding_label(&action, out, (int)sizeof(out));
    TEST_ASSERT_EQUAL_STRING("L1+Left/Ctrl+Z", out);
}

void test_binding_label_keyboard_only(void)
{
    EditorBinding action = {.binding = {KEY_F5, 0}};
    char out[32];
    (void)binding_label(&action, out, (int)sizeof(out));
    TEST_ASSERT_EQUAL_STRING("F5", out);
}

/* ---- binding_table_render ----------------------------------------------- */

void test_table_render_contains_mode_label_and_bindings(void)
{
    const EditorBinding actions[] = {
        {.binding = {KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, .description = "Confirm"},
        {
            .binding = {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
            .modifier = {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
            .description = "Undo",
        },
    };
    const EditorBindingTable table = {
        .actions = actions,
        .count = (int)(sizeof(actions) / sizeof(actions[0])),
        .mode_label = "Edit",
    };
    char out[256];
    int len = binding_table_render(&table, out, (int)sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out, "[Edit]"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Confirm"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Ctrl+Z"));
    TEST_ASSERT_NOT_NULL(strstr(out, "L1+Left"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Undo"));
}

void test_table_render_skips_empty_entries(void)
{
    const EditorBinding actions[] = {
        {.binding = {KEY_ENTER, 0}, .description = "Confirm"},
        {.binding = {0, 0}, .description = "Unbound"}, /* skipped */
    };
    const EditorBindingTable table = {
        .actions = actions,
        .count = 2,
        .mode_label = "M",
    };
    char out[128];
    (void)binding_table_render(&table, out, (int)sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "Confirm"));
    TEST_ASSERT_NULL(strstr(out, "Unbound"));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_binding_pressed_keyboard_only);
    RUN_TEST(test_binding_pressed_gamepad_only);
    RUN_TEST(test_binding_pressed_nothing_held);
    RUN_TEST(test_binding_pressed_chord_keyboard_modifier_held);
    RUN_TEST(test_binding_pressed_chord_keyboard_modifier_not_held);
    RUN_TEST(test_binding_pressed_chord_gamepad_modifier_held);
    RUN_TEST(test_binding_pressed_chord_gamepad_modifier_not_held);
    RUN_TEST(test_binding_held_fires_while_key_down);
    RUN_TEST(test_binding_held_chord_requires_modifier);
    RUN_TEST(test_binding_label_plain_keyboard_and_gamepad);
    RUN_TEST(test_binding_label_chord);
    RUN_TEST(test_binding_label_keyboard_only);
    RUN_TEST(test_table_render_contains_mode_label_and_bindings);
    RUN_TEST(test_table_render_skips_empty_entries);

    return UNITY_END();
}

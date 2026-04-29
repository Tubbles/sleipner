#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* Raylib draw fakes — keyboard_widget_draw is exercised by the unit
 * tests but the actual draw path is faked out. */
FAKE_VOID_FUNC(DrawCircle, int, int, float, Color);
FAKE_VOID_FUNC(DrawRing, Vector2, float, float, float, float, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);

/* Raylib input fakes — input.c reads these but the tests construct
 * InputState directly. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

#include "test_heap_alloc.h"

#include "../src/strv.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/keyboard_widget.c" // NOLINT(bugprone-suspicious-include)

/* TextFormat is a variadic raylib helper; provide a no-op stub. */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

static BindingStore store;
static char buf[256];
static int len;
static KeyboardWidget widget;

void setUp(void)
{
    test_helpers_init();
    store = (BindingStore){0};
    input_func_load_defaults(&store, test_heap_alloc);
    buf[0] = '\0';
    len = 0;
    keyboard_widget_reset(&widget, buf, &len, (int)sizeof(buf));
}

void tearDown(void)
{
    for (int act = 0; act < ACTION_COUNT; act++) {
        for (int alt = 0; alt < store.actions[act].alternatives.count; alt++) {
            vec_atomic_input_free(&store.actions[act].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store.actions[act].alternatives);
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        for (int alt = 0; alt < store.axes[axis].alternatives.count; alt++) {
            vec_atomic_input_free(&store.axes[axis].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store.axes[axis].alternatives);
    }
}

/* Drive the radial via the synthetic gamepad axis: the widget reads
 * AXIS_PRIMARY_X/Y, which by default come from GAMEPAD_AXIS_LEFT_X/Y. */
static void tilt_stick(InputState *input, float x_axis, float y_axis)
{
    input_state_set_gp_axis(input, GAMEPAD_AXIS_LEFT_X, x_axis);
    input_state_set_gp_axis(input, GAMEPAD_AXIS_LEFT_Y, y_axis);
}

void test_reset_clears_group_and_selected(void)
{
    widget.group = 3;
    widget.selected = 2;
    keyboard_widget_reset(&widget, buf, &len, (int)sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, widget.group);
    TEST_ASSERT_EQUAL_INT(-1, widget.selected);
    TEST_ASSERT_EQUAL_PTR(buf, widget.buf);
}

void test_confirm_north_picks_first_group_then_first_char(void)
{
    /* Stick tilted up = 12 o'clock = sector 0 (the vowels group "aeiou"). */
    InputState input = {0};
    tilt_stick(&input, 0.0F, -1.0F);
    input_state_press_key(&input, KEY_ENTER); /* CONFIRM */

    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_NONE, result); /* group selected, no char yet */
    TEST_ASSERT_EQUAL_INT(0, widget.group);

    /* Second tilt+confirm picks the first char of the group. */
    InputState input2 = {0};
    tilt_stick(&input2, 0.0F, -1.0F);
    input_state_press_key(&input2, KEY_ENTER);
    result = keyboard_widget_handle_input(&widget, &input2, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_TYPED, result);
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_INT('a', buf[0]);
    TEST_ASSERT_EQUAL_INT(-1, widget.group);
}

void test_cancel_at_top_level_with_empty_buffer_emits_exit(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE); /* CANCEL */
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_EXIT_REQUESTED, result);
}

void test_cancel_at_top_level_with_buffer_exits(void)
{
    /* New behavior: Cancel (Esc / B) is always "leave the widget" once
     * the user is at the top-level group, regardless of buffer contents.
     * The backspace path now lives on ACTION_KEYBOARD_BACKSPACE so the
     * host screen can use B / Esc as "back" without it eating chars. */
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = '\0';
    len = 2;
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE);
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_EXIT_REQUESTED, result);
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_INT('a', buf[0]);
    TEST_ASSERT_EQUAL_INT('b', buf[1]);
}

void test_backspace_deletes_one_character(void)
{
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = '\0';
    len = 2;
    InputState input = {0};
    input_state_press_key(&input, KEY_BACKSPACE);
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_BACKSPACED, result);
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_INT('a', buf[0]);
    TEST_ASSERT_EQUAL_INT('\0', buf[1]);
}

void test_backspace_on_empty_buffer_is_noop(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_BACKSPACE);
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_NONE, result);
    TEST_ASSERT_EQUAL_INT(0, len);
}

void test_cancel_inside_group_exits_directly(void)
{
    /* New behavior: Cancel always exits in one press. The "pop group"
     * intermediate step was removed so the host screen can rely on
     * Cancel being a deterministic single-press exit, not a depth-
     * dependent multi-press. The group state is left untouched; the
     * host resets it via keyboard_widget_reset on next entry. */
    widget.group = 2;
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE);
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    TEST_ASSERT_EQUAL_INT(KB_RESULT_EXIT_REQUESTED, result);
}

void test_typing_at_capacity_does_not_overflow(void)
{
    for (int index = 0; index < (int)sizeof(buf) - 1; index++) {
        buf[index] = 'x';
    }
    buf[sizeof(buf) - 1] = '\0';
    len = (int)sizeof(buf) - 1;
    /* Force a TYPED attempt: pre-set group, tilt north to select sector 0,
     * then CONFIRM. The widget must clamp at cap-1 and not overflow. */
    widget.group = 0;
    InputState input = {0};
    tilt_stick(&input, 0.0F, -1.0F);
    input_state_press_key(&input, KEY_ENTER);
    KeyboardWidgetResult result = keyboard_widget_handle_input(&widget, &input, &store);
    /* Buffer was full so kb_type_char declined; widget reports NONE. */
    TEST_ASSERT_EQUAL_INT(KB_RESULT_NONE, result);
    TEST_ASSERT_EQUAL_INT((int)sizeof(buf) - 1, len);
}

/* ---- Char-group data integrity (moved here from editor_widgets_test.c) -- */

void test_keyboard_group_sizes_consistent(void)
{
    for (int group = 0; group < KEYBOARD_GROUP_COUNT; group++) {
        int actual_size = 0;
        for (int character = 0; character < KEYBOARD_MAX_CHARS_PER_GROUP; character++) {
            if (keyboard_groups[group][character] != '\0') {
                actual_size++;
            }
        }
        TEST_ASSERT_EQUAL_INT(keyboard_group_sizes[group], actual_size);
    }
}

void test_keyboard_all_lowercase_alpha_present(void)
{
    for (int code = 'a'; code <= 'z'; code++) {
        char letter = (char)code;
        bool found = false;
        for (int group = 0; group < KEYBOARD_GROUP_COUNT && !found; group++) {
            for (int character = 0; character < keyboard_group_sizes[group]; character++) {
                if (keyboard_groups[group][character] == letter) {
                    found = true;
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE(found);
    }
}

void test_keyboard_all_digits_present(void)
{
    for (int code = '0'; code <= '9'; code++) {
        char digit = (char)code;
        bool found = false;
        for (int group = 0; group < KEYBOARD_GROUP_COUNT && !found; group++) {
            for (int character = 0; character < keyboard_group_sizes[group]; character++) {
                if (keyboard_groups[group][character] == digit) {
                    found = true;
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE(found);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_clears_group_and_selected);
    RUN_TEST(test_confirm_north_picks_first_group_then_first_char);
    RUN_TEST(test_cancel_at_top_level_with_empty_buffer_emits_exit);
    RUN_TEST(test_cancel_at_top_level_with_buffer_exits);
    RUN_TEST(test_backspace_deletes_one_character);
    RUN_TEST(test_backspace_on_empty_buffer_is_noop);
    RUN_TEST(test_cancel_inside_group_exits_directly);
    RUN_TEST(test_typing_at_capacity_does_not_overflow);
    RUN_TEST(test_keyboard_group_sizes_consistent);
    RUN_TEST(test_keyboard_all_lowercase_alpha_present);
    RUN_TEST(test_keyboard_all_digits_present);
    return UNITY_END();
}

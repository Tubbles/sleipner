#include "fff.h"
#include "unity.h"

#include "../src/touch.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* touch_update() calls these raylib functions — provide fff definitions */
FAKE_VALUE_FUNC(int, GetTouchPointCount);
FAKE_VALUE_FUNC(Vector2, GetTouchPosition, int);

void setUp(void) {}
void tearDown(void) {}

static Rectangle make_button_rect(void)
{
    return (Rectangle){900.0F, 0.0F, 100.0F, 100.0F};
}

void test_touch_initial_state_is_none(void)
{
    TouchState state = {0};
    TEST_ASSERT_EQUAL_INT(TOUCH_NONE, state.mode);
}

void test_touch_first_touch_outside_button_sets_stick_mode(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    TEST_ASSERT_EQUAL_INT(TOUCH_STICK, state.mode);
}

void test_touch_first_touch_sets_origin(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 100.0F, state.stick_origin.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 400.0F, state.stick_origin.y);
}

void test_touch_continue_updates_current(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    touch_process(&state, 1, (Vector2){150.0F, 450.0F}, make_button_rect());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 150.0F, state.stick_current.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 450.0F, state.stick_current.y);
}

void test_touch_continue_preserves_origin(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    touch_process(&state, 1, (Vector2){150.0F, 450.0F}, make_button_rect());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 100.0F, state.stick_origin.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 400.0F, state.stick_origin.y);
}

void test_touch_release_resets_mode(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    touch_process(&state, 0, (Vector2){0.0F, 0.0F}, make_button_rect());
    TEST_ASSERT_EQUAL_INT(TOUCH_NONE, state.mode);
}

void test_touch_button_area_sets_button_mode(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){950.0F, 50.0F}, make_button_rect());
    TEST_ASSERT_EQUAL_INT(TOUCH_BUTTON, state.mode);
}

void test_touch_button_triggers_on_press(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){950.0F, 50.0F}, make_button_rect());
    TEST_ASSERT_TRUE(state.debug_button_triggered);
}

void test_touch_button_not_triggered_on_hold(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){950.0F, 50.0F}, make_button_rect());
    touch_process(&state, 1, (Vector2){950.0F, 50.0F}, make_button_rect());
    TEST_ASSERT_FALSE(state.debug_button_triggered);
}

void test_touch_get_stick_no_touch_returns_zero(void)
{
    TouchState state = {0};
    Vector2 result = touch_get_stick(&state, 100.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.y);
}

void test_touch_get_stick_right_returns_positive_x(void)
{
    TouchState state = {0};
    /* Start at x=200, drag to x=300 — 100px right with max_radius=200 → 0.5 */
    touch_process(&state, 1, (Vector2){200.0F, 400.0F}, make_button_rect());
    touch_process(&state, 1, (Vector2){300.0F, 400.0F}, make_button_rect());
    Vector2 result = touch_get_stick(&state, 200.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.5F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.y);
}

void test_touch_get_stick_clamps_to_one(void)
{
    TouchState state = {0};
    /* Start at x=100, drag 400px right — far past max_radius=100 → clamped to 1.0 */
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    touch_process(&state, 1, (Vector2){500.0F, 400.0F}, make_button_rect());
    Vector2 result = touch_get_stick(&state, 100.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.y);
}

void test_touch_not_triggered_outside_button(void)
{
    TouchState state = {0};
    touch_process(&state, 1, (Vector2){100.0F, 400.0F}, make_button_rect());
    TEST_ASSERT_FALSE(state.debug_button_triggered);
}

int main(void)
{
    UNITY_BEGIN();
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
    return UNITY_END();
}

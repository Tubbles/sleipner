#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* raylib fakes — input.c references these but input_apply_deadzone
 * (the unit under test) does not. They exist solely to satisfy the
 * linker when we pull in input.c as a translation-unit include.
 * raylib.h is included above so clang-tidy sees these as definitions
 * of already-declared external functions, not new user-defined names. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

/* debug_log is variadic; provide a no-op stub rather than a fake. */
#include "debug.h"
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

#include "../src/input.c" // NOLINT(bugprone-suspicious-include)

#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static float vec2_magnitude(Vector2 vector)
{
    return sqrtf((vector.x * vector.x) + (vector.y * vector.y));
}

/* ---- Zero input --------------------------------------------------------- */

void test_input_deadzone_zero_input_with_deadzone_returns_zero(void)
{
    Vector2 result = input_apply_deadzone((Vector2){0.0F, 0.0F}, 0.15F);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.y);
}

void test_input_deadzone_zero_input_zero_deadzone_returns_zero(void)
{
    /* Divide-by-zero guard: magnitude 0 with deadzone 0 must return (0, 0)
     * rather than computing 0/0. */
    Vector2 result = input_apply_deadzone((Vector2){0.0F, 0.0F}, 0.0F);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.y);
}

void test_input_deadzone_below_deadzone_returns_zero(void)
{
    Vector2 result = input_apply_deadzone((Vector2){0.1F, 0.0F}, 0.15F);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, result.y);
}

/* ---- Cardinal max ------------------------------------------------------- */

void test_input_deadzone_cardinal_max_reaches_unit_magnitude(void)
{
    /* Full push right: (1.0 - 0.15) / (1.0 - 0.15) = 1.0, direction +x. */
    Vector2 result = input_apply_deadzone((Vector2){1.0F, 0.0F}, 0.15F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, vec2_magnitude(result));
}

/* ---- Square corner clamp — the core bug --------------------------------- */

void test_input_deadzone_square_corner_clamps_to_unit_disc(void)
{
    /* Stick reports (1, 1) — raw magnitude sqrt(2). With the bug, output
     * magnitude is ~1.49. With the fix, clamped to 1.0. Direction must
     * stay on the +x+y diagonal. */
    Vector2 result = input_apply_deadzone((Vector2){1.0F, 1.0F}, 0.15F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, vec2_magnitude(result));
    TEST_ASSERT_TRUE(result.x > 0.0F);
    TEST_ASSERT_TRUE(result.y > 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, result.x, result.y);
}

void test_input_deadzone_keyboard_diagonal_normalizes_to_unit(void)
{
    /* Keyboard diagonal: deadzone = 0, stick = (1, 1). Expected output
     * is (1/sqrt(2), 1/sqrt(2)) so diagonal speed matches cardinal. */
    Vector2 result = input_apply_deadzone((Vector2){1.0F, 1.0F}, 0.0F);
    float expected_component = 1.0F / sqrtf(2.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, expected_component, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, expected_component, result.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, vec2_magnitude(result));
}

/* ---- Mid-range scaling -------------------------------------------------- */

void test_input_deadzone_mid_range_scales_correctly(void)
{
    /* Half-push on one axis with deadzone 0.15:
     * mapped = (0.5 - 0.15) / (1 - 0.15) = 0.35 / 0.85 ≈ 0.4118. */
    Vector2 result = input_apply_deadzone((Vector2){0.5F, 0.0F}, 0.15F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.4118F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, result.y);
}

/* ---- Sign preservation -------------------------------------------------- */

void test_input_deadzone_negative_diagonal_preserves_sign(void)
{
    Vector2 result = input_apply_deadzone((Vector2){-1.0F, -1.0F}, 0.15F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, vec2_magnitude(result));
    TEST_ASSERT_TRUE(result.x < 0.0F);
    TEST_ASSERT_TRUE(result.y < 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, result.x, result.y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_deadzone_zero_input_with_deadzone_returns_zero);
    RUN_TEST(test_input_deadzone_zero_input_zero_deadzone_returns_zero);
    RUN_TEST(test_input_deadzone_below_deadzone_returns_zero);
    RUN_TEST(test_input_deadzone_cardinal_max_reaches_unit_magnitude);
    RUN_TEST(test_input_deadzone_square_corner_clamps_to_unit_disc);
    RUN_TEST(test_input_deadzone_keyboard_diagonal_normalizes_to_unit);
    RUN_TEST(test_input_deadzone_mid_range_scales_correctly);
    RUN_TEST(test_input_deadzone_negative_diagonal_preserves_sign);
    return UNITY_END();
}

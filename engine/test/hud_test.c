#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* Raylib draw fakes -- hud_draw_hearts is not exercised by these tests
 * (only the pure layout/state half is), but hud.c is compiled directly
 * into this translation unit (mirrors effect_test.c/keyboard_widget_test.c),
 * so its calls to these raylib functions still need to resolve. */
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawRectangleLines, int, int, int, int, Color);

#include "../src/hud.c" // NOLINT(bugprone-suspicious-include)

void setUp(void) {}
void tearDown(void) {}

void test_hud_compute_hearts_full_health_all_full(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 6.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[2]);
}

void test_hud_compute_hearts_half_last_heart(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 5.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_HALF, hearts.hearts[2]);
}

void test_hud_compute_hearts_empty_tail(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 2.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[2]);
}

void test_hud_compute_hearts_zero_health_all_empty(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 0.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[2]);
}

/* Odd max_health (5 -> 2.5 hearts) rounds the heart count up to 3; at full
 * health the last heart is a half-slot, since only one health point fills
 * its HUD_HEALTH_PER_HEART capacity. */
void test_hud_compute_hearts_odd_max_last_heart_is_half_slot(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 5.0F, .max = 5.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_HALF, hearts.hearts[2]);
}

void test_hud_compute_hearts_clamps_current_above_max(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 100.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_FULL, hearts.hearts[2]);
}

void test_hud_compute_hearts_clamps_negative_current_to_zero(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = -5.0F, .max = 6.0F});

    TEST_ASSERT_EQUAL_INT(3, hearts.count);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[0]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[1]);
    TEST_ASSERT_EQUAL_INT(HUD_HEART_EMPTY, hearts.hearts[2]);
}

/* A max_health far beyond HUD_MAX_HEARTS_DISPLAYED (e.g. the "player"
 * blueprint's current health = [100, 100], 50 half-hearts) clamps the
 * heart row instead of overflowing the fixed-cap array. */
void test_hud_compute_hearts_clamps_count_to_max_displayed(void)
{
    HudHearts hearts = hud_compute_hearts((HudPlayerHealth){.current = 100.0F, .max = 100.0F});

    TEST_ASSERT_EQUAL_INT(HUD_MAX_HEARTS_DISPLAYED, hearts.count);
}

void test_hud_heart_screen_position_spaces_hearts_from_top_left_margin(void)
{
    Vector2 first = hud_heart_screen_position(0);
    Vector2 second = hud_heart_screen_position(1);
    Vector2 third = hud_heart_screen_position(2);

    TEST_ASSERT_EQUAL_FLOAT(12.0F, first.x);
    TEST_ASSERT_EQUAL_FLOAT(12.0F, first.y);
    TEST_ASSERT_EQUAL_FLOAT(32.0F, second.x);
    TEST_ASSERT_EQUAL_FLOAT(12.0F, second.y);
    TEST_ASSERT_EQUAL_FLOAT(52.0F, third.x);
    TEST_ASSERT_EQUAL_FLOAT(12.0F, third.y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hud_compute_hearts_full_health_all_full);
    RUN_TEST(test_hud_compute_hearts_half_last_heart);
    RUN_TEST(test_hud_compute_hearts_empty_tail);
    RUN_TEST(test_hud_compute_hearts_zero_health_all_empty);
    RUN_TEST(test_hud_compute_hearts_odd_max_last_heart_is_half_slot);
    RUN_TEST(test_hud_compute_hearts_clamps_current_above_max);
    RUN_TEST(test_hud_compute_hearts_clamps_negative_current_to_zero);
    RUN_TEST(test_hud_compute_hearts_clamps_count_to_max_displayed);
    RUN_TEST(test_hud_heart_screen_position_spaces_hearts_from_top_left_margin);
    return UNITY_END();
}

#include "unity.h"
#include "game.h"
#include "render.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>

/* Regression guard for TODO.md's "wall-warp" report ("running against the
 * walls significantly warps the sprite... could be related to float
 * position not scaling up correctly or other scaling issue"). game_test.c
 * and integration_test.c already prove player movement itself is smooth
 * (test_integration_wall_press_no_position_jump); these tests cover the
 * render-side suspect named in that investigation: the sub-pixel upscale
 * blit in main.c's render_frame (render_split_camera_target +
 * render_upscale_dest_rect, composed here as
 * render_world_to_screen_position). */

void test_render_split_camera_target_separates_integer_and_fraction(void)
{
    CameraSplit split = render_split_camera_target((Vector2){160.75F, 40.25F});
    TEST_ASSERT_EQUAL_FLOAT(160.0F, split.integer_target.x);
    TEST_ASSERT_EQUAL_FLOAT(40.0F, split.integer_target.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 0.75F, split.fractional_offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 0.25F, split.fractional_offset.y);
}

void test_render_split_camera_target_exact_integer_has_zero_fraction(void)
{
    CameraSplit split = render_split_camera_target((Vector2){42.0F, -8.0F});
    TEST_ASSERT_EQUAL_FLOAT(42.0F, split.integer_target.x);
    TEST_ASSERT_EQUAL_FLOAT(-8.0F, split.integer_target.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, split.fractional_offset.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, split.fractional_offset.y);
}

void test_render_upscale_dest_rect_matches_formula(void)
{
    Rectangle dest = render_upscale_dest_rect((Vector2){0.5F, 0.25F}, 4, 800, 600);
    TEST_ASSERT_EQUAL_FLOAT(-2.0F, dest.x);
    TEST_ASSERT_EQUAL_FLOAT(-1.0F, dest.y);
    TEST_ASSERT_EQUAL_FLOAT(804.0F, dest.width);
    TEST_ASSERT_EQUAL_FLOAT(604.0F, dest.height);
}

/* Sweep camera_target across many integer boundaries (the point where
 * render_split_camera_target's integer_target increments and
 * fractional_offset wraps from just-under-1 back to 0) while a world
 * point sits at a wall-clamped position (FRAME_SIZE/2, matching
 * update_player's boundary clamp in game.c). A "warp" would show up as a
 * discontinuous multi-pixel jump in the on-screen position right at one
 * of these crossings. Half a screen pixel is the threshold below which a
 * discontinuity could not be visually distinguished from smooth motion;
 * it is also ~25x the tiny (~0.02px at 800x600/PIXEL_SCALE=4) rounding
 * gap that render_upscale_dest_rect's dest.width = screen_width +
 * pixel_scale oversizing provably introduces at each crossing (dest
 * scale is a hair over pixel_scale so the low-res buffer isn't left
 * short at the sub-pixel-shifted edge) -- expected and harmless at that
 * magnitude, not the reported "significant" warp. */
static void assert_screen_position_continuous_along_axis(bool sweep_x)
{
    RectU32 game_bounds = {200, 150}; /* matches SCREEN_WIDTH/HEIGHT_DEFAULT / PIXEL_SCALE(4) */
    int screen_width = 800;
    int screen_height = 600;
    int pixel_scale = 4;
    float half = FRAME_SIZE / 2.0F;

    Vector2 world_position = {half, half};
    Vector2 camera_target = {half, half};
    float *swept_axis = sweep_x ? &camera_target.x : &camera_target.y;

    Vector2 previous = render_world_to_screen_position(world_position, game_bounds, camera_target, screen_width,
                                                       screen_height, pixel_scale);
    const float max_step = 0.5F;
    for (int step = 0; step < 2000; step++) {
        *swept_axis += 0.01F; /* crosses an integer boundary every 100 steps */
        Vector2 current = render_world_to_screen_position(world_position, game_bounds, camera_target, screen_width,
                                                          screen_height, pixel_scale);
        char message[128];
        (void)snprintf(message, sizeof(message), "step %d (%s) screen moved (%.4f, %.4f)", step, sweep_x ? "x" : "y",
                       (double)(current.x - previous.x), (double)(current.y - previous.y));
        TEST_ASSERT_TRUE_MESSAGE(fabsf(current.x - previous.x) <= max_step, message);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(current.y - previous.y) <= max_step, message);
        previous = current;
    }
}

void test_render_world_to_screen_position_continuous_sweeping_x_at_wall(void)
{
    assert_screen_position_continuous_along_axis(true);
}

void test_render_world_to_screen_position_continuous_sweeping_y_at_wall(void)
{
    assert_screen_position_continuous_along_axis(false);
}

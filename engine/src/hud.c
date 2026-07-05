#include "hud.h"

#include "raylib.h"

#include <math.h>

static float clamp_to_range(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static HudHeartState classify_heart(int half_heart_units, int heart_index)
{
    int remaining = half_heart_units - (heart_index * HUD_HEALTH_PER_HEART);
    if (remaining >= HUD_HEALTH_PER_HEART) {
        return HUD_HEART_FULL;
    }
    if (remaining >= 1) {
        return HUD_HEART_HALF;
    }
    return HUD_HEART_EMPTY;
}

HudHearts hud_compute_hearts(HudPlayerHealth health)
{
    float clamped_max = health.max > 0.0F ? health.max : 0.0F;
    float clamped_current = clamp_to_range(health.current, 0.0F, clamped_max);
    int half_heart_units = (int)roundf(clamped_current);

    int heart_count = (int)ceilf(clamped_max / (float)HUD_HEALTH_PER_HEART);
    if (heart_count > HUD_MAX_HEARTS_DISPLAYED) {
        heart_count = HUD_MAX_HEARTS_DISPLAYED;
    }

    HudHearts result = {.count = heart_count};
    for (int heart_index = 0; heart_index < heart_count; heart_index++) {
        result.hearts[heart_index] = classify_heart(half_heart_units, heart_index);
    }
    return result;
}

/* Placeholder layout constants (v1, no heart sprite yet -- TODO.md). */
#define HUD_HEART_MARGIN_X 12
#define HUD_HEART_MARGIN_Y 12
#define HUD_HEART_SIZE 16
#define HUD_HEART_SPACING 4

Vector2 hud_heart_screen_position(int heart_index)
{
    float step = (float)(HUD_HEART_SIZE + HUD_HEART_SPACING);
    return (Vector2){
        (float)HUD_HEART_MARGIN_X + (step * (float)heart_index),
        (float)HUD_HEART_MARGIN_Y,
    };
}

static void draw_heart(Vector2 position, HudHeartState state)
{
    int position_x = (int)position.x;
    int position_y = (int)position.y;
    switch (state) {
    case HUD_HEART_FULL:
        DrawRectangle(position_x, position_y, HUD_HEART_SIZE, HUD_HEART_SIZE, RED);
        break;
    case HUD_HEART_HALF:
        DrawRectangleLines(position_x, position_y, HUD_HEART_SIZE, HUD_HEART_SIZE, RED);
        DrawRectangle(position_x, position_y, HUD_HEART_SIZE / 2, HUD_HEART_SIZE, RED);
        break;
    case HUD_HEART_EMPTY:
        DrawRectangleLines(position_x, position_y, HUD_HEART_SIZE, HUD_HEART_SIZE, GRAY);
        break;
    }
}

void hud_draw_hearts(HudHearts hearts)
{
    for (int heart_index = 0; heart_index < hearts.count; heart_index++) {
        draw_heart(hud_heart_screen_position(heart_index), hearts.hearts[heart_index]);
    }
}

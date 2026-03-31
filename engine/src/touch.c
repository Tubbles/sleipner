#include "touch.h"

#include "raylib.h"

#include <math.h>

static bool point_in_rect(Vector2 point, Rectangle rect)
{
    return point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y && point.y < rect.y + rect.height;
}

void touch_process(TouchState *state, int touch_count, Vector2 touch_pos, Rectangle debug_button_rect)
{
    state->debug_button_triggered = false;

    if (touch_count > 0 && state->prev_touch_count == 0) {
        /* New touch began — classify by location */
        if (point_in_rect(touch_pos, debug_button_rect)) {
            state->mode = TOUCH_BUTTON;
            state->debug_button_triggered = true;
        } else {
            state->mode = TOUCH_STICK;
            state->stick_origin = touch_pos;
            state->stick_current = touch_pos;
        }
    } else if (touch_count > 0) {
        /* Continuing touch — update stick position */
        if (state->mode == TOUCH_STICK) {
            state->stick_current = touch_pos;
        }
    } else {
        /* Touch released */
        state->mode = TOUCH_NONE;
        state->stick_origin = (Vector2){0.0F, 0.0F};
        state->stick_current = (Vector2){0.0F, 0.0F};
    }

    state->prev_touch_count = touch_count;
}

void touch_update(TouchState *state, Rectangle debug_button_rect)
{
    int touch_count = GetTouchPointCount();
    Vector2 touch_pos = (touch_count > 0) ? GetTouchPosition(0) : (Vector2){0.0F, 0.0F};
    touch_process(state, touch_count, touch_pos, debug_button_rect);
}

Vector2 touch_get_stick(const TouchState *state, float max_radius)
{
    if (state->mode != TOUCH_STICK || max_radius <= 0.0F) {
        return (Vector2){0.0F, 0.0F};
    }
    float delta_x = state->stick_current.x - state->stick_origin.x;
    float delta_y = state->stick_current.y - state->stick_origin.y;
    float length = sqrtf((delta_x * delta_x) + (delta_y * delta_y));
    if (length <= 0.0F) {
        return (Vector2){0.0F, 0.0F};
    }
    float clamped = (length < max_radius) ? length : max_radius;
    float scale = clamped / (length * max_radius);
    return (Vector2){delta_x * scale, delta_y * scale};
}

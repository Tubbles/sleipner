#pragma once

#include "raylib.h"
#include <stdbool.h>

typedef enum { TOUCH_NONE, TOUCH_STICK, TOUCH_BUTTON } TouchMode;

typedef struct {
    TouchMode mode;
    int prev_touch_count;
    Vector2 stick_origin;
    Vector2 stick_current;
    bool debug_button_triggered;
} TouchState;

/* Pure: process one frame of touch input given external sensor data. */
void touch_process(TouchState *state, int touch_count, Vector2 touch_pos, Rectangle debug_button_rect);

/* Thin raylib wrapper: reads touch state from raylib and calls touch_process. */
void touch_update(TouchState *state, Rectangle debug_button_rect);

/* Pure: returns stick direction as a Vector2 in [-1,1] each axis,
 * where max_radius pixels of drag maps to 1.0. Returns (0,0) if not in
 * stick mode or no drag. */
Vector2 touch_get_stick(const TouchState *state, float max_radius);

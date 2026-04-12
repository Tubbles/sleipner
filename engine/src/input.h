#pragma once

#include "alloc.h"
#include "debug.h"
#include "raylib.h"
#include <stdbool.h>

typedef struct {
    Vector2 left_stick;
    Vector2 right_stick;
    bool buttons[4];
    float left_trigger;
    float right_trigger;
} InputState;

void input_load_mappings(DebugState *dbg, Allocator *alloc, const char *data, int size);
InputState input_read(int gamepad_id);
InputState input_read_keyboard(void);
int input_count_gamepads(void);
bool input_exit_requested(int gamepad_id);

/* Pure helper: apply radial deadzone and clamp the output to the unit
 * disc. Exposed for unit testing. Returns (0, 0) when magnitude is at or
 * below the deadzone, so passing deadzone=0 is safe on a zero vector. */
Vector2 input_apply_deadzone(Vector2 stick, float deadzone);

#include "input.h"

#include "alloc.h"
#include "debug.h"
#include "raylib.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* --- Bit helpers for InputState fields ----------------------------------- */

static bool key_in_range(int key)
{
    return key > 0 && key < INPUT_MAX_KEY_CODE;
}

static bool gp_button_in_range(int button)
{
    return button > 0 && button < INPUT_GP_BUTTON_COUNT;
}

static bool gp_axis_in_range(int axis)
{
    return axis >= 0 && axis < INPUT_GP_AXIS_COUNT;
}

void input_capture(InputState *state)
{
    for (int word = 0; word < INPUT_KEY_BITSET_WORDS; word++) {
        state->key_down[word] = 0;
        state->key_pressed[word] = 0;
    }
    state->gp_button_down = 0;
    state->gp_button_pressed = 0;
    for (int axis = 0; axis < INPUT_GP_AXIS_COUNT; axis++) {
        state->gp_axis[axis] = 0.0F;
    }

    /* Keyboard: poll every code in our supported range. raylib silently
     * returns false for unsupported codes, so the loop is harmless on
     * platforms with smaller keyboards. */
    for (int key = 1; key < INPUT_MAX_KEY_CODE; key++) {
        if (IsKeyDown(key)) {
            state->key_down[key / INPUT_BITS_PER_WORD] |= (1ULL << (key % INPUT_BITS_PER_WORD));
        }
        if (IsKeyPressed(key)) {
            state->key_pressed[key / INPUT_BITS_PER_WORD] |= (1ULL << (key % INPUT_BITS_PER_WORD));
        }
    }

    /* Gamepad 0 only — multi-pad fan-out is future work. raylib returns
     * false / 0.0 for unavailable gamepads, so polling unconditionally is
     * safe; gating on IsGamepadAvailable here would also bypass the
     * test-input wrap shim for tests that drive gamepad input without
     * mocking IsGamepadAvailable. */
    state->gp_connected = IsGamepadAvailable(0);
    for (int button = 1; button < INPUT_GP_BUTTON_COUNT; button++) {
        if (IsGamepadButtonDown(0, button)) {
            state->gp_button_down |= (1U << button);
        }
        if (IsGamepadButtonPressed(0, button)) {
            state->gp_button_pressed |= (1U << button);
        }
    }
    for (int axis = 0; axis < INPUT_GP_AXIS_COUNT; axis++) {
        state->gp_axis[axis] = GetGamepadAxisMovement(0, axis);
    }
}

void input_state_press_key(InputState *state, int key)
{
    if (!key_in_range(key)) {
        return;
    }
    uint64_t mask = (1ULL << (key % INPUT_BITS_PER_WORD));
    state->key_down[key / INPUT_BITS_PER_WORD] |= mask;
    state->key_pressed[key / INPUT_BITS_PER_WORD] |= mask;
}

void input_state_hold_key(InputState *state, int key)
{
    if (!key_in_range(key)) {
        return;
    }
    state->key_down[key / INPUT_BITS_PER_WORD] |= (1ULL << (key % INPUT_BITS_PER_WORD));
}

void input_state_release_key(InputState *state, int key)
{
    if (!key_in_range(key)) {
        return;
    }
    uint64_t mask = (1ULL << (key % INPUT_BITS_PER_WORD));
    state->key_down[key / INPUT_BITS_PER_WORD] &= ~mask;
    state->key_pressed[key / INPUT_BITS_PER_WORD] &= ~mask;
}

void input_state_press_gp_button(InputState *state, int button)
{
    if (!gp_button_in_range(button)) {
        return;
    }
    state->gp_button_down |= (1U << button);
    state->gp_button_pressed |= (1U << button);
    state->gp_connected = true;
}

void input_state_hold_gp_button(InputState *state, int button)
{
    if (!gp_button_in_range(button)) {
        return;
    }
    state->gp_button_down |= (1U << button);
    state->gp_connected = true;
}

void input_state_release_gp_button(InputState *state, int button)
{
    if (!gp_button_in_range(button)) {
        return;
    }
    state->gp_button_down &= ~(1U << button);
    state->gp_button_pressed &= ~(1U << button);
}

void input_state_set_gp_axis(InputState *state, int axis, float value)
{
    if (!gp_axis_in_range(axis)) {
        return;
    }
    state->gp_axis[axis] = value;
    state->gp_connected = true;
}

void input_state_clear_edges(InputState *state)
{
    for (int word = 0; word < INPUT_KEY_BITSET_WORDS; word++) {
        state->key_pressed[word] = 0;
    }
    state->gp_button_pressed = 0;
}

bool input_key_pressed(const InputState *state, int key)
{
    if (!key_in_range(key)) {
        return false;
    }
    return (state->key_pressed[key / INPUT_BITS_PER_WORD] & (1ULL << (key % INPUT_BITS_PER_WORD))) != 0;
}

Vector2 input_apply_deadzone(Vector2 stick, float deadzone)
{
    float magnitude = sqrtf((stick.x * stick.x) + (stick.y * stick.y));
    if (magnitude <= deadzone) {
        return (Vector2){0, 0};
    }
    /* Clamp to the unit disc — sticks that report into the square corner
     * (magnitude up to sqrt(2)) or 8-way keyboard diagonals would
     * otherwise produce output vectors with magnitude > 1, making
     * diagonal movement faster than cardinal. */
    float clamped = (magnitude > 1.0F) ? 1.0F : magnitude;
    float mapped = (clamped - deadzone) / (1.0F - deadzone);
    float scale = mapped / magnitude;
    return (Vector2){stick.x * scale, stick.y * scale};
}

void input_load_mappings(DebugState *dbg, Allocator *alloc, const char *data, int size)
{
    char *mappings = alloc->malloc_fn(alloc->ctx, (size_t)size + 1);
    if (!mappings) {
        debug_log(dbg, "WARNING: could not allocate gamepad mappings buffer");
        return;
    }
    memcpy(mappings, data, (size_t)size);
    mappings[size] = '\0';

    int result = SetGamepadMappings(mappings);
    debug_log(dbg, "loaded gamepad mappings (%d bytes, result=%d)", size, result);
}

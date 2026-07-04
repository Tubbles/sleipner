#pragma once

#include "alloc.h"
#include "debug.h"
#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

/* Sized to fit raylib KEY_* codes (currently up to ~348). 8 * 64 = 512 bits. */
#define INPUT_KEY_BITSET_WORDS 8
#define INPUT_BITS_PER_WORD 64
#define INPUT_MAX_KEY_CODE (INPUT_KEY_BITSET_WORDS * INPUT_BITS_PER_WORD)
#define INPUT_GP_BUTTON_COUNT 32 /* raylib GAMEPAD_BUTTON_* enum max value (18 today, headroom for growth) */
#define INPUT_GP_AXIS_COUNT 6    /* raylib GAMEPAD_AXIS_* enum count */

/* Per-frame input snapshot consumed by the function layer (input_func.h).
 * key_down/pressed are bitsets indexed by raylib KEY_* codes.
 * gp_button_down/pressed are bitsets indexed by raylib GAMEPAD_BUTTON_*.
 * gp_axis is indexed by raylib GAMEPAD_AXIS_*. */
typedef struct {
    uint64_t key_down[INPUT_KEY_BITSET_WORDS];
    uint64_t key_pressed[INPUT_KEY_BITSET_WORDS];
    bool gp_connected;
    uint32_t gp_button_down;
    uint32_t gp_button_pressed;
    float gp_axis[INPUT_GP_AXIS_COUNT];
} InputState;

/* Fill `state` by polling raylib for gamepad 0 every frame. */
void input_capture(InputState *state);

/* --- Test helpers for the low-level fields ---
 * Tests construct an InputState directly (no linker wraps) and call these
 * to flip the appropriate bits. Calling these from production code is a
 * no-op functionally — they just write bits — but is not the intended use. */
void input_state_press_key(InputState *state, int key); /* edge: sets both _down and _pressed */
void input_state_hold_key(InputState *state, int key);  /* level: sets _down only */
void input_state_release_key(InputState *state, int key);
void input_state_press_gp_button(InputState *state, int button);
void input_state_hold_gp_button(InputState *state, int button);
void input_state_release_gp_button(InputState *state, int button);
void input_state_set_gp_axis(InputState *state, int axis, float value);
void input_state_clear_edges(InputState *state); /* clear *_pressed bits, retain *_down */

/* Raw key edge query, bypassing the BindingStore. For input that is not
 * worth promoting to a bound InputAction (e.g. the editor radial's digit
 * 1-9 direct-select, editor/widgets.c) but must still honor the same
 * InputState snapshot production and tests already drive (input_capture /
 * input_state_press_key), rather than reading raylib globals directly. */
[[nodiscard]] bool input_key_pressed(const InputState *state, int key);

/* Load the SDL gamecontrollerdb mapping table into raylib for analog
 * gamepad detection. Called once at startup. Distinct from the input
 * function layer; this is raylib device-mapping config, not key bindings. */
void input_load_mappings(DebugState *dbg, Allocator *alloc, const char *data, int size);

/* Pure helper: apply radial deadzone and clamp the output to the unit
 * disc. Exposed for unit testing. Returns (0, 0) when magnitude is at or
 * below the deadzone, so passing deadzone=0 is safe on a zero vector. */
Vector2 input_apply_deadzone(Vector2 stick, float deadzone);

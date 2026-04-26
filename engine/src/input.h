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

/* Per-frame input snapshot.
 *
 * Two parallel representations during the input system migration:
 *  - Low-level (key_down/pressed bitsets, gp_button_* bitsets, gp_axis floats):
 *    populated by input_capture(); consumed by the function layer (input_func.h).
 *  - High-level (left_stick / right_stick / buttons / triggers): populated by
 *    read_all_input() in main.c via input_read()/input_read_keyboard();
 *    consumed by legacy call sites yet to be migrated.
 *
 * The high-level fields are removed once all consumers move to the function
 * layer (see plans/parsed-floating-dolphin.md, stage 9). */
typedef struct {
    /* Low-level (function layer) */
    uint64_t key_down[INPUT_KEY_BITSET_WORDS];
    uint64_t key_pressed[INPUT_KEY_BITSET_WORDS];
    bool gp_connected;
    uint32_t gp_button_down;
    uint32_t gp_button_pressed;
    float gp_axis[INPUT_GP_AXIS_COUNT];

    /* High-level (legacy) */
    Vector2 left_stick;
    Vector2 right_stick;
    bool buttons[4];
    float left_trigger;
    float right_trigger;
} InputState;

/* Fill the low-level fields of `state` by polling raylib for gamepad 0.
 * Does not touch the high-level fields — call after read_all_input() so
 * both representations are populated in the same frame. */
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

/* --- Legacy API (removed in stage 9 of the input overhaul) --- */
void input_load_mappings(DebugState *dbg, Allocator *alloc, const char *data, int size);
InputState input_read(int gamepad_id);
InputState input_read_keyboard(void);
int input_count_gamepads(void);
bool input_exit_requested(int gamepad_id);

/* Pure helper: apply radial deadzone and clamp the output to the unit
 * disc. Exposed for unit testing. Returns (0, 0) when magnitude is at or
 * below the deadzone, so passing deadzone=0 is safe on a zero vector. */
Vector2 input_apply_deadzone(Vector2 stick, float deadzone);

#pragma once

/* Test-only mock for raylib input polling.
 *
 * The integration test binary (engine_tests) links with
 * -Wl,--wrap=IsKeyPressed,--wrap=IsGamepadButtonPressed, which redirects
 * every engine-side call to those raylib functions into the __wrap_*
 * functions defined in test_input_mock.c. Those wraps read from a small
 * mutable mock state declared here so integration tests can drive the
 * real editor input paths (e.g. handle_browse_input -> toggle_pressed)
 * without a window, without raylib's real input polling, and without
 * reaching past the abstraction layer into engine internals.
 *
 * All state is process-global because __wrap_* have raylib's native
 * signatures and can't take an extra context pointer. Call
 * test_input_reset() in setUp() to guarantee a clean slate.
 *
 * The mock faithfully models raylib's edge-triggered semantics: a "press"
 * fires IsKeyPressed / IsGamepadButtonPressed exactly once, then is
 * cleared on the next test_input_frame_advance() call — the same shape
 * as raylib clearing its per-frame edge state inside PollInputEvents. */

void test_input_reset(void);

/* Mark a key as pressed-this-frame. __wrap_IsKeyPressed(key) returns true
 * until the next test_input_frame_advance(). */
void test_input_press_key(int key);

/* Mark a gamepad button as pressed-this-frame. __wrap_IsGamepadButtonPressed
 * (pad, button) returns true until the next test_input_frame_advance(). */
void test_input_press_gamepad_button(int gamepad_id, int button);

/* Clear all edge-triggered bits. Call between simulated frames so a
 * single press fires exactly once, matching the real engine frame loop. */
void test_input_frame_advance(void);

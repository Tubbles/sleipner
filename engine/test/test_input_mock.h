#pragma once

/* Test-only mock for raylib input polling.
 *
 * The integration test binary (engine_tests) links with
 *     -Wl,--wrap=IsKeyPressed
 *     -Wl,--wrap=IsGamepadButtonPressed
 *     -Wl,--wrap=IsKeyDown
 *     -Wl,--wrap=IsGamepadButtonDown
 * which redirects every engine-side call to those raylib functions into
 * the __wrap_* functions defined in test_input_mock.c. Those wraps read
 * from a small mutable mock state declared here so integration tests can
 * drive the real editor input paths (e.g. handle_browse_input ->
 * toggle_pressed, handle_attr_edit_input -> binding_held) without a
 * window, without raylib's real input polling, and without reaching past
 * the abstraction layer into engine internals.
 *
 * All state is process-global because __wrap_* have raylib's native
 * signatures and can't take an extra context pointer. Call
 * test_input_reset() in setUp() to guarantee a clean slate.
 *
 * Edge vs level semantics match raylib:
 *   - test_input_press_*  -> IsKeyPressed/IsGamepadButtonPressed fire
 *                            true exactly once until the next
 *                            test_input_frame_advance().
 *   - test_input_hold_*   -> IsKeyDown/IsGamepadButtonDown stay true
 *                            until the matching test_input_release_*.
 *                            test_input_frame_advance() does NOT clear
 *                            held state; only edge-triggered bits.
 *   - test_input_tap_*    -> sets both Pressed (one frame) and Down
 *                            (one frame) for the next polled frame and
 *                            auto-clears on the next frame_advance.
 *                            This models a single-frame keypress that
 *                            both edge and level handlers must observe. */

void test_input_reset(void);

/* Mark a key as pressed-this-frame. __wrap_IsKeyPressed(key) returns true
 * until the next test_input_frame_advance(). */
void test_input_press_key(int key);

/* Mark a gamepad button as pressed-this-frame. __wrap_IsGamepadButtonPressed
 * (pad, button) returns true until the next test_input_frame_advance(). */
void test_input_press_gamepad_button(int gamepad_id, int button);

/* Mark a key as held. __wrap_IsKeyDown(key) returns true until
 * test_input_release_key(key) or test_input_reset(). Unlike presses,
 * held state survives test_input_frame_advance(). */
void test_input_hold_key(int key);
void test_input_release_key(int key);

/* Mark a gamepad button as held. Same semantics as test_input_hold_key. */
void test_input_hold_gamepad_button(int gamepad_id, int button);
void test_input_release_gamepad_button(int gamepad_id, int button);

/* Convenience: simulate a single-frame tap that fires both the edge
 * (Pressed) and the level (Down) on the same frame, then auto-clears on
 * test_input_frame_advance(). Use this for tests that exercise bindings
 * checked via either binding_pressed() or binding_held(). */
void test_input_tap_key(int key);
void test_input_tap_gamepad_button(int gamepad_id, int button);

/* Clear edge-triggered bits and any one-shot tap held bits. Held state
 * set via test_input_hold_* is preserved across frame_advance and only
 * cleared by an explicit release or reset. */
void test_input_frame_advance(void);

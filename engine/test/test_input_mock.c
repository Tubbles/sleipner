#include "test_input_mock.h"

#include <stdbool.h>
#include <string.h>

/* Raylib keys max out at KEY_KB_MENU = 348; 512 gives headroom without
 * chasing the exact enum. Gamepad buttons max at GAMEPAD_BUTTON_RIGHT_THUMB
 * = 17; 32 is headroom. Four gamepads matches what input.c polls. */
#define MOCK_KEY_CAPACITY 512
#define MOCK_GAMEPAD_COUNT 4
#define MOCK_GAMEPAD_BUTTON_CAPACITY 32

typedef struct {
    bool key_pressed[MOCK_KEY_CAPACITY];
    bool gamepad_button_pressed[MOCK_GAMEPAD_COUNT][MOCK_GAMEPAD_BUTTON_CAPACITY];
    bool key_down[MOCK_KEY_CAPACITY];
    bool gamepad_button_down[MOCK_GAMEPAD_COUNT][MOCK_GAMEPAD_BUTTON_CAPACITY];
    /* "Tap" = single-frame held bit released on the next frame_advance.
     * Tracked separately from the sticky key_down/gamepad_button_down so
     * frame_advance can clear taps without clobbering true held state. */
    bool key_tapped[MOCK_KEY_CAPACITY];
    bool gamepad_button_tapped[MOCK_GAMEPAD_COUNT][MOCK_GAMEPAD_BUTTON_CAPACITY];
} MockInputState;

static MockInputState mock_state;

void test_input_reset(void)
{
    memset(&mock_state, 0, sizeof(mock_state));
}

void test_input_press_key(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return;
    }
    mock_state.key_pressed[key] = true;
}

void test_input_press_gamepad_button(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return;
    }
    mock_state.gamepad_button_pressed[gamepad_id][button] = true;
}

void test_input_hold_key(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return;
    }
    mock_state.key_down[key] = true;
}

void test_input_release_key(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return;
    }
    mock_state.key_down[key] = false;
    mock_state.key_tapped[key] = false;
}

void test_input_hold_gamepad_button(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return;
    }
    mock_state.gamepad_button_down[gamepad_id][button] = true;
}

void test_input_release_gamepad_button(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return;
    }
    mock_state.gamepad_button_down[gamepad_id][button] = false;
    mock_state.gamepad_button_tapped[gamepad_id][button] = false;
}

void test_input_tap_key(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return;
    }
    mock_state.key_pressed[key] = true;
    mock_state.key_tapped[key] = true;
}

void test_input_tap_gamepad_button(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return;
    }
    mock_state.gamepad_button_pressed[gamepad_id][button] = true;
    mock_state.gamepad_button_tapped[gamepad_id][button] = true;
}

void test_input_frame_advance(void)
{
    memset(mock_state.key_pressed, 0, sizeof(mock_state.key_pressed));
    memset(mock_state.gamepad_button_pressed, 0, sizeof(mock_state.gamepad_button_pressed));
    memset(mock_state.key_tapped, 0, sizeof(mock_state.key_tapped));
    memset(mock_state.gamepad_button_tapped, 0, sizeof(mock_state.gamepad_button_tapped));
}

/* --- Linker wraps for raylib polling -------------------------------------
 *
 * The engine_tests target is linked with
 *     -Wl,--wrap=IsKeyPressed
 *     -Wl,--wrap=IsGamepadButtonPressed
 *     -Wl,--wrap=IsKeyDown
 *     -Wl,--wrap=IsGamepadButtonDown
 * which redirects every call from engine code into these functions.
 * Raylib itself is never initialised in tests (no InitWindow), so the
 * real symbols are unreachable and we do not forward to __real_*.
 *
 * The `__wrap_` prefix is mandated by the GNU ld --wrap mechanism and
 * cannot be renamed; the NOLINT block below suppresses the
 * reserved-identifier and naming-style warnings that follow from that. */

/* NOLINTBEGIN(readability-identifier-naming,cert-dcl37-c,cert-dcl51-cpp,bugprone-reserved-identifier) */
bool __wrap_IsKeyPressed(int key);
bool __wrap_IsGamepadButtonPressed(int gamepad_id, int button);
bool __wrap_IsKeyDown(int key);
bool __wrap_IsGamepadButtonDown(int gamepad_id, int button);

bool __wrap_IsKeyPressed(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return false;
    }
    return mock_state.key_pressed[key];
}

bool __wrap_IsGamepadButtonPressed(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return false;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return false;
    }
    return mock_state.gamepad_button_pressed[gamepad_id][button];
}

bool __wrap_IsKeyDown(int key)
{
    if (key < 0 || key >= MOCK_KEY_CAPACITY) {
        return false;
    }
    return mock_state.key_down[key] || mock_state.key_tapped[key];
}

bool __wrap_IsGamepadButtonDown(int gamepad_id, int button)
{
    if (gamepad_id < 0 || gamepad_id >= MOCK_GAMEPAD_COUNT) {
        return false;
    }
    if (button < 0 || button >= MOCK_GAMEPAD_BUTTON_CAPACITY) {
        return false;
    }
    return mock_state.gamepad_button_down[gamepad_id][button] || mock_state.gamepad_button_tapped[gamepad_id][button];
}
/* NOLINTEND(readability-identifier-naming,cert-dcl37-c,cert-dcl51-cpp,bugprone-reserved-identifier) */

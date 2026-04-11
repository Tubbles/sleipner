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

void test_input_frame_advance(void)
{
    memset(mock_state.key_pressed, 0, sizeof(mock_state.key_pressed));
    memset(mock_state.gamepad_button_pressed, 0, sizeof(mock_state.gamepad_button_pressed));
}

/* --- Linker wraps for raylib polling -------------------------------------
 *
 * The engine_tests target is linked with
 *     -Wl,--wrap=IsKeyPressed
 *     -Wl,--wrap=IsGamepadButtonPressed
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
/* NOLINTEND(readability-identifier-naming,cert-dcl37-c,cert-dcl51-cpp,bugprone-reserved-identifier) */

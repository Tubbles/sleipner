#include "input.h"
#include "debug.h"
#include "raylib.h"

#include <math.h>

#define STICK_DEADZONE 0.15F

static Vector2 apply_deadzone(Vector2 stick, float deadzone)
{
    float magnitude = sqrtf((stick.x * stick.x) + (stick.y * stick.y));
    if (magnitude < deadzone) {
        return (Vector2){0, 0};
    }
    float scale = ((magnitude - deadzone) / (1.0F - deadzone)) / magnitude;
    return (Vector2){stick.x * scale, stick.y * scale};
}

void input_load_mappings(const char *path)
{
    char *mappings = LoadFileText(path);
    if (!mappings) {
        debug_log("WARNING: could not load gamepad mappings from %s", path);
        return;
    }

    int result = SetGamepadMappings(mappings);
    debug_log("loaded gamepad mappings from %s (result=%d)", path, result);

    UnloadFileText(mappings);
}

InputState input_read(int gamepad_id)
{
    InputState state = {0};

    if (!IsGamepadAvailable(gamepad_id)) {
        return state;
    }

    Vector2 left = {GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_LEFT_X),
                    GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_LEFT_Y)};
    Vector2 right = {GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_RIGHT_X),
                     GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_RIGHT_Y)};
    state.left_stick = apply_deadzone(left, STICK_DEADZONE);
    state.right_stick = apply_deadzone(right, STICK_DEADZONE);

    state.buttons[0] = IsGamepadButtonPressed(gamepad_id, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    state.buttons[1] = IsGamepadButtonPressed(gamepad_id, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    state.buttons[2] = IsGamepadButtonPressed(gamepad_id, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    state.buttons[3] = IsGamepadButtonPressed(gamepad_id, GAMEPAD_BUTTON_RIGHT_FACE_UP);

    state.left_trigger = GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_LEFT_TRIGGER);
    state.right_trigger = GetGamepadAxisMovement(gamepad_id, GAMEPAD_AXIS_RIGHT_TRIGGER);

    /* Normalize triggers from -1..1 to 0..1 */
    state.left_trigger = (state.left_trigger + 1.0F) * 0.5F;
    state.right_trigger = (state.right_trigger + 1.0F) * 0.5F;

    return state;
}

InputState input_read_keyboard(void)
{
    InputState state = {0};

    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
        state.left_stick.x = -1.0F;
    }
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
        state.left_stick.x = 1.0F;
    }
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
        state.left_stick.y = -1.0F;
    }
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) {
        state.left_stick.y = 1.0F;
    }

    if (IsKeyDown(KEY_Q)) {
        state.right_stick.x = -1.0F;
    }
    if (IsKeyDown(KEY_E)) {
        state.right_stick.x = 1.0F;
    }

    state.buttons[0] = IsKeyPressed(KEY_SPACE);
    state.buttons[1] = IsKeyPressed(KEY_TAB);
    state.buttons[2] = IsKeyPressed(KEY_LEFT_SHIFT);
    state.buttons[3] = IsKeyPressed(KEY_LEFT_CONTROL);

    if (IsKeyDown(KEY_Z)) {
        state.left_trigger = 1.0F;
    }
    if (IsKeyDown(KEY_X)) {
        state.right_trigger = 1.0F;
    }

    return state;
}

int input_count_gamepads(void)
{
    int count = 0;
    for (int index = 0; index < 4; index++) {
        if (IsGamepadAvailable(index)) {
            count++;
        }
    }
    return count;
}

bool input_exit_requested(int gamepad_id)
{
    if (!IsGamepadAvailable(gamepad_id)) {
        return false;
    }
    return (bool)(IsGamepadButtonDown(gamepad_id, GAMEPAD_BUTTON_MIDDLE_LEFT) &&
                  IsGamepadButtonDown(gamepad_id, GAMEPAD_BUTTON_MIDDLE_RIGHT));
}

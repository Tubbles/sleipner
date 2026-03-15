#ifndef INPUT_H
#define INPUT_H

#include "raylib.h" // IWYU pragma: export
#include <stdbool.h>

typedef struct {
    Vector2 left_stick;
    Vector2 right_stick;
    bool buttons[4];
    float left_trigger;
    float right_trigger;
} InputState;

void input_load_mappings(const char *data, int size);
InputState input_read(int gamepad_id);
InputState input_read_keyboard(void);
int input_count_gamepads(void);
bool input_exit_requested(int gamepad_id);

#endif

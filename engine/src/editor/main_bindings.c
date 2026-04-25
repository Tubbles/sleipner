#include "editor/main_bindings.h"

#include "editor/keybindings.h"
#include "raylib.h"

const EditorBinding place_actions[PLACE_ACT_COUNT] = {
    [PLACE_ACT_CONFIRM] =
        {
            .binding = {KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN},
            .description = "Place",
        },
    [PLACE_ACT_CANCEL] =
        {
            .binding = {KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
            .description = "Cancel",
        },
    [PLACE_ACT_UP] =
        {
            .binding = {KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP},
            .description = "Prev",
        },
    [PLACE_ACT_DOWN] =
        {
            .binding = {KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN},
            .description = "Next",
        },
    [PLACE_ACT_PAGE_UP] =
        {
            .binding = {KEY_Q, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
            .description = "Page up",
        },
    [PLACE_ACT_PAGE_DOWN] =
        {
            .binding = {KEY_E, GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
            .description = "Page down",
        },
};

static const EditorBindingTable place_table = {
    .actions = place_actions,
    .count = PLACE_ACT_COUNT,
    .mode_label = "Place entity",
};

const EditorBindingTable *place_bindings(void)
{
    return &place_table;
}

const EditorBinding play_mode_actions[PLAY_ACT_COUNT] = {
    [PLAY_ACT_OPEN_MENU] =
        {
            .binding = {KEY_F3, GAMEPAD_BUTTON_MIDDLE_LEFT},
            .description = "Menu",
        },
    [PLAY_ACT_FONT_PREVIEW] =
        {
            .binding = {KEY_F4, GAMEPAD_BUTTON_RIGHT_THUMB},
            .description = "Fonts",
        },
    [PLAY_ACT_ENTER_EDITOR] =
        {
            .binding = {KEY_F5, GAMEPAD_BUTTON_MIDDLE_RIGHT},
            .description = "Editor",
        },
};

static const EditorBindingTable play_mode_table = {
    .actions = play_mode_actions,
    .count = PLAY_ACT_COUNT,
    .mode_label = "Play",
};

const EditorBindingTable *play_mode_bindings(void)
{
    return &play_mode_table;
}

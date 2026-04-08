#include "internal.h"

/* --- Blueprint list view --- */

static void handle_blueprint_list_input(GameState *state, EditorState *editor_state, InputState input)
{
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_list_scroll = 0;
        return;
    }

    int count = state->gamedata.blueprints.entries.count;
    if (count == 0) {
        return;
    }

    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        editor_state->blueprint_list_scroll = (editor_state->blueprint_list_scroll + 1) % count;
    }
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        editor_state->blueprint_list_scroll = (editor_state->blueprint_list_scroll - 1 + count) % count;
    }

    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        editor_state->selected_blueprint_index = editor_state->blueprint_list_scroll;
        editor_state->blueprint_attr_index = -1;
        editor_state->blueprint_tree_index = -1;
    }
}

/* --- Blueprint detail view --- */

static void handle_blueprint_detail_input(GameState *state,
                                          EditorState *editor_state,
                                          UndoHistory *undo_history,
                                          InputState input)
{
    (void)state;
    (void)undo_history;
    (void)input;

    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_attr_index = -1;
        editor_state->blueprint_tree_index = -1;
    }
}

/* --- Public --- */

void handle_blueprint_browse_input(GameState *state,
                                   EditorState *editor_state,
                                   UndoHistory *undo_history,
                                   InputState input)
{
    if (editor_state->selected_blueprint_index < 0) {
        handle_blueprint_list_input(state, editor_state, input);
    } else {
        handle_blueprint_detail_input(state, editor_state, undo_history, input);
    }
}

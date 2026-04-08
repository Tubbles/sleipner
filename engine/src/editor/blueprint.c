#include "internal.h"

#include <string.h>

/* --- Helpers --- */

static Blueprint *selected_blueprint(GameState *state, const EditorState *editor_state)
{
    int idx = editor_state->selected_blueprint_index;
    if (idx < 0 || idx >= state->gamedata.blueprints.entries.count) {
        return nullptr;
    }
    return &state->gamedata.blueprints.entries.data[idx];
}

static int blueprint_tree_total(const Blueprint *blueprint)
{
    return blueprint->children.count + 1; /* children + ADD CHILD sentinel */
}

static bool blueprint_tree_is_add_child(const Blueprint *blueprint, int tree_index)
{
    return tree_index == blueprint->children.count;
}

/* --- Navigation --- */

static void handle_blueprint_navigate(const Blueprint *blueprint, EditorState *editor_state, int direction)
{
    int tree_total = blueprint_tree_total(blueprint);
    int attr_count = blueprint->attrs.entries.count;
    int attr_sentinel = attr_count; /* the "+ADD" row */

    if (editor_state->blueprint_tree_index >= 0) {
        int new_tree = editor_state->blueprint_tree_index + direction;
        if (new_tree < 0) {
            editor_state->blueprint_tree_index = -1;
        } else if (new_tree >= tree_total) {
            editor_state->blueprint_tree_index = -1;
            editor_state->blueprint_attr_index = 0;
        } else {
            editor_state->blueprint_tree_index = new_tree;
        }
    } else if (editor_state->blueprint_attr_index >= 0) {
        int new_attr = editor_state->blueprint_attr_index + direction;
        if (new_attr < 0) {
            editor_state->blueprint_attr_index = -1;
            editor_state->blueprint_tree_index = tree_total - 1;
        } else if (new_attr > attr_sentinel) {
            editor_state->blueprint_attr_index = attr_sentinel;
        } else {
            editor_state->blueprint_attr_index = new_attr;
        }
    } else {
        if (direction > 0) {
            editor_state->blueprint_tree_index = 0;
        } else {
            editor_state->blueprint_attr_index = attr_sentinel;
        }
    }
}

/* --- Select (Enter) --- */

static void handle_blueprint_select(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }

    /* Tree section */
    if (editor_state->blueprint_tree_index >= 0) {
        if (blueprint_tree_is_add_child(blueprint, editor_state->blueprint_tree_index)) {
            fuzzy_finder_build_items(state, editor_state);
            editor_state->fuzzy_finder_scroll = 0;
            editor_state->adding_child = true;
            editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        }
        return;
    }

    /* Attr section */
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx < 0) {
        return;
    }
    int sentinel_index = blueprint->attrs.entries.count;
    if (attr_idx == sentinel_index) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->adding_blueprint_attr = true;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    if (attr_idx >= sentinel_index) {
        return;
    }
    Attribute *attr = &blueprint->attrs.entries.data[attr_idx];
    if (attr->type == ATTR_BOOL) {
        attr->value.b = !attr->value.b;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Toggle blueprint attr"));
    } else if (attr->type == ATTR_INT) {
        editor_state->saved_attr_int = attr->value.i;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (attr->type == ATTR_FLOAT) {
        editor_state->saved_attr_float = attr->value.f;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (attr->type == ATTR_STRING) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
    }
}

/* --- Cancel (Escape) --- */

static void handle_blueprint_cancel(EditorState *editor_state)
{
    if (editor_state->blueprint_attr_index >= 0) {
        editor_state->blueprint_attr_index = -1;
    } else if (editor_state->blueprint_tree_index >= 0) {
        editor_state->blueprint_tree_index = -1;
    } else {
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_attr_index = -1;
        editor_state->blueprint_tree_index = -1;
    }
}

/* --- Delete (X) --- */

static void handle_blueprint_delete(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }

    /* Tree section: X on child row → remove blueprint child */
    int tree_idx = editor_state->blueprint_tree_index;
    if (tree_idx >= 0) {
        if (!blueprint_tree_is_add_child(blueprint, tree_idx) && tree_idx < blueprint->children.count) {
            remove_blueprint_child(state, editor_state, undo_history, tree_idx);
        }
        return;
    }

    /* Attr section: X on attr → remove from blueprint */
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx >= 0 && attr_idx < blueprint->attrs.entries.count) {
        Attribute *target = &blueprint->attrs.entries.data[attr_idx];
        const char *attr_name = target->name.ptr;
        if (strcmp(attr_name, "name") == 0) {
            return; /* never remove the name attr */
        }
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        attr_remove(&alloc, &blueprint->attrs, attr_name);
        editor_state->blueprint_attr_index = -1;
        if (strcmp(attr_name, "collision_offset_x") == 0 || strcmp(attr_name, "collision_offset_y") == 0 ||
            strcmp(attr_name, "collision_w") == 0 || strcmp(attr_name, "collision_h") == 0) {
            propagate_collision_to_entities(state, blueprint);
        }
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Remove blueprint attr"));
    }
}

/* --- Type change radial --- */

static void open_type_radial(EditorState *editor_state)
{
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx < 0) {
        return;
    }
    /* Child props radial for tree section */
    if (editor_state->blueprint_tree_index >= 0) {
        editor_state->child_edit_index = editor_state->blueprint_tree_index;
        editor_state->radial_selected = -1;
        editor_state->radial_confirmed = -1;
        editor_state->radial_item_count = 3;
        editor_state->radial_context = RADIAL_CTX_CHILD_PROPS;
        editor_state->sub_mode = EDITOR_SUB_RADIAL;
        return;
    }
    /* Attr type radial */
    editor_state->radial_selected = -1;
    editor_state->radial_confirmed = -1;
    editor_state->radial_item_count = 4;
    editor_state->radial_context = RADIAL_CTX_ATTR_TYPE;
    editor_state->sub_mode = EDITOR_SUB_RADIAL;
}

/* --- Blueprint list view --- */

static void handle_blueprint_list_input(GameState *state, EditorState *editor_state, InputState input)
{
    (void)input;

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

static void
handle_blueprint_detail_input(GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input)
{
    (void)input;

    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        editor_state->selected_blueprint_index = -1;
        return;
    }

    /* Dispatch pending radial confirm */
    if (editor_state->radial_confirmed >= 0) {
        int confirmed = editor_state->radial_confirmed;
        editor_state->radial_confirmed = -1;
        if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
            dispatch_attr_type_change(state, editor_state, confirmed, undo_history);
        } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
            dispatch_child_props(state, editor_state, confirmed);
        }
        return;
    }

    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        handle_blueprint_cancel(editor_state);
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        handle_blueprint_navigate(blueprint, editor_state, 1);
    }
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        handle_blueprint_navigate(blueprint, editor_state, -1);
    }
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        handle_blueprint_select(state, editor_state, undo_history);
    }
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        handle_blueprint_delete(state, editor_state, undo_history);
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT_BRACKET, GAMEPAD_BUTTON_RIGHT_TRIGGER_2})) {
        open_type_radial(editor_state);
    }
    /* Undo / Redo */
    if (toggle_pressed((ToggleBinding){KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT})) {
        undo_history_step_back(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT})) {
        undo_history_step_forward(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
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

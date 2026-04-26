#include "editor/internal.h"

#include "alloc.h"
#include "editor/keybindings.h"
#include "input.h"
#include "input_func.h"
#include "str.h"
#include "strv.h"

#include <stdlib.h>
#include <string.h>

#define RADIX_DECIMAL 10

/* Forward decls for the HUD-hint table (still walked by editor/draw.c).
 * The binding fields are dead data after stage 6 of the input overhaul;
 * the description fields remain in use for the hints bar until stage 9
 * replaces the HUD layer wholesale. */
enum {
    ATTR_EDIT_ACT_CONFIRM,
    ATTR_EDIT_ACT_CANCEL,
    ATTR_EDIT_ACT_DEC_ONE,
    ATTR_EDIT_ACT_INC_ONE,
    ATTR_EDIT_ACT_DEC_TEN,
    ATTR_EDIT_ACT_INC_TEN,
    ATTR_EDIT_ACT_DEC_HUNDRED,
    ATTR_EDIT_ACT_INC_HUNDRED,
    ATTR_EDIT_ACT_COUNT
};

static const EditorBinding attr_edit_actions[ATTR_EDIT_ACT_COUNT];
static const EditorBindingTable attr_edit_table;

static int read_value_delta(const InputState *input, const BindingStore *bindings)
{
    int delta = 0;
    if (input_pressed(input, bindings, ACTION_ATTR_DEC_10)) {
        delta -= EDITOR_ATTR_LARGE_STEP;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_INC_10)) {
        delta += EDITOR_ATTR_LARGE_STEP;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_DEC_100)) {
        delta -= EDITOR_ATTR_HUGE_STEP;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_INC_100)) {
        delta += EDITOR_ATTR_HUGE_STEP;
    }
    return delta;
}

static int read_held_dir(const InputState *input, const BindingStore *bindings)
{
    if (input_held(input, bindings, ACTION_ATTR_DEC_1)) {
        return -1;
    }
    if (input_held(input, bindings, ACTION_ATTR_INC_1)) {
        return 1;
    }
    return 0;
}

static const AttrType attr_type_radial_order[] = {ATTR_FLOAT, ATTR_INT, ATTR_BOOL, ATTR_STRING};

typedef struct {
    float as_float;
    int as_int;
    bool as_bool;
} AttrConvertedValues;

static AttrConvertedValues attr_extract_values(const Attribute *attr, Allocator *alloc)
{
    (void)alloc;
    AttrConvertedValues result = {0};
    switch (attr->type) {
    case ATTR_FLOAT:
        result.as_float = attr->value.f;
        result.as_int = (int)attr->value.f;
        result.as_bool = attr->value.f != 0.0F;
        break;
    case ATTR_INT:
        result.as_float = (float)attr->value.i;
        result.as_int = attr->value.i;
        result.as_bool = attr->value.i != 0;
        break;
    case ATTR_BOOL:
        result.as_float = attr->value.b ? 1.0F : 0.0F;
        result.as_int = attr->value.b ? 1 : 0;
        result.as_bool = attr->value.b;
        break;
    case ATTR_STRING: {
        const char *text = (attr->value.str.ptr && attr->value.str.ptr[0] != '\0') ? attr->value.str.ptr : "0";
        result.as_float = (float)strtod(text, nullptr);
        result.as_int = (int)strtol(text, nullptr, RADIX_DECIMAL);
        result.as_bool = attr->value.str.len > 0;
        Str freed = attr->value.str;
        str_free(&freed);
        break;
    }
    }
    return result;
}

static void attr_apply_converted(Attribute *attr, AttrType target_type, AttrConvertedValues values, Allocator *alloc)
{
    if (target_type == ATTR_STRING) {
        const char *formatted = "";
        if (attr->type == ATTR_FLOAT) {
            formatted = TextFormat("%.2f", (double)values.as_float);
        } else if (attr->type == ATTR_INT) {
            formatted = TextFormat("%d", values.as_int);
        } else if (attr->type == ATTR_BOOL) {
            formatted = values.as_bool ? "true" : "false";
        }
        Str new_str = str_new(*alloc);
        (void)str_from_cstr(&new_str, formatted);
        attr->value.str = new_str;
    } else if (target_type == ATTR_FLOAT) {
        attr->value.f = values.as_float;
    } else if (target_type == ATTR_INT) {
        attr->value.i = values.as_int;
    } else if (target_type == ATTR_BOOL) {
        attr->value.b = values.as_bool;
    }
    attr->type = target_type;
}

static Attribute *active_edit_attr(GameState *state, const EditorState *editor_state)
{
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        int attr_idx = editor_state->blueprint_attr_index;
        if (bp_idx < 0 || bp_idx >= state->gamedata.blueprints.entries.count || attr_idx < 0) {
            return nullptr;
        }
        Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        if (attr_idx >= blueprint->attrs.entries.count) {
            return nullptr;
        }
        return &blueprint->attrs.entries.data[attr_idx];
    }
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count || attr_idx < 0) {
        return nullptr;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    return attr_at_display_index(state, entity, attr_idx);
}

void dispatch_attr_type_change(GameState *state, EditorState *editor_state, int confirmed, UndoHistory *undo_history)
{
    if (confirmed < 0 || confirmed >= 4) {
        return;
    }
    Attribute *attr = active_edit_attr(state, editor_state);
    if (!attr) {
        return;
    }
    AttrType target_type = attr_type_radial_order[confirmed];
    if (attr->type == target_type) {
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    AttrConvertedValues values = attr_extract_values(attr, &alloc);
    attr_apply_converted(attr, target_type, values, &alloc);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Change type"));
}

void dispatch_child_props(GameState *state, EditorState *editor_state, int confirmed)
{
    if (confirmed < 0) {
        return;
    }
    Blueprint *blueprint = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        if (bp_idx >= 0 && bp_idx < state->gamedata.blueprints.entries.count) {
            blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        }
    } else {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
            Entity *entity = &state->gamedata.current_level.entities.data[sel];
            blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        }
    }
    if (!blueprint || editor_state->child_edit_index < 0 ||
        editor_state->child_edit_index >= blueprint->children.count) {
        return;
    }
    BlueprintChild *child = &blueprint->children.data[editor_state->child_edit_index];
    if (confirmed == 0) { /* Tag */
        editor_state->word_builder_len = 0;
        editor_state->word_builder_buf[0] = '\0';
        if (child->tag.len > 0 && child->tag.len < WORD_BUILDER_BUF_SIZE) {
            memcpy(editor_state->word_builder_buf, child->tag.ptr, child->tag.len);
            editor_state->word_builder_buf[child->tag.len] = '\0';
            editor_state->word_builder_len = (int)child->tag.len;
        }
        editor_state->word_builder_scroll = 0;
        editor_state->editing_child_tag = true;
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
    } else if (confirmed == 1) { /* Offset X */
        editor_state->saved_attr_int = (int)child->offset.x;
        editor_state->editing_child_offset = true;
        editor_state->child_edit_axis = 0;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (confirmed == 2) { /* Offset Y */
        editor_state->saved_attr_int = (int)child->offset.y;
        editor_state->editing_child_offset = true;
        editor_state->child_edit_axis = 1;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    }
}

void confirm_child_tag_edit(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    (void)diag;
    Blueprint *blueprint = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        if (bp_idx >= 0 && bp_idx < state->gamedata.blueprints.entries.count) {
            blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        }
    } else {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
            Entity *entity = &state->gamedata.current_level.entities.data[sel];
            blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        }
    }
    int child_idx = editor_state->child_edit_index;
    if (!blueprint || child_idx < 0 || child_idx >= blueprint->children.count) {
        editor_state->editing_child_tag = false;
        return;
    }
    BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *old_tag = child->tag.len > 0 ? child->tag.ptr : "";
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    child->tag = str_new(alloc);
    (void)str_from_cstr(&child->tag, editor_state->word_builder_buf);
    propagate_child_tag(state, blueprint, child_idx, old_tag);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit child tag"));
    editor_state->editing_child_tag = false;
}

static void
confirm_child_offset_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int new_value)
{
    Blueprint *blueprint = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        if (bp_idx >= 0 && bp_idx < state->gamedata.blueprints.entries.count) {
            blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        }
    } else {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
            Entity *entity = &state->gamedata.current_level.entities.data[sel];
            blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        }
    }
    int child_idx = editor_state->child_edit_index;
    if (!blueprint || child_idx < 0 || child_idx >= blueprint->children.count) {
        editor_state->editing_child_offset = false;
        return;
    }
    BlueprintChild *child = &blueprint->children.data[child_idx];
    if (editor_state->child_edit_axis == 0) {
        child->offset.x = (float)new_value;
    } else {
        child->offset.y = (float)new_value;
    }
    propagate_child_offset(state, blueprint, child_idx);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit child offset"));
    editor_state->editing_child_offset = false;
}

static void apply_attr_delta(GameState *state, EditorState *editor_state, int delta)
{
    Attribute *attr = active_edit_attr(state, editor_state);
    if (!attr) {
        return;
    }
    if (attr->type == ATTR_INT) {
        attr->value.i += delta;
    } else if (attr->type == ATTR_FLOAT) {
        attr->value.f += (float)delta;
    }
}

static void reset_attr_hold(EditorState *editor_state)
{
    editor_state->attr_hold_total = 0.0F;
    editor_state->attr_hold_subtick = 0.0F;
    editor_state->attr_hold_dir = 0;
}

static void handle_child_offset_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history,
                                     const InputState *input, float delta_time)
{
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        confirm_child_offset_edit(state, editor_state, undo_history, editor_state->saved_attr_int);
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->editing_child_offset = false;
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int multi_delta = read_value_delta(input, &state->bindings);
    if (multi_delta != 0) {
        editor_state->saved_attr_int += multi_delta;
    }
    int held = read_held_dir(input, &state->bindings);
    if (held != editor_state->attr_hold_dir) {
        editor_state->attr_hold_dir = held;
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        if (held != 0) {
            editor_state->saved_attr_int += held;
        }
    }
    if (held == 0) {
        return;
    }
    editor_state->attr_hold_total += delta_time;
    editor_state->attr_hold_subtick += delta_time;
    if (editor_state->attr_hold_total < ATTR_REPEAT_DELAY) {
        return;
    }
    float hold_excess = editor_state->attr_hold_total - ATTR_REPEAT_DELAY;
    float period = ATTR_REPEAT_PERIOD / (1.0F + (ATTR_REPEAT_ACCEL * hold_excess));
    if (period < ATTR_REPEAT_MIN_PERIOD) {
        period = ATTR_REPEAT_MIN_PERIOD;
    }
    while (editor_state->attr_hold_subtick >= period) {
        editor_state->attr_hold_subtick -= period;
        editor_state->saved_attr_int += held;
    }
}

static void attr_edit_confirm(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit attribute"));
    reset_attr_hold(editor_state);
    editor_state->sub_mode = EDITOR_SUB_BROWSE;
}

void handle_attr_edit_input(GameState *state, EditorState *editor_state, UndoHistory *undo_history,
                            const InputState *input, float delta_time)
{
    if (editor_state->editing_child_offset) {
        handle_child_offset_edit(state, editor_state, undo_history, input, delta_time);
        return;
    }
    Attribute *current_attr = active_edit_attr(state, editor_state);
    if (!current_attr) {
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        attr_edit_confirm(state, editor_state, undo_history);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        if (current_attr->type == ATTR_INT) {
            current_attr->value.i = editor_state->saved_attr_int;
        } else if (current_attr->type == ATTR_FLOAT) {
            current_attr->value.f = editor_state->saved_attr_float;
        }
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int multi_delta = read_value_delta(input, &state->bindings);
    if (multi_delta != 0) {
        apply_attr_delta(state, editor_state, multi_delta);
    }
    int held = read_held_dir(input, &state->bindings);
    if (held != editor_state->attr_hold_dir) {
        editor_state->attr_hold_dir = held;
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        if (held != 0) {
            apply_attr_delta(state, editor_state, held);
        }
    }
    if (held == 0) {
        return;
    }
    editor_state->attr_hold_total += delta_time;
    editor_state->attr_hold_subtick += delta_time;
    if (editor_state->attr_hold_total < ATTR_REPEAT_DELAY) {
        return;
    }
    float hold_excess = editor_state->attr_hold_total - ATTR_REPEAT_DELAY;
    float period = ATTR_REPEAT_PERIOD / (1.0F + (ATTR_REPEAT_ACCEL * hold_excess));
    if (period < ATTR_REPEAT_MIN_PERIOD) {
        period = ATTR_REPEAT_MIN_PERIOD;
    }
    while (editor_state->attr_hold_subtick >= period) {
        editor_state->attr_hold_subtick -= period;
        apply_attr_delta(state, editor_state, held);
    }
}

/* --- Binding table --- */

static const EditorBinding attr_edit_actions[ATTR_EDIT_ACT_COUNT] = {
    [ATTR_EDIT_ACT_CONFIRM] =
        {
            .binding = {KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN},
            .description = "Confirm",
        },
    [ATTR_EDIT_ACT_CANCEL] =
        {
            .binding = {KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
            .description = "Cancel",
        },
    [ATTR_EDIT_ACT_DEC_ONE] =
        {
            .binding = {KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
            .description = "-1 (tap or hold)",
        },
    [ATTR_EDIT_ACT_INC_ONE] =
        {
            .binding = {KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
            .description = "+1 (tap or hold)",
        },
    [ATTR_EDIT_ACT_DEC_TEN] =
        {
            .binding = {KEY_LEFT_BRACKET, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
            .description = "-10",
        },
    [ATTR_EDIT_ACT_INC_TEN] =
        {
            .binding = {KEY_RIGHT_BRACKET, GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
            .description = "+10",
        },
    [ATTR_EDIT_ACT_DEC_HUNDRED] =
        {
            .binding = {KEY_PAGE_DOWN, GAMEPAD_BUTTON_LEFT_TRIGGER_2},
            .description = "-100",
        },
    [ATTR_EDIT_ACT_INC_HUNDRED] =
        {
            .binding = {KEY_PAGE_UP, GAMEPAD_BUTTON_RIGHT_TRIGGER_2},
            .description = "+100",
        },
};

static const EditorBindingTable attr_edit_table = {
    .actions = attr_edit_actions,
    .count = ATTR_EDIT_ACT_COUNT,
    .mode_label = "Adjust value",
};

const EditorBindingTable *attr_edit_bindings(void)
{
    return &attr_edit_table;
}

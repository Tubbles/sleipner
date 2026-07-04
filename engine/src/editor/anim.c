#include "editor/internal.h"

#include "alloc.h"
#include "strv.h"

#include <string.h>

/* --- Blueprint selection ---
 *
 * EDITOR_SUB_ANIM_EDIT double-duty (anim_blueprint_index < 0 shows the list
 * picker below, >=0 shows the params rows) mirrors atlas_texture_index's
 * split in editor/atlas.c. A separate field rather than reusing
 * selected_blueprint_index (Blueprint mode) follows the same precedent:
 * atlas_texture_index didn't reuse it either, keeping each top mode's
 * navigation history independent. */

static Blueprint *anim_selected_blueprint(GameState *state, const EditorState *editor_state)
{
    int index = editor_state->anim_blueprint_index;
    if (index < 0 || index >= state->gamedata.blueprints.entries.count) {
        return nullptr;
    }
    return &state->gamedata.blueprints.entries.data[index];
}

/* The Tools radial only opens from Scene-mode BROWSE (handle_browse_input,
 * editor/core.c, is the only caller that checks ACTION_EDITOR_OPEN_TOOLS),
 * so the only pre-existing selection reachable from enter_anim_mode is a
 * scene entity, never a Blueprint-mode selection. Mirrors
 * find_place_blueprint_index's (editor/core.c) entity->blueprint name
 * resolution, but returns -1 (not 0) when nothing is selected or the
 * blueprint can't be found, so enter_anim_mode falls back to the blueprint
 * list picker instead of silently guessing blueprint 0. */
static int anim_resolve_selected_entity_blueprint_index(const GameState *state, const EditorState *editor_state)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return -1;
    }
    const char *name = state->gamedata.current_level.entities.data[sel].blueprint_name.ptr;
    for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
        const char *bp_name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        if (bp_name && strcmp(bp_name, name) == 0) {
            return index;
        }
    }
    return -1;
}

void enter_anim_mode(GameState *state, EditorState *editor_state)
{
    editor_state->top_mode = EDITOR_TOP_ANIM;
    editor_state->sub_mode = EDITOR_SUB_ANIM_EDIT;
    editor_state->anim_blueprint_index = anim_resolve_selected_entity_blueprint_index(state, editor_state);
    editor_state->anim_blueprint_scroll =
        editor_state->anim_blueprint_index >= 0 ? editor_state->anim_blueprint_index : 0;
    editor_state->anim_edit_row = 0;
    editor_state->anim_frame_index = 0;
    editor_state->selected_entity_id = -1;
}

/* --- ANIM_EDIT: blueprint list (anim_blueprint_index < 0) --- */

static void handle_anim_blueprint_list_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int count = state->gamedata.blueprints.entries.count;
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->anim_blueprint_scroll < count - 1) {
        editor_state->anim_blueprint_scroll++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->anim_blueprint_scroll > 0) {
        editor_state->anim_blueprint_scroll--;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM) && count > 0) {
        editor_state->anim_blueprint_index = editor_state->anim_blueprint_scroll;
        editor_state->anim_edit_row = 0;
        editor_state->anim_frame_index = 0;
    }
}

/* --- ANIM_EDIT: params rows (anim_blueprint_index >= 0) --- */

typedef enum {
    ANIM_EDIT_ROW_FRAMES,
    ANIM_EDIT_ROW_SIZE,
    ANIM_EDIT_ROW_SPEED,
    ANIM_EDIT_ROW_ROW,
    ANIM_EDIT_ROW_COUNT,
} AnimEditRow;

static const char *const anim_edit_row_labels[ANIM_EDIT_ROW_COUNT] = {"Frames", "Size", "Speed", "Row"};
static const char *const anim_edit_attr_names[ANIM_EDIT_ROW_COUNT] = {
    "anim_frames",
    "anim_size",
    "anim_speed",
    "anim_row",
};

/* Defaults per S5.5: a blueprint with no anim_* attrs yet starts from
 * these when the row is first bumped, rather than 0/0/0/0 (frames=0 or
 * size=0 would make the animation and its src rect degenerate). */
static int anim_edit_default(int row)
{
    switch (row) {
    case ANIM_EDIT_ROW_FRAMES:
        return 1;
    case ANIM_EDIT_ROW_SIZE:
        return 16;
    case ANIM_EDIT_ROW_SPEED:
        return 1;
    default: /* ANIM_EDIT_ROW_ROW */
        return 0;
    }
}

static int anim_edit_get(const Blueprint *blueprint, int row)
{
    return attr_get_int(&blueprint->attrs, anim_edit_attr_names[row], anim_edit_default(row));
}

/* frames/speed/row floor at 0, size floors at 1 (a zero-size src rect is
 * degenerate) -- matches level_detail_clamp's per-row floor shape
 * (editor/level.c). */
static int anim_edit_clamp(int row, int value)
{
    int floor_value = (row == ANIM_EDIT_ROW_SIZE) ? 1 : 0;
    return value < floor_value ? floor_value : value;
}

static void anim_edit_set(GameState *state, Blueprint *blueprint, int row, int value)
{
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    (void)attr_set_int(&alloc, &blueprint->attrs, anim_edit_attr_names[row], anim_edit_clamp(row, value));
}

static void handle_anim_edit_navigate(EditorState *editor_state, int direction)
{
    int new_row = editor_state->anim_edit_row + direction;
    if (new_row < 0) {
        new_row = 0;
    } else if (new_row >= ANIM_EDIT_ROW_COUNT) {
        new_row = ANIM_EDIT_ROW_COUNT - 1;
    }
    editor_state->anim_edit_row = new_row;
}

/* Reads the same +/-1 / +/-10 attr-step actions the value adjuster uses
 * (CLAUDE.md Input Function Layer), applied directly to the focused row --
 * same immediate-commit-per-press shape as read_level_detail_delta
 * (editor/level.c), no held-value/cancel-to-revert session. */
static int read_anim_edit_delta(const InputState *input, const BindingStore *bindings)
{
    int delta = 0;
    if (input_pressed(input, bindings, ACTION_ATTR_DEC_1)) {
        delta -= 1;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_INC_1)) {
        delta += 1;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_DEC_10)) {
        delta -= EDITOR_ATTR_LARGE_STEP;
    }
    if (input_pressed(input, bindings, ACTION_ATTR_INC_10)) {
        delta += EDITOR_ATTR_LARGE_STEP;
    }
    return delta;
}

static void apply_anim_edit_delta(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int delta)
{
    if (delta == 0) {
        return;
    }
    Blueprint *blueprint = anim_selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }
    int row = editor_state->anim_edit_row;
    anim_edit_set(state, blueprint, row, anim_edit_get(blueprint, row) + delta);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit animation"));
}

static int anim_frame_count(const Blueprint *blueprint)
{
    return attr_get_int(&blueprint->attrs, "anim_frames", 1);
}

static void clamp_anim_frame_index(EditorState *editor_state, int frame_count)
{
    if (frame_count <= 0) {
        editor_state->anim_frame_index = 0;
        return;
    }
    if (editor_state->anim_frame_index < 0) {
        editor_state->anim_frame_index = 0;
    } else if (editor_state->anim_frame_index > frame_count - 1) {
        editor_state->anim_frame_index = frame_count - 1;
    }
}

static void handle_anim_params_input(GameState *state,
                                     EditorState *editor_state,
                                     UndoHistory *undo_history,
                                     const InputState *input)
{
    /* TAB switches to the frame scrubber; checked first per the universal
     * tab-switch convention (CLAUDE.md Input Function Layer /
     * settings.c handle_list_input), since TAB_NEXT/PREV share L1/R1 with
     * ACTION_ATTR_INC_10/DEC_10. Only two "tabs" exist here, so either
     * direction toggles the same way Tile mode's single TAB_NEXT toggles
     * its two layers (editor/tile.c). */
    if (input_pressed(input, &state->bindings, ACTION_TAB_PREV) ||
        input_pressed(input, &state->bindings, ACTION_TAB_NEXT)) {
        Blueprint *blueprint = anim_selected_blueprint(state, editor_state);
        clamp_anim_frame_index(editor_state, blueprint ? anim_frame_count(blueprint) : 0);
        editor_state->sub_mode = EDITOR_SUB_ANIM_FRAMES;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->anim_blueprint_index = -1;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN)) {
        handle_anim_edit_navigate(editor_state, 1);
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP)) {
        handle_anim_edit_navigate(editor_state, -1);
    }
    int delta = read_anim_edit_delta(input, &state->bindings);
    apply_anim_edit_delta(state, editor_state, undo_history, delta);
}

void handle_anim_edit_input(GameState *state,
                            EditorState *editor_state,
                            UndoHistory *undo_history,
                            const InputState *input)
{
    if (editor_state->anim_blueprint_index < 0) {
        handle_anim_blueprint_list_input(state, editor_state, input);
    } else {
        handle_anim_params_input(state, editor_state, undo_history, input);
    }
}

/* --- ANIM_FRAMES: scrubber --- */

void handle_anim_frames_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_TAB_PREV) ||
        input_pressed(input, &state->bindings, ACTION_TAB_NEXT) ||
        input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_ANIM_EDIT;
        return;
    }
    Blueprint *blueprint = anim_selected_blueprint(state, editor_state);
    int frame_count = blueprint ? anim_frame_count(blueprint) : 0;
    if (input_pressed(input, &state->bindings, ACTION_NAV_LEFT)) {
        editor_state->anim_frame_index--;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_RIGHT)) {
        editor_state->anim_frame_index++;
    }
    clamp_anim_frame_index(editor_state, frame_count);
}

/* --- Draw --- */

/* Const-correct texture-by-filename lookup, duplicated from tile.c's
 * find_tile_texture (see that function's doc comment): every draw_*_panel
 * function takes a const GameState *, so it can't call game.c's
 * texture_registry_lookup (which takes a non-const void * user_data). */
static const Texture2D *find_anim_texture(const GameState *state, const char *filename)
{
    for (int index = 0; index < state->assets.textures.count; index++) {
        if (strcmp(state->assets.textures.data[index].filename, filename) == 0) {
            return &state->assets.textures.data[index].texture;
        }
    }
    return nullptr;
}

/* Frame i's source rect on the blueprint's texture: a horizontal strip at
 * row anim_row (D20/S3.4 data layout, see CLAUDE.md task brief). Caller is
 * expected to have already clamped frame_index to [0, anim_frames). */
static Rectangle anim_frame_src_rect(int frame_index, int anim_size, int anim_row)
{
    float size = (float)anim_size;
    return (Rectangle){(float)frame_index * size, (float)anim_row * size, size, size};
}

static void draw_anim_blueprint_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int count = state->gamedata.blueprints.entries.count;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Animation: Pick Blueprint ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    if (count == 0) {
        draw_ui_text(font, "(no blueprints)", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                     debug_text_color);
        return;
    }

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->anim_blueprint_scroll - (visible / 2);
    if (scroll < 0) {
        scroll = 0;
    }
    int max_scroll = count - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    int end = scroll + visible;
    if (end > count) {
        end = count;
    }
    for (int index = scroll; index < end; index++) {
        bool selected = (index == editor_state->anim_blueprint_scroll);
        Color color = selected ? WHITE : debug_text_color;
        const char *name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", name ? name : "?"), panel_x + DEBUG_MARGIN,
                     y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void draw_anim_edit_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int bp_index = editor_state->anim_blueprint_index;
    if (bp_index < 0 || bp_index >= state->gamedata.blueprints.entries.count) {
        return;
    }
    const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_index];
    const char *name = attr_get_string(&blueprint->attrs, "name");
    Font font = state->assets.ui_font;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ %s: Animation ]", name ? name : "?"), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    for (int row = 0; row < ANIM_EDIT_ROW_COUNT; row++) {
        bool selected = (row == editor_state->anim_edit_row);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(
            font,
            TextFormat("%s %s: %d", selected ? ">" : " ", anim_edit_row_labels[row], anim_edit_get(blueprint, row)),
            panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

#define ANIM_FRAME_PREVIEW_SIZE 64 /* screen px: scaled-up frame preview in the FRAMES side panel */

static void draw_anim_frames_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int bp_index = editor_state->anim_blueprint_index;
    if (bp_index < 0 || bp_index >= state->gamedata.blueprints.entries.count) {
        return;
    }
    const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_index];
    const char *name = attr_get_string(&blueprint->attrs, "name");
    Font font = state->assets.ui_font;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ %s: Frames ]", name ? name : "?"), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int frame_count = anim_frame_count(blueprint);
    int anim_size = attr_get_int(&blueprint->attrs, "anim_size", 16);
    int anim_row = attr_get_int(&blueprint->attrs, "anim_row", 0);
    draw_ui_text(font, TextFormat("frame: %d / %d", editor_state->anim_frame_index, frame_count),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    const char *texture_name = attr_get_string(&blueprint->attrs, "texture");
    const Texture2D *texture = texture_name ? find_anim_texture(state, texture_name) : nullptr;
    if (texture) {
        Rectangle source = anim_frame_src_rect(editor_state->anim_frame_index, anim_size, anim_row);
        Rectangle dest = {(float)(panel_x + DEBUG_MARGIN), (float)y_offset, ANIM_FRAME_PREVIEW_SIZE,
                          ANIM_FRAME_PREVIEW_SIZE};
        DrawTexturePro(*texture, source, dest, (Vector2){0, 0}, 0.0F, WHITE);
    }
}

/* Single entry point for main.c's draw_active_editor_panel dispatch,
 * folding the sub_mode/anim_blueprint_index three-way branch above into one
 * call so a new top_mode's dispatch doesn't push draw_active_editor_panel's
 * own cognitive complexity over the readability-function-cognitive-
 * complexity threshold (same rationale as that function's own doc comment
 * in main.c). */
void draw_anim_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode == EDITOR_SUB_ANIM_FRAMES) {
        draw_anim_frames_panel(screen, state, editor_state);
    } else if (editor_state->anim_blueprint_index >= 0) {
        draw_anim_edit_panel(screen, state, editor_state);
    } else {
        draw_anim_blueprint_list_panel(screen, state, editor_state);
    }
}

/* --- Hint tables --- */

static const EditorActionHint anim_blueprint_list_hints[] = {
    {ACTION_CANCEL, "Back to scene"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Select"},
};

static const EditorHintTable anim_blueprint_list_table = {
    .hints = anim_blueprint_list_hints,
    .count = (int)(sizeof(anim_blueprint_list_hints) / sizeof(anim_blueprint_list_hints[0])),
    .mode_label = "Animation: pick blueprint",
};

const EditorHintTable *anim_blueprint_list_hints_table(void)
{
    return &anim_blueprint_list_table;
}

static const EditorActionHint anim_edit_hints[] = {
    {ACTION_CANCEL, "Back to list"}, {ACTION_NAV_UP, "Prev field"}, {ACTION_NAV_DOWN, "Next field"},
    {ACTION_ATTR_DEC_1, "-1"},       {ACTION_ATTR_INC_1, "+1"},     {ACTION_ATTR_DEC_10, "-10"},
    {ACTION_ATTR_INC_10, "+10"},     {ACTION_TAB_NEXT, "Frames"},
};

static const EditorHintTable anim_edit_table = {
    .hints = anim_edit_hints,
    .count = (int)(sizeof(anim_edit_hints) / sizeof(anim_edit_hints[0])),
    .mode_label = "Animation edit",
};

const EditorHintTable *anim_edit_hints_table(void)
{
    return &anim_edit_table;
}

static const EditorActionHint anim_frames_hints[] = {
    {ACTION_TAB_PREV, "Edit"},
    {ACTION_NAV_LEFT, "Prev frame"},
    {ACTION_NAV_RIGHT, "Next frame"},
    {ACTION_CANCEL, "Back to edit"},
};

static const EditorHintTable anim_frames_table = {
    .hints = anim_frames_hints,
    .count = (int)(sizeof(anim_frames_hints) / sizeof(anim_frames_hints[0])),
    .mode_label = "Animation frames",
};

const EditorHintTable *anim_frames_hints_table(void)
{
    return &anim_frames_table;
}

#include "editor/internal.h"

#include "str.h"
#include "strv.h"

/* --- Helpers --- */

/* Combined list shown in Level mode: slot 0 is current_level, slots
 * 1..other_levels.count are other_levels[0..count-1]. */
static int level_list_total(const GameState *state)
{
    return 1 + state->gamedata.other_levels.count;
}

static Strv level_list_name_at(const GameState *state, int index)
{
    if (index == 0) {
        return str_to_strv(state->gamedata.current_level.name);
    }
    return str_to_strv(state->gamedata.other_levels.data[index - 1].name);
}

static int level_list_entity_count_at(const GameState *state, int index)
{
    if (index == 0) {
        return state->gamedata.current_level.entities.count;
    }
    return state->gamedata.other_levels.data[index - 1].entities.count;
}

/* --- Confirm (switch) --- */

/* CONFIRM on the focused row. Switching to the already-current level is a
 * no-op. Otherwise gated by a dirty-check: the first CONFIRM while undo
 * history is dirty only arms level_switch_confirm_pending and shows a
 * toast; a second CONFIRM (or any CONFIRM while clean) commits the switch
 * via level_activate, then resets undo history for the new context the
 * same way handle_transition/reset_editor_after_reload do after a
 * game_load_gamedata call — level_activate itself does not touch undo
 * history, since game.c has no undo.h dependency. */
static void confirm_level_switch(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Strv target_name = level_list_name_at(state, editor_state->level_list_scroll);
    if (strv_eq(target_name, str_to_strv(state->gamedata.current_level.name))) {
        editor_state->level_switch_confirm_pending = false;
        return;
    }

    if (undo_history_is_dirty(undo_history) && !editor_state->level_switch_confirm_pending) {
        editor_state->level_switch_confirm_pending = true;
        editor_state->toast_text = strv_from_cstr("Unsaved changes, press again to switch");
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    editor_state->level_switch_confirm_pending = false;

    if (!level_activate(diag, state, target_name)) {
        editor_state->toast_text = strv_from_cstr("Level not found");
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }

    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Switch level"));

    /* Land back in Scene mode so the user sees the newly-active level's
     * entities. Selection is entity-id keyed against the OLD level, so it
     * must be cleared rather than carried over. */
    editor_state->top_mode = EDITOR_TOP_SCENE;
    editor_state->level_list_scroll = 0;
    editor_state->selected_entity_id = -1;
    editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
    editor_state->selected_tree_index = -1;
}

/* --- Public --- */

void handle_level_browse_input(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->level_list_scroll = 0;
        editor_state->level_switch_confirm_pending = false;
        return;
    }

    int total = level_list_total(state);
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN)) {
        editor_state->level_list_scroll = (editor_state->level_list_scroll + 1) % total;
        editor_state->level_switch_confirm_pending = false;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP)) {
        editor_state->level_list_scroll = (editor_state->level_list_scroll - 1 + total) % total;
        editor_state->level_switch_confirm_pending = false;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        confirm_level_switch(diag, state, editor_state, undo_history);
    }
}

void draw_level_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Level Mode ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int total = level_list_total(state);
    int visible = place_visible_count(screen.height);
    int scroll = editor_state->level_list_scroll - (visible / 2);
    if (scroll < 0) {
        scroll = 0;
    }
    int max_scroll = total - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    int end = scroll + visible;
    if (end > total) {
        end = total;
    }
    for (int index = scroll; index < end; index++) {
        bool selected = (index == editor_state->level_list_scroll);
        Color color = selected ? WHITE : debug_text_color;
        Strv name = level_list_name_at(state, index);
        const char *marker = (index == 0) ? "*" : " ";
        draw_ui_text(font,
                     TextFormat("%s%s %.*s  (%d entities)", selected ? ">" : " ", marker, (int)name.len, name.ptr,
                                level_list_entity_count_at(state, index)),
                     panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

/* --- Hint table --- */

static const EditorActionHint level_list_hints[] = {
    {ACTION_CANCEL, "Back to scene"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_CONFIRM, "Switch"},
};

static const EditorHintTable level_list_table = {
    .hints = level_list_hints,
    .count = (int)(sizeof(level_list_hints) / sizeof(level_list_hints[0])),
    .mode_label = "Level list",
};

const EditorHintTable *level_list_hints_table(void)
{
    return &level_list_table;
}

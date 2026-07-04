#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "str.h"
#include "strv.h"

#include <string.h>

/* Defaults for a freshly created level (S5.2b). Floor size mirrors the
 * level size unless authored otherwise, same convention level_load
 * applies when parsing a level with no floor_size key. */
#define LEVEL_DEFAULT_WIDTH 640
#define LEVEL_DEFAULT_HEIGHT 360

/* --- Helpers --- */

/* Combined list shown in Level mode: slot 0 is current_level, slots
 * 1..other_levels.count are other_levels[0..count-1], and the final
 * slot is the "+ NEW LEVEL" sentinel. */
static int level_list_total(const GameState *state)
{
    return 1 + state->gamedata.other_levels.count + 1;
}

static bool level_list_is_new_sentinel(const GameState *state, int index)
{
    return index == level_list_total(state) - 1;
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

/* --- New level creation --- */

static bool level_name_in_use(const GameState *state, Strv name)
{
    if (strv_eq(name, str_to_strv(state->gamedata.current_level.name))) {
        return true;
    }
    for (int index = 0; index < state->gamedata.other_levels.count; index++) {
        if (strv_eq(name, str_to_strv(state->gamedata.other_levels.data[index].name))) {
            return true;
        }
    }
    return false;
}

static void toast_level_error(EditorState *editor_state, const char *message)
{
    editor_state->toast_text = strv_from_cstr(message);
    editor_state->toast_timer = TOAST_DURATION;
}

/* Build a Level with the S5.2b defaults, append it to other_levels, and
 * activate it via level_activate — the same swap-in mechanism
 * confirm_level_switch uses, so the new (empty) level becomes current
 * and the previously-current level lands in other_levels. Rejects an
 * empty or already-used name without touching gamedata. */
void create_new_level(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name)
{
    if (!name || name[0] == '\0') {
        toast_level_error(editor_state, "Level name required");
        return;
    }
    Strv name_view = strv_from_cstr(name);
    if (level_name_in_use(state, name_view)) {
        toast_level_error(editor_state, "Level name already exists");
        return;
    }

    Allocator alloc = allocator_arena(&state->gamedata_arena);
    Level new_level = {0};
    new_level.name = str_new(alloc);
    new_level.music_name = str_new(alloc);
    new_level.background_tile = str_new(alloc);
    if (!str_from_cstr(&new_level.name, name) || !str_from_cstr(&new_level.background_tile, "grass.png")) {
        toast_level_error(editor_state, "Level creation failed");
        return;
    }
    new_level.background_tint = WHITE;
    new_level.width = LEVEL_DEFAULT_WIDTH;
    new_level.height = LEVEL_DEFAULT_HEIGHT;
    new_level.floor_width = LEVEL_DEFAULT_WIDTH;
    new_level.floor_height = LEVEL_DEFAULT_HEIGHT;
    new_level.next_entity_id = 0;
    new_level.entities = vec_entity_new(alloc);
    /* Tile grid (D36): sized from the S5.2b defaults above, layers left
     * empty so the ground layer falls back to the flat background_tile
     * fill until the editor's Tile mode paints something. Without this,
     * a freshly created level would carry a degenerate 0x0 grid and a
     * zero-valued (unusable) vec_int allocator. */
    new_level.tiles_wide = level_tiles_wide(LEVEL_DEFAULT_WIDTH);
    new_level.tiles_high = level_tiles_high(LEVEL_DEFAULT_HEIGHT);
    new_level.tiles_ground = vec_int_new(alloc);
    new_level.tiles_overlay = vec_int_new(alloc);

    if (!vec_level_push(&state->gamedata.other_levels, new_level)) {
        toast_level_error(editor_state, "Out of memory");
        return;
    }
    if (!level_activate(diag, state, name_view)) {
        toast_level_error(editor_state, "Level creation failed");
        return;
    }

    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Create level"));

    /* Land back in Scene mode so the user sees the new, empty level —
     * mirrors confirm_level_switch's tail below. */
    editor_state->top_mode = EDITOR_TOP_SCENE;
    editor_state->level_list_scroll = 0;
    editor_state->level_detail_open = false;
    editor_state->level_detail_row = 0;
    editor_state->selected_entity_id = -1;
    editor_state->multiselect_count = 0;
    editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
    editor_state->selected_tree_index = -1;
}

/* --- Level detail rows --- */

typedef enum {
    LEVEL_DETAIL_ROW_WIDTH,
    LEVEL_DETAIL_ROW_HEIGHT,
    LEVEL_DETAIL_ROW_FLOOR_WIDTH,
    LEVEL_DETAIL_ROW_FLOOR_HEIGHT,
    LEVEL_DETAIL_ROW_BACKGROUND_TILE,
    LEVEL_DETAIL_ROW_TINT_R,
    LEVEL_DETAIL_ROW_TINT_G,
    LEVEL_DETAIL_ROW_TINT_B,
    LEVEL_DETAIL_ROW_MUSIC,
    LEVEL_DETAIL_ROW_COUNT,
} LevelDetailRow;

static const char *const level_detail_row_labels[LEVEL_DETAIL_ROW_COUNT] = {
    "Width", "Height", "Floor width", "Floor height", "Background tile", "Tint R", "Tint G", "Tint B", "Music",
};

static bool level_detail_row_is_string(int row)
{
    return row == LEVEL_DETAIL_ROW_BACKGROUND_TILE || row == LEVEL_DETAIL_ROW_MUSIC;
}

/* Non-owning: the Str lives in gamedata_arena and outlives this call.
 * Const/non-const pair mirrors attr_set_for_section_const / attr_section_set
 * in editor/core.c — draw_level_detail_panel only reads, confirm_level_string_edit
 * writes. */
static const Str *level_detail_string_field_const(const Level *level, int row)
{
    return row == LEVEL_DETAIL_ROW_BACKGROUND_TILE ? &level->background_tile : &level->music_name;
}

static Str *level_detail_string_field(Level *level, int row)
{
    return row == LEVEL_DETAIL_ROW_BACKGROUND_TILE ? &level->background_tile : &level->music_name;
}

static int level_detail_int_get(const Level *level, int row)
{
    switch (row) {
    case LEVEL_DETAIL_ROW_WIDTH:
        return level->width;
    case LEVEL_DETAIL_ROW_HEIGHT:
        return level->height;
    case LEVEL_DETAIL_ROW_FLOOR_WIDTH:
        return level->floor_width;
    case LEVEL_DETAIL_ROW_FLOOR_HEIGHT:
        return level->floor_height;
    case LEVEL_DETAIL_ROW_TINT_R:
        return level->background_tint.r;
    case LEVEL_DETAIL_ROW_TINT_G:
        return level->background_tint.g;
    case LEVEL_DETAIL_ROW_TINT_B:
        return level->background_tint.b;
    default:
        return 0;
    }
}

/* Sizes stay positive (width/height/floor_width/floor_height >= 1);
 * tint channels clamp to the byte range Color.r/g/b actually store.
 * `row` takes the enum type rather than `int` for self-documentation,
 * but clang-tidy still flags the pair as swappable because the enum is
 * int-convertible — same false-positive shape as emit_collision_kind's
 * NOLINT in toml_emitter.c. Reordering doesn't help with only two
 * parameters (nothing to separate them with). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int level_detail_clamp(LevelDetailRow row, int value)
{
    if (row == LEVEL_DETAIL_ROW_TINT_R || row == LEVEL_DETAIL_ROW_TINT_G || row == LEVEL_DETAIL_ROW_TINT_B) {
        if (value < 0) {
            return 0;
        }
        if (value > 255) {
            return 255;
        }
        return value;
    }
    return value < 1 ? 1 : value;
}

static void level_detail_int_set(Level *level, int row, int value)
{
    int clamped = level_detail_clamp(row, value);
    switch (row) {
    case LEVEL_DETAIL_ROW_WIDTH:
        level->width = clamped;
        break;
    case LEVEL_DETAIL_ROW_HEIGHT:
        level->height = clamped;
        break;
    case LEVEL_DETAIL_ROW_FLOOR_WIDTH:
        level->floor_width = clamped;
        break;
    case LEVEL_DETAIL_ROW_FLOOR_HEIGHT:
        level->floor_height = clamped;
        break;
    case LEVEL_DETAIL_ROW_TINT_R:
        level->background_tint.r = (unsigned char)clamped;
        break;
    case LEVEL_DETAIL_ROW_TINT_G:
        level->background_tint.g = (unsigned char)clamped;
        break;
    case LEVEL_DETAIL_ROW_TINT_B:
        level->background_tint.b = (unsigned char)clamped;
        break;
    default:
        break;
    }
}

/* Reads the same +/-1 / +/-10 attr-step actions the value adjuster uses
 * (CLAUDE.md Input Function Layer), but applies the delta directly to
 * the focused row instead of routing through EDITOR_SUB_ATTR_EDIT's
 * session state machine — there is no held-value / cancel-to-revert
 * step here, each press is a complete, immediately-committed edit. */
static int read_level_detail_delta(const InputState *input, const BindingStore *bindings)
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

/* Single-frame mutation: push undo right after applying, same as the
 * bool-attribute toggle in handle_browse_select. No session to close. */
static void apply_level_detail_delta(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int delta)
{
    int row = editor_state->level_detail_row;
    if (delta == 0 || level_detail_row_is_string(row)) {
        return;
    }
    Level *level = &state->gamedata.current_level;
    level_detail_int_set(level, row, level_detail_int_get(level, row) + delta);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit level detail"));
}

/* Opens the word builder pre-filled with the field's current value,
 * mirroring fuzzy_finder_enter_word_builder's pre-fill pattern
 * (editor/widgets.c) for editing an existing string in place. */
static void enter_level_detail_string_edit(EditorState *editor_state, const Str *current_value, LevelStringField field)
{
    int existing_len = (int)current_value->len;
    if (existing_len >= WORD_BUILDER_BUF_SIZE) {
        existing_len = WORD_BUILDER_BUF_SIZE - 1;
    }
    if (existing_len > 0) {
        memcpy(editor_state->word_builder_buf, current_value->ptr, (size_t)existing_len);
    }
    editor_state->word_builder_buf[existing_len] = '\0';
    editor_state->word_builder_len = existing_len;
    editor_state->word_builder_scroll = 0;
    editor_state->editing_level_string_field = field;
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

/* Called from the word builder's CONFIRM dispatch (editor/widgets.c)
 * once the user picks "[ DONE ]" while editing_level_string_field is
 * set. Fresh str_new + str_from_cstr into gamedata_arena, matching how
 * confirm_child_tag_edit (editor/attr.c) rewrites a Str field: the old
 * block is orphaned in the arena, bounded by one per edit event. */
void confirm_level_string_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Level *level = &state->gamedata.current_level;
    int row = (editor_state->editing_level_string_field == LEVEL_STRING_FIELD_BACKGROUND_TILE)
                  ? LEVEL_DETAIL_ROW_BACKGROUND_TILE
                  : LEVEL_DETAIL_ROW_MUSIC;
    Str *target = level_detail_string_field(level, row);
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    Str new_value = str_new(alloc);
    (void)str_from_cstr(&new_value, editor_state->word_builder_buf);
    *target = new_value;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit level detail"));
}

static void handle_level_detail_navigate(EditorState *editor_state, int direction)
{
    int new_row = editor_state->level_detail_row + direction;
    if (new_row < 0) {
        new_row = 0;
    } else if (new_row >= LEVEL_DETAIL_ROW_COUNT) {
        new_row = LEVEL_DETAIL_ROW_COUNT - 1;
    }
    editor_state->level_detail_row = new_row;
}

static void handle_level_detail_confirm(GameState *state, EditorState *editor_state)
{
    int row = editor_state->level_detail_row;
    if (!level_detail_row_is_string(row)) {
        return;
    }
    Level *level = &state->gamedata.current_level;
    LevelStringField field =
        (row == LEVEL_DETAIL_ROW_BACKGROUND_TILE) ? LEVEL_STRING_FIELD_BACKGROUND_TILE : LEVEL_STRING_FIELD_MUSIC;
    enter_level_detail_string_edit(editor_state, level_detail_string_field(level, row), field);
}

static void handle_level_detail_input(GameState *state,
                                      EditorState *editor_state,
                                      UndoHistory *undo_history,
                                      const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->level_detail_open = false;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN)) {
        handle_level_detail_navigate(editor_state, 1);
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP)) {
        handle_level_detail_navigate(editor_state, -1);
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        handle_level_detail_confirm(state, editor_state);
    }
    int delta = read_level_detail_delta(input, &state->bindings);
    apply_level_detail_delta(state, editor_state, undo_history, delta);
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
    editor_state->multiselect_count = 0;
    editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
    editor_state->selected_tree_index = -1;
}

/* CONFIRM on the list: the "+ NEW LEVEL" sentinel opens the word builder
 * to name a fresh level (mirrors the new-blueprint word-builder flow,
 * editor/blueprint.c enter_word_builder_empty); row 0 (current level,
 * marked "*") opens the detail view for its metadata instead of the
 * former switch-to-self no-op; any other row switches via
 * confirm_level_switch as before. */
static void confirm_level_list_row(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    if (level_list_is_new_sentinel(state, editor_state->level_list_scroll)) {
        editor_state->creating_level = true;
        editor_state->word_builder_len = 0;
        editor_state->word_builder_buf[0] = '\0';
        editor_state->word_builder_scroll = 0;
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
        return;
    }
    if (editor_state->level_list_scroll == 0) {
        editor_state->level_detail_open = true;
        editor_state->level_detail_row = 0;
        return;
    }
    confirm_level_switch(diag, state, editor_state, undo_history);
}

/* --- Public --- */

/* Level mode is a list-vs-detail focus split (see LevelStringField /
 * EditorState.level_detail_open in editor.h): the list handler below
 * owns navigation over current+other_levels+"+ NEW LEVEL", while
 * handle_level_detail_input owns the current level's metadata rows once
 * CONFIRM opens them. CANCEL in detail view returns to the list; CANCEL
 * in the list returns to Scene mode. */
void handle_level_browse_input(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input)
{
    if (editor_state->level_detail_open) {
        handle_level_detail_input(state, editor_state, undo_history, input);
        return;
    }

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
        confirm_level_list_row(diag, state, editor_state, undo_history);
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
        if (level_list_is_new_sentinel(state, index)) {
            draw_ui_text(font, TextFormat("%s + NEW LEVEL", selected ? ">" : " "), panel_x + DEBUG_MARGIN, y_offset,
                         EDITOR_PANEL_FONT_SIZE, color);
        } else {
            Strv name = level_list_name_at(state, index);
            const char *marker = (index == 0) ? "*" : " ";
            draw_ui_text(font,
                         TextFormat("%s%s %.*s  (%d entities)", selected ? ">" : " ", marker, (int)name.len, name.ptr,
                                    level_list_entity_count_at(state, index)),
                         panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        }
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static const char *level_detail_row_value_text(const Level *level, int row)
{
    if (level_detail_row_is_string(row)) {
        const Str *field = level_detail_string_field_const(level, row);
        return (field->ptr && field->len > 0) ? field->ptr : "";
    }
    return TextFormat("%d", level_detail_int_get(level, row));
}

void draw_level_detail_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    const Level *level = &state->gamedata.current_level;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ %.*s ]", (int)level->name.len, level->name.ptr), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    for (int row = 0; row < LEVEL_DETAIL_ROW_COUNT; row++) {
        bool selected = (row == editor_state->level_detail_row);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font,
                     TextFormat("%s %s: %s", selected ? ">" : " ", level_detail_row_labels[row],
                                level_detail_row_value_text(level, row)),
                     panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

/* --- Hint tables --- */

static const EditorActionHint level_list_hints[] = {
    {ACTION_CANCEL, "Back to scene"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_CONFIRM, "Switch / Edit / New"},
};

static const EditorHintTable level_list_table = {
    .hints = level_list_hints,
    .count = (int)(sizeof(level_list_hints) / sizeof(level_list_hints[0])),
    .mode_label = "Level list",
};

static const EditorActionHint level_detail_hints[] = {
    {ACTION_CANCEL, "Back to list"},     {ACTION_NAV_DOWN, "Next"},   {ACTION_NAV_UP, "Prev"},
    {ACTION_CONFIRM, "Edit text field"}, {ACTION_ATTR_DEC_1, "-1"},   {ACTION_ATTR_INC_1, "+1"},
    {ACTION_ATTR_DEC_10, "-10"},         {ACTION_ATTR_INC_10, "+10"},
};

static const EditorHintTable level_detail_table = {
    .hints = level_detail_hints,
    .count = (int)(sizeof(level_detail_hints) / sizeof(level_detail_hints[0])),
    .mode_label = "Level detail",
};

const EditorHintTable *level_detail_hints_table(void)
{
    return &level_detail_table;
}

const EditorHintTable *level_list_hints_table(void)
{
    return &level_list_table;
}

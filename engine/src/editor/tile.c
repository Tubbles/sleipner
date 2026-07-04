#include "editor/internal.h"

#include "strv.h"
#include "tileset.h"

#include <string.h>

/* --- Layer helpers --- */

static vec_int *active_tile_layer(Level *level, TileLayer active)
{
    return active == TILE_LAYER_GROUND ? &level->tiles_ground : &level->tiles_overlay;
}

/* Lazily grows a never-authored (count == 0) tile layer to the level's full
 * tiles_wide * tiles_high grid, zero-filled, matching the "empty or exact
 * match" invariant parse_tile_layer enforces (level.c). Every level's layer
 * vecs are always either empty or already exactly this size — level_load,
 * level_load_others, and create_new_level all establish that — so this only
 * ever transitions empty -> full, never partial. Single-cell writes after
 * this point are safe direct index assignments with no further push, so
 * there is no pointer-across-push concern (CLAUDE.md "Vec growth and
 * pointer stability"). */
static void ensure_tile_layer_allocated(vec_int *layer, int total_cells)
{
    if (layer->count > 0) {
        return;
    }
    for (int index = 0; index < total_cells; index++) {
        if (!vec_int_push(layer, 0)) {
            return;
        }
    }
}

static void paint_tile_at_cursor(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int tile_id)
{
    Level *level = &state->gamedata.current_level;
    int total_cells = level->tiles_wide * level->tiles_high;
    vec_int *layer = active_tile_layer(level, editor_state->tile_active_layer);
    ensure_tile_layer_allocated(layer, total_cells);
    if (layer->count != total_cells) {
        return; /* allocation failed; don't write out of bounds */
    }
    int cell_index = level_tile_index(editor_state->tile_cursor_row, editor_state->tile_cursor_col, level->tiles_wide);
    layer->data[cell_index] = tile_id;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr(tile_id > 0 ? "Paint tile" : "Erase tile"));
}

/* --- Cursor navigation --- */

/* Which cursor field/grid dimension a move_tile_cursor call targets. Kept as
 * a distinct type (rather than a second `int` parameter) so the "current
 * position" and "grid extent" it derives are two differently-typed
 * arguments, not two adjacent `int`s a caller could transpose by mistake
 * (bugprone-easily-swappable-parameters). `delta` is placed last, after
 * `level`, rather than next to `axis`: clang-tidy treats a plain enum as
 * int-convertible, so an enum directly beside an `int` still trips the
 * same check as two adjacent ints — only a pointer-typed neighbor avoids
 * it. */
typedef enum {
    CURSOR_AXIS_COL,
    CURSOR_AXIS_ROW,
} CursorAxis;

static void move_tile_cursor(EditorState *editor_state, CursorAxis axis, const Level *level, int delta)
{
    int *cursor = (axis == CURSOR_AXIS_COL) ? &editor_state->tile_cursor_col : &editor_state->tile_cursor_row;
    int max_value = ((axis == CURSOR_AXIS_COL) ? level->tiles_wide : level->tiles_high) - 1;
    int moved = *cursor + delta;
    if (moved < 0) {
        moved = 0;
    } else if (moved > max_value) {
        moved = max_value;
    }
    *cursor = moved;
}

static void handle_tile_cursor_navigate(EditorState *editor_state,
                                        const Level *level,
                                        const InputState *input,
                                        const BindingStore *bindings)
{
    if (input_pressed(input, bindings, ACTION_NAV_LEFT)) {
        move_tile_cursor(editor_state, CURSOR_AXIS_COL, level, -1);
    }
    if (input_pressed(input, bindings, ACTION_NAV_RIGHT)) {
        move_tile_cursor(editor_state, CURSOR_AXIS_COL, level, 1);
    }
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        move_tile_cursor(editor_state, CURSOR_AXIS_ROW, level, -1);
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        move_tile_cursor(editor_state, CURSOR_AXIS_ROW, level, 1);
    }
}

/* Opens TILE_PALETTE focused on the currently selected tile id (or the top
 * of the list otherwise). No-ops if the tileset has no real entries
 * (count <= 1, only the unused index-0 placeholder — see tileset.h) —
 * mirrors handle_place_input's blueprint-count guard for the analogous
 * "nothing to pick from yet" case. */
static void enter_tile_palette(GameState *state, EditorState *editor_state)
{
    int total = state->gamedata.tileset.count > 0 ? state->gamedata.tileset.count - 1 : 0;
    if (total <= 0) {
        return;
    }
    int scroll = editor_state->selected_tile_id > 0 ? editor_state->selected_tile_id - 1 : 0;
    editor_state->tile_palette_scroll = scroll > total - 1 ? total - 1 : scroll;
    editor_state->sub_mode = EDITOR_SUB_TILE_PALETTE;
}

/* --- Public: input handlers --- */

/* TILE_PAINT: NAV moves the cursor cell by cell over the current level's
 * tile grid, CONFIRM paints selected_tile_id at the cursor on the active
 * layer, EDITOR_DELETE erases (paints 0), TAB_NEXT toggles ground/overlay,
 * and EDITOR_PLACE opens the palette. CANCEL leaves Tile mode entirely,
 * back to Scene/BROWSE. */
void handle_tile_paint_input(GameState *state,
                             EditorState *editor_state,
                             UndoHistory *undo_history,
                             const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_PLACE)) {
        enter_tile_palette(state, editor_state);
        return;
    }
    handle_tile_cursor_navigate(editor_state, &state->gamedata.current_level, input, &state->bindings);
    if (input_pressed(input, &state->bindings, ACTION_TAB_NEXT)) {
        editor_state->tile_active_layer =
            (editor_state->tile_active_layer == TILE_LAYER_GROUND) ? TILE_LAYER_OVERLAY : TILE_LAYER_GROUND;
    }
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_DELETE)) {
        paint_tile_at_cursor(state, editor_state, undo_history, 0);
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        paint_tile_at_cursor(state, editor_state, undo_history, editor_state->selected_tile_id);
    }
}

/* TILE_PALETTE: NAV_UP/DOWN clamp the focused row (matches the watch list /
 * fuzzy finder pickers, which clamp rather than wrap). CONFIRM adopts the
 * focused tile id as selected_tile_id and returns to TILE_PAINT; CANCEL
 * returns without changing it. */
void handle_tile_palette_input(EditorState *editor_state,
                               const vec_tileset_entry *tileset,
                               const InputState *input,
                               const BindingStore *bindings)
{
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_TILE_PAINT;
        return;
    }
    int total = tileset->count > 0 ? tileset->count - 1 : 0;
    if (input_pressed(input, bindings, ACTION_NAV_DOWN) && editor_state->tile_palette_scroll < total - 1) {
        editor_state->tile_palette_scroll++;
    }
    if (input_pressed(input, bindings, ACTION_NAV_UP) && editor_state->tile_palette_scroll > 0) {
        editor_state->tile_palette_scroll--;
    }
    if (input_pressed(input, bindings, ACTION_CONFIRM) && total > 0) {
        editor_state->selected_tile_id = editor_state->tile_palette_scroll + 1;
        editor_state->sub_mode = EDITOR_SUB_TILE_PAINT;
    }
}

/* --- Texture preview lookup ---
 *
 * Const-correct counterpart of game.c's texture_registry_lookup (which
 * takes a non-const void * user_data): every draw_*_panel function in the
 * editor takes a const GameState *, same pattern draw_place_preview
 * (editor/draw.c) already uses for its ghost-sprite lookup. */
static const Texture2D *find_tile_texture(const GameState *state, const char *filename)
{
    for (int index = 0; index < state->assets.textures.count; index++) {
        if (strcmp(state->assets.textures.data[index].filename, filename) == 0) {
            return &state->assets.textures.data[index].texture;
        }
    }
    return nullptr;
}

/* --- Public: draw --- */

#define TILE_PAINT_PREVIEW_SIZE 32 /* screen px: selected-tile icon in the TILE_PAINT side panel */
#define TILE_PALETTE_ICON_SIZE 32  /* screen px: per-row icon in the TILE_PALETTE list */
#define TILE_PALETTE_ROW_PAD 8     /* extra vertical gap between palette rows, beyond the icon size */

static const Color tile_cursor_color = {255, 255, 255, 255}; /* white: matches the selected-entity outline convention */

void draw_tile_paint_cursor(const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_TILE_PAINT) {
        return;
    }
    Rectangle cell = {
        (float)(editor_state->tile_cursor_col * TILE_SIZE),
        (float)(editor_state->tile_cursor_row * TILE_SIZE),
        TILE_SIZE,
        TILE_SIZE,
    };
    DrawRectangleLinesEx(cell, 2.0F, tile_cursor_color);
}

static const char *tile_layer_label(TileLayer layer)
{
    return layer == TILE_LAYER_GROUND ? "ground" : "overlay";
}

void draw_tile_paint_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_TILE_PAINT) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Tile Paint ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("layer: %s", tile_layer_label(editor_state->tile_active_layer)),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("cursor: %d, %d", editor_state->tile_cursor_col, editor_state->tile_cursor_row),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("tile id: %d", editor_state->selected_tile_id), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    const vec_tileset_entry *tileset = &state->gamedata.tileset;
    int tile_id = editor_state->selected_tile_id;
    if (tile_id > 0 && tile_id < tileset->count && tileset->data[tile_id].texture.len > 0) {
        const TilesetEntry *entry = &tileset->data[tile_id];
        const Texture2D *texture = find_tile_texture(state, entry->texture.ptr);
        if (texture) {
            Rectangle dest = {(float)(panel_x + DEBUG_MARGIN), (float)y_offset, TILE_PAINT_PREVIEW_SIZE,
                              TILE_PAINT_PREVIEW_SIZE};
            DrawTexturePro(*texture, entry->src, dest, (Vector2){0, 0}, 0.0F, WHITE);
        }
    }
}

static int tile_palette_visible_count(int screen_height)
{
    int row_height = TILE_PALETTE_ICON_SIZE + TILE_PALETTE_ROW_PAD;
    return (screen_height - HINTS_BAR_HEIGHT - EDITOR_PANEL_LINE_HEIGHT) / row_height;
}

void draw_tile_palette_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_TILE_PALETTE) {
        return;
    }
    const vec_tileset_entry *tileset = &state->gamedata.tileset;
    int total = tileset->count > 0 ? tileset->count - 1 : 0;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Tile Palette ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int row_height = TILE_PALETTE_ICON_SIZE + TILE_PALETTE_ROW_PAD;
    int visible = tile_palette_visible_count(screen.height);
    int scroll = editor_state->tile_palette_scroll - (visible / 2);
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
        int tile_id = index + 1;
        const TilesetEntry *entry = &tileset->data[tile_id];
        bool selected = (index == editor_state->tile_palette_scroll);
        Color color = selected ? WHITE : debug_text_color;
        const Texture2D *texture = entry->texture.len > 0 ? find_tile_texture(state, entry->texture.ptr) : nullptr;
        if (texture) {
            Rectangle dest = {(float)(panel_x + DEBUG_MARGIN), (float)y_offset, TILE_PALETTE_ICON_SIZE,
                              TILE_PALETTE_ICON_SIZE};
            DrawTexturePro(*texture, entry->src, dest, (Vector2){0, 0}, 0.0F, WHITE);
        }
        const char *texture_name = entry->texture.len > 0 ? entry->texture.ptr : "?";
        draw_ui_text(font, TextFormat("%s id:%d  %s", selected ? ">" : " ", tile_id, texture_name),
                     panel_x + DEBUG_MARGIN + TILE_PALETTE_ICON_SIZE + DEBUG_MARGIN, y_offset + (row_height / 4),
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += row_height;
    }
}

/* --- Hint tables --- */

static const EditorActionHint tile_paint_hints[] = {
    {ACTION_CANCEL, "Back to scene"}, {ACTION_NAV_UP, "Up"},           {ACTION_NAV_DOWN, "Down"},
    {ACTION_NAV_LEFT, "Left"},        {ACTION_NAV_RIGHT, "Right"},     {ACTION_TAB_NEXT, "Toggle layer"},
    {ACTION_CONFIRM, "Paint"},        {ACTION_EDITOR_DELETE, "Erase"}, {ACTION_EDITOR_PLACE, "Palette"},
};

static const EditorHintTable tile_paint_table = {
    .hints = tile_paint_hints,
    .count = (int)(sizeof(tile_paint_hints) / sizeof(tile_paint_hints[0])),
    .mode_label = "Tile paint",
};

const EditorHintTable *tile_paint_hints_table(void)
{
    return &tile_paint_table;
}

static const EditorActionHint tile_palette_hints[] = {
    {ACTION_CANCEL, "Back to paint"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Select tile"},
};

static const EditorHintTable tile_palette_table = {
    .hints = tile_palette_hints,
    .count = (int)(sizeof(tile_palette_hints) / sizeof(tile_palette_hints[0])),
    .mode_label = "Tile palette",
};

const EditorHintTable *tile_palette_hints_table(void)
{
    return &tile_palette_table;
}

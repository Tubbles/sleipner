#include "raylib.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "diag.h"
#include "editor.h"
#include "error.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "map.h"
#include "rect.h"
#include "rule.h"
#include "strv.h"
#include "undo.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static const Color handle_color = {0, 255, 255, 255};            /* cyan: collision handles */
static const int place_preview_alpha = 128;                      /* alpha for texture ghost during placement */
static const Color place_ghost_color = {100, 255, 100, 180};     /* semi-transparent green: place preview */
static const Color radial_highlight_color = {255, 200, 50, 200}; /* amber: selected radial sector */

static void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color)
{
    DrawTextEx(font, text, (Vector2){(float)pos_x, (float)pos_y}, (float)font_size, 1, color);
}

static int measure_ui_text(Font font, const char *text, int font_size)
{
    return (int)MeasureTextEx(font, text, (float)font_size, 1).x;
}

bool toggle_pressed(ToggleBinding binding)
{
    if (IsKeyPressed(binding.key)) {
        return true;
    }
    return IsGamepadButtonPressed(0, binding.gamepad_button);
}

void update_editor_camera(Camera2D *camera, InputState input, float delta_time)
{
    camera->target.x += input.left_stick.x * EDITOR_CAMERA_SPEED * delta_time;
    camera->target.y += input.left_stick.y * EDITOR_CAMERA_SPEED * delta_time;
}

void draw_editor_crosshair(RectU32 game_bounds)
{
    int center_x = (int)game_bounds.width / 2;
    int center_y = (int)game_bounds.height / 2;
    DrawLine(center_x - EDITOR_CROSSHAIR_HALF, center_y, center_x + EDITOR_CROSSHAIR_HALF, center_y, WHITE);
    DrawLine(center_x, center_y - EDITOR_CROSSHAIR_HALF, center_x, center_y + EDITOR_CROSSHAIR_HALF, WHITE);
}

void draw_hints_bar(bool editor_mode, const EditorState *editor_state, ScreenSize screen, Font ui_font)
{
    int bar_y = screen.height - HINTS_BAR_HEIGHT;
    DrawRectangle(0, bar_y, screen.width, HINTS_BAR_HEIGHT, debug_bg_color);
    const char *hints;
    if (editor_mode) {
        if (editor_state->sub_mode == EDITOR_SUB_DRAG) {
            hints = "F9/Y: Save  |  A/Ent: Confirm  |  B/Esc: Cancel  |  Stick: Move entity";
        } else if (editor_state->sub_mode == EDITOR_SUB_HANDLES) {
            hints = "F9/Y: Save  |  A/Ent: Confirm  |  B/Esc: Cancel  |  Stick: Move  |  R-Stick: Resize";
        } else if (editor_state->sub_mode == EDITOR_SUB_PLACE) {
            hints = "A/Ent: Spawn  |  B/Esc: Cancel  |  Up/Down: Scroll  |  L1/Q: PgUp  |  R1/E: PgDn  |  Stick: Pan";
        } else if (editor_state->sub_mode == EDITOR_SUB_ATTR_EDIT) {
            hints = "A/Ent: Confirm  |  B/Esc: Cancel  |  Left/Right: ±1 (hold: repeat)  |  [/L1: ±10  |  PgDn/L2: "
                    "-100  |  PgUp/R2: +100";
        } else if (editor_state->sub_mode == EDITOR_SUB_RADIAL) {
            hints = "Stick: Pick tool  |  A/Ent: Confirm  |  B/Esc: Cancel";
        } else if (editor_state->sub_mode == EDITOR_SUB_WORD_BUILDER) {
            hints = "A/Ent: Pick / Done  |  B/Esc: Undo / Cancel  |  Up/Down: Scroll  |  L1/Q: PgUp  |  R1/E: PgDn";
        } else {
            hints = "F5: Play  |  F9/Y: Save  |  Tab/Sel: Tools  |  A/Ent: Sel  |  B/Esc: Desel  |  Up/Down: Attr  |  "
                    "Shift/L2: Watch  |  Del/X: Delete  |  G/L3: Grab  |  H/L1: Handles  |  P/R1: Place  |  Stick: Pan";
        }
    } else {
        hints = "F5: Editor  |  F3: Debug  |  F4: Fonts";
    }
    int text_y = bar_y + ((HINTS_BAR_HEIGHT - HINTS_FONT_SIZE) / 2);
    draw_ui_text(ui_font, hints, DEBUG_MARGIN, text_y, HINTS_FONT_SIZE, debug_text_color);
}

int find_nearest_entity(const Level *level, Vector2 cursor_world)
{
    int nearest_index = -1;
    float nearest_dist_sq = 0.0F;
    for (int index = 0; index < level->entities.count; index++) {
        const Entity *entity = &level->entities.data[index];
        if (entity->parent_index >= 0) {
            continue;
        }
        float delta_x = entity->position.x - cursor_world.x;
        float delta_y = entity->position.y - cursor_world.y;
        float dist_sq = (delta_x * delta_x) + (delta_y * delta_y);
        if (nearest_index < 0 || dist_sq < nearest_dist_sq) {
            nearest_index = index;
            nearest_dist_sq = dist_sq;
        }
    }
    return nearest_index;
}

static Rectangle get_source_rect(const AttrSet *instance, const AttrSet *defaults)
{
    return (Rectangle){
        attr_get_scoped_float(instance, defaults, "src_x", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_y", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_w", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_h", 0.0F),
    };
}

static Rectangle entity_outline_rect(const GameState *state, const Entity *entity)
{
    Rectangle col = entity->collision;
    if (col.width > 0.0F && col.height > 0.0F) {
        return col;
    }
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    Rectangle src = get_source_rect(&entity->attrs, defaults);
    return (Rectangle){entity->position.x, entity->position.y, src.width, src.height};
}

void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index)
{
    if (hover_entity_index >= 0 && hover_entity_index < state->gamedata.current_level.entities.count) {
        DrawRectangleLinesEx(
            entity_outline_rect(state, &state->gamedata.current_level.entities.data[hover_entity_index]), 1.0F, YELLOW);
    }
    int sel = editor_state->selected_entity_index;
    if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
        DrawRectangleLinesEx(entity_outline_rect(state, &state->gamedata.current_level.entities.data[sel]), 2.0F,
                             WHITE);
    }
}

static const char *attr_display_value(const Attribute *attr)
{
    switch (attr->type) {
    case ATTR_BOOL:
        return attr->value.b ? "true" : "false";
    case ATTR_INT:
        return TextFormat("%d", attr->value.i);
    case ATTR_FLOAT:
        return TextFormat("%.1f", attr->value.f);
    case ATTR_STRING:
        return attr->value.str.ptr ? attr->value.str.ptr : "";
    }
    return "";
}

static void
draw_attr_section(Font font, const AttrSet *set, int panel_x, int *y_offset, int base_index, int selected_attr_index)
{
    for (int index = 0; index < set->entries.count; index++) {
        const Attribute *attr = &set->entries.data[index];
        Color text_color = (base_index + index == selected_attr_index) ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("  %s: %s", attr->name.ptr, attr_display_value(attr)), panel_x + DEBUG_MARGIN,
                     *y_offset, EDITOR_PANEL_FONT_SIZE, text_color);
        *y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_editor_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    Font font = state->assets.ui_font;
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    int y_offset = 0;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    draw_ui_text(font,
                 TextFormat("[ %s ]  id: %d  parent: %d", entity->blueprint_name.ptr, entity->id, entity->parent_index),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("pos: %.1f %.1f", entity->position.x, entity->position.y), panel_x + DEBUG_MARGIN,
                 y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;
    draw_ui_text(font, "--- instance ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    int sel_attr = editor_state->selected_attr_index;
    draw_attr_section(font, &entity->attrs, panel_x, &y_offset, 0, sel_attr);
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    if (defaults) {
        draw_ui_text(font, "--- blueprint ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                     debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        int blueprint_base = entity->attrs.entries.count;
        draw_attr_section(font, defaults, panel_x, &y_offset, blueprint_base, sel_attr);
    }
}

void draw_watch_overlay(ScreenSize screen, const GameState *state, const WatchList *watches)
{
    if (watches->count <= 0) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    int panel_height = (watches->count * 2 * EDITOR_PANEL_LINE_HEIGHT) + DEBUG_MARGIN;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, panel_height, debug_bg_color);
    int y_offset = 0;
    for (int index = 0; index < watches->count; index++) {
        int entity_index = watches->entity_indices[index];
        if (entity_index >= state->gamedata.current_level.entities.count) {
            y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;
            continue;
        }
        const Entity *entity = &state->gamedata.current_level.entities.data[entity_index];
        draw_ui_text(
            state->assets.ui_font,
            TextFormat("[%s] pos:%.0f,%.0f", entity->blueprint_name.ptr, entity->position.x, entity->position.y),
            panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
        const Attribute *hp_attr = attr_get_scoped(&entity->attrs, defaults, "hp");
        const Attribute *hp_max_attr = attr_get_scoped(&entity->attrs, defaults, "hp_max");
        if (hp_attr && hp_max_attr) {
            draw_ui_text(state->assets.ui_font, TextFormat("  hp: %d/%d", hp_attr->value.i, hp_max_attr->value.i),
                         panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        }
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static Blueprint *find_blueprint_by_name(GameState *state, const char *name)
{
    for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
        Blueprint *blueprint = &state->gamedata.blueprints.entries.data[index];
        const char *blueprint_name = attr_get_string(&blueprint->attrs, "name");
        if (blueprint_name != nullptr && strcmp(blueprint_name, name) == 0) {
            return blueprint;
        }
    }
    return nullptr;
}

static int total_attr_count(const GameState *state, const Entity *entity)
{
    int instance_count = entity->attrs.entries.count;
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    int blueprint_count = defaults ? defaults->entries.count : 0;
    return instance_count + blueprint_count;
}

/* entity must not be nullptr; resolves blueprint attrs via find_blueprint_by_name */
static Attribute *attr_at_display_index(GameState *state, Entity *entity, int attr_index)
{
    int instance_count = entity->attrs.entries.count;
    if (attr_index < instance_count) {
        return &entity->attrs.entries.data[attr_index];
    }
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
    if (!blueprint) {
        return nullptr;
    }
    int blueprint_index = attr_index - instance_count;
    if (blueprint_index >= blueprint->attrs.entries.count) {
        return nullptr;
    }
    return &blueprint->attrs.entries.data[blueprint_index];
}

static bool is_blueprint_attr(const Entity *entity, int attr_index)
{
    return attr_index >= entity->attrs.entries.count;
}

static int read_value_delta(void)
{
    int delta = 0;
    if (toggle_pressed((ToggleBinding){KEY_LEFT_BRACKET, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        delta -= EDITOR_ATTR_LARGE_STEP;
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT_BRACKET, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        delta += EDITOR_ATTR_LARGE_STEP;
    }
    if (toggle_pressed((ToggleBinding){KEY_PAGE_DOWN, GAMEPAD_BUTTON_LEFT_TRIGGER_2})) {
        delta -= EDITOR_ATTR_HUGE_STEP;
    }
    if (toggle_pressed((ToggleBinding){KEY_PAGE_UP, GAMEPAD_BUTTON_RIGHT_TRIGGER_2})) {
        delta += EDITOR_ATTR_HUGE_STEP;
    }
    return delta;
}

static int read_held_dir(void)
{
    if (IsKeyDown(KEY_LEFT) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
        return -1;
    }
    if (IsKeyDown(KEY_RIGHT) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
        return 1;
    }
    return 0;
}

static void propagate_collision_to_entities(GameState *state, const Blueprint *blueprint)
{
    const char *blueprint_name = attr_get_string(&blueprint->attrs, "name");
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        Entity *entity = &state->gamedata.current_level.entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, blueprint_name) == 0) {
            entity->collision_offset = blueprint_get_collision_offset(blueprint);
            entity->collision_size = blueprint_get_collision_size(blueprint);
            entity_update_collision(entity);
        }
    }
}

void draw_collision_handles(const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_HANDLES) {
        return;
    }
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    Rectangle col = state->gamedata.current_level.entities.data[sel].collision;
    DrawRectangleLinesEx(col, 2.0F, handle_color);
    int half = EDITOR_HANDLE_SIZE / 2;
    DrawRectangle((int)col.x - half, (int)col.y - half, EDITOR_HANDLE_SIZE, EDITOR_HANDLE_SIZE, handle_color);
    DrawRectangle((int)(col.x + col.width) - half, (int)col.y - half, EDITOR_HANDLE_SIZE, EDITOR_HANDLE_SIZE,
                  handle_color);
    DrawRectangle((int)col.x - half, (int)(col.y + col.height) - half, EDITOR_HANDLE_SIZE, EDITOR_HANDLE_SIZE,
                  handle_color);
    DrawRectangle((int)(col.x + col.width) - half, (int)(col.y + col.height) - half, EDITOR_HANDLE_SIZE,
                  EDITOR_HANDLE_SIZE, handle_color);
}

static int find_place_blueprint_index(const GameState *state, const EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return 0;
    }
    const char *name = state->gamedata.current_level.entities.data[sel].blueprint_name.ptr;
    for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
        const char *bp_name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        if (bp_name && strcmp(bp_name, name) == 0) {
            return index;
        }
    }
    return 0;
}

static int place_visible_count(int screen_height)
{
    return (screen_height - HINTS_BAR_HEIGHT - EDITOR_PANEL_LINE_HEIGHT) / EDITOR_PANEL_LINE_HEIGHT;
}

void draw_place_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_PLACE) {
        return;
    }
    int count = state->gamedata.blueprints.entries.count;
    if (count == 0) {
        return;
    }
    int bp_index = editor_state->place_blueprint_index;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, "[ Place Mode ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int visible = place_visible_count(screen.height);
    int scroll = bp_index - (visible / 2);
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
        const char *name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        bool selected = (index == bp_index);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", name ? name : "?"), panel_x + DEBUG_MARGIN,
                     y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_place_preview(const GameState *state, const EditorState *editor_state, Camera2D camera)
{
    if (editor_state->sub_mode != EDITOR_SUB_PLACE) {
        return;
    }
    if (state->gamedata.blueprints.entries.count == 0) {
        return;
    }
    const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[editor_state->place_blueprint_index];

    const char *texture_name = attr_get_string(&blueprint->attrs, "texture");
    if (texture_name) {
        for (int index = 0; index < state->assets.textures.count; index++) {
            if (strcmp(state->assets.textures.data[index].filename, texture_name) == 0) {
                Rectangle source = {
                    attr_get_float(&blueprint->attrs, "src_x", 0.0F),
                    attr_get_float(&blueprint->attrs, "src_y", 0.0F),
                    attr_get_float(&blueprint->attrs, "src_w", 0.0F),
                    attr_get_float(&blueprint->attrs, "src_h", 0.0F),
                };
                Color ghost_tint = {255, 255, 255, (unsigned char)place_preview_alpha};
                DrawTextureRec(state->assets.textures.data[index].texture, source, camera.target, ghost_tint);
                break;
            }
        }
    }

    Vector2 offset = blueprint_get_collision_offset(blueprint);
    Vector2 size = blueprint_get_collision_size(blueprint);
    Rectangle ghost = {camera.target.x + offset.x, camera.target.y + offset.y, size.x, size.y};
    DrawRectangleLinesEx(ghost, 2.0F, place_ghost_color);
}

static void mark_deleted_descendants(const Level *level, bool *is_deleted, int count)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int index = 0; index < count; index++) {
            if (is_deleted[index]) {
                continue;
            }
            int parent = level->entities.data[index].parent_index;
            if (parent >= 0 && is_deleted[parent]) {
                is_deleted[index] = true;
                changed = true;
            }
        }
    }
}

static void delete_selected_entity(GameState *state, EditorState *editor_state, WatchList *watches)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    if (state->gamedata.current_level.entities.data[sel].parent_index >= 0) {
        return;
    }
    int count = state->gamedata.current_level.entities.count;
    SCRATCH_SCOPE(&state->scratch_arena);
    bool *is_deleted = arena_alloc(&state->scratch_arena, (size_t)count * sizeof(bool));
    int *new_index_map = arena_alloc(&state->scratch_arena, (size_t)count * sizeof(int));
    memset(is_deleted, 0, (size_t)count * sizeof(bool));
    is_deleted[sel] = true;
    mark_deleted_descendants(&state->gamedata.current_level, is_deleted, count);
    for (int index = 0; index < count; index++) {
        if (is_deleted[index]) {
            int entity_id = state->gamedata.current_level.entities.data[index].id;
            map_int_str_remove(&state->gamedata.entity_blueprints, entity_id);
            map_entity_ruleset_remove(&state->gamedata.rule_table, entity_id);
        }
    }
    int new_count = 0;
    for (int index = 0; index < count; index++) {
        if (is_deleted[index]) {
            new_index_map[index] = -1;
            continue;
        }
        new_index_map[index] = new_count++;
    }
    for (int index = 0; index < count; index++) {
        if (!is_deleted[index]) {
            int parent = state->gamedata.current_level.entities.data[index].parent_index;
            if (parent >= 0) {
                state->gamedata.current_level.entities.data[index].parent_index = new_index_map[parent];
            }
        }
    }
    int write = 0;
    for (int index = 0; index < count; index++) {
        if (!is_deleted[index]) {
            state->gamedata.current_level.entities.data[write++] = state->gamedata.current_level.entities.data[index];
        }
    }
    state->gamedata.current_level.entities.count = write;
    if (state->gamedata.player_index >= 0) {
        state->gamedata.player_index = new_index_map[state->gamedata.player_index];
    }
    int new_watch_count = 0;
    for (int watch_index = 0; watch_index < watches->count; watch_index++) {
        int entity_index = watches->entity_indices[watch_index];
        if (entity_index < count && !is_deleted[entity_index]) {
            watches->entity_indices[new_watch_count++] = new_index_map[entity_index];
        }
    }
    watches->count = new_watch_count;
    editor_state->selected_entity_index = -1;
}

static void
handle_browse_select(GameState *state, Camera2D *camera, EditorState *editor_state, UndoHistory *undo_history)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        editor_state->selected_entity_index = find_nearest_entity(&state->gamedata.current_level, camera->target);
        editor_state->selected_attr_index = -1;
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    int attr_idx = editor_state->selected_attr_index;
    if (attr_idx < 0) {
        return;
    }
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
    if (!attr) {
        return;
    }
    if (attr->type == ATTR_BOOL) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Toggle attribute"));
        attr->value.b = !attr->value.b;
        if (is_blueprint_attr(entity, attr_idx)) {
            Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
            if (blueprint) {
                propagate_collision_to_entities(state, blueprint);
            }
        }
    } else if (attr->type == ATTR_INT) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Edit attribute"));
        editor_state->saved_attr_int = attr->value.i;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (attr->type == ATTR_FLOAT) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Edit attribute"));
        editor_state->saved_attr_float = attr->value.f;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (attr->type == ATTR_STRING) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Edit string"));
        const char *existing = attr->value.str.ptr ? attr->value.str.ptr : "";
        int existing_len = (int)strlen(existing);
        if (existing_len >= WORD_BUILDER_BUF_SIZE) {
            existing_len = WORD_BUILDER_BUF_SIZE - 1;
        }
        memcpy(editor_state->word_builder_buf, existing, (size_t)existing_len);
        editor_state->word_builder_buf[existing_len] = '\0';
        editor_state->word_builder_len = existing_len;
        editor_state->word_builder_scroll = 0;
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
    }
}

static void handle_browse_cancel(EditorState *editor_state)
{
    if (editor_state->selected_attr_index >= 0) {
        editor_state->selected_attr_index = -1;
    } else {
        editor_state->selected_entity_index = -1;
    }
}

static void handle_browse_attr_navigate(const GameState *state, EditorState *editor_state, int direction)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    int total = total_attr_count(state, entity);
    if (total <= 0) {
        return;
    }
    int current = editor_state->selected_attr_index;
    if (current < 0) {
        editor_state->selected_attr_index = (direction > 0) ? 0 : total - 1;
    } else {
        editor_state->selected_attr_index = (current + direction + total) % total;
    }
}

static void
dispatch_radial_confirm(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int confirmed = editor_state->radial_confirmed;
    editor_state->radial_confirmed = -1;
    if (editor_state->radial_context == RADIAL_CTX_TOOLS) {
        int sel = editor_state->selected_entity_index;
        if (confirmed == 0 && sel >= 0) { /* Grab */
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Move entity"));
            editor_state->saved_position = state->gamedata.current_level.entities.data[sel].position;
            editor_state->sub_mode = EDITOR_SUB_DRAG;
        } else if (confirmed == 1) { /* Place */
            if (state->gamedata.blueprints.entries.count > 0) {
                editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
                editor_state->sub_mode = EDITOR_SUB_PLACE;
            }
        } else if (confirmed == 2 && sel >= 0) { /* Handles */
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Resize collision"));
            editor_state->saved_col_offset = state->gamedata.current_level.entities.data[sel].collision_offset;
            editor_state->saved_col_size = state->gamedata.current_level.entities.data[sel].collision_size;
            editor_state->sub_mode = EDITOR_SUB_HANDLES;
        } else if (confirmed == 3) { /* Delete */
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Delete entity"));
            delete_selected_entity(state, editor_state, watches);
        }
    }
}

static void toggle_watch(EditorState *editor_state, WatchList *watches)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    for (int index = 0; index < watches->count; index++) {
        if (watches->entity_indices[index] == sel) {
            watches->entity_indices[index] = watches->entity_indices[watches->count - 1];
            watches->count--;
            return;
        }
    }
    if (watches->count < EDITOR_WATCH_MAX) {
        watches->entity_indices[watches->count] = sel;
        watches->count++;
    }
}

static void reset_editor_selection(EditorState *editor_state, WatchList *watches)
{
    editor_state->selected_entity_index = -1;
    editor_state->selected_attr_index = -1;
    watches->count = 0;
}

void handle_browse_input(GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         UndoHistory *undo_history,
                         InputState input,
                         float delta_time)
{
    if (editor_state->radial_confirmed >= 0) {
        dispatch_radial_confirm(state, editor_state, watches, undo_history);
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_TAB, GAMEPAD_BUTTON_MIDDLE_LEFT})) {
        editor_state->radial_selected = -1;
        editor_state->radial_confirmed = -1;
        editor_state->radial_item_count = 4;
        editor_state->radial_context = RADIAL_CTX_TOOLS;
        editor_state->sub_mode = EDITOR_SUB_RADIAL;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        handle_browse_select(state, camera, editor_state, undo_history);
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        handle_browse_cancel(editor_state);
    }
    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        handle_browse_attr_navigate(state, editor_state, 1);
    }
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        handle_browse_attr_navigate(state, editor_state, -1);
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT_SHIFT, GAMEPAD_BUTTON_LEFT_TRIGGER_2})) {
        toggle_watch(editor_state, watches);
    }
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Delete entity"));
        delete_selected_entity(state, editor_state, watches);
    }
    if (toggle_pressed((ToggleBinding){KEY_P, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        if (state->gamedata.blueprints.entries.count > 0) {
            editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_PLACE;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT})) {
        undo_history_step_back(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        reset_editor_selection(editor_state, watches);
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT})) {
        undo_history_step_forward(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        reset_editor_selection(editor_state, watches);
    }
    update_editor_camera(camera, input, delta_time);
}

void handle_mode_transitions(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    if (editor_state->sub_mode != EDITOR_SUB_BROWSE) {
        return;
    }
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_G, GAMEPAD_BUTTON_LEFT_THUMB})) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Move entity"));
        editor_state->saved_position = entity->position;
        editor_state->sub_mode = EDITOR_SUB_DRAG;
    }
    if (toggle_pressed((ToggleBinding){KEY_H, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Resize collision"));
        editor_state->saved_col_offset = entity->collision_offset;
        editor_state->saved_col_size = entity->collision_size;
        editor_state->sub_mode = EDITOR_SUB_HANDLES;
    }
}

void handle_drag_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        entity->position = editor_state->saved_position;
        entity_update_collision(entity);
        undo_history_discard(undo_history);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    entity->position.x += input.left_stick.x * EDITOR_CAMERA_SPEED * delta_time;
    entity->position.y += input.left_stick.y * EDITOR_CAMERA_SPEED * delta_time;
    entity_update_collision(entity);
}

void handle_handle_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        if (blueprint != nullptr) {
            Allocator alloc = allocator_arena(&state->gamedata_arena);
            /* Attrs already exist on this blueprint; attr_set_float updates in-place, no arena growth. */
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_offset_x", entity->collision_offset.x);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_offset_y", entity->collision_offset.y);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_w", entity->collision_size.x);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_h", entity->collision_size.y);
            propagate_collision_to_entities(state, blueprint);
        }
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        entity->collision_offset = editor_state->saved_col_offset;
        entity->collision_size = editor_state->saved_col_size;
        entity_update_collision(entity);
        undo_history_discard(undo_history);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    entity->collision_offset.x += input.left_stick.x * EDITOR_HANDLE_SPEED * delta_time;
    entity->collision_offset.y += input.left_stick.y * EDITOR_HANDLE_SPEED * delta_time;
    entity->collision_size.x += input.right_stick.x * EDITOR_HANDLE_SPEED * delta_time;
    entity->collision_size.y += input.right_stick.y * EDITOR_HANDLE_SPEED * delta_time;
    if (entity->collision_size.x < 0.0F) {
        entity->collision_size.x = 0.0F;
    }
    if (entity->collision_size.y < 0.0F) {
        entity->collision_size.y = 0.0F;
    }
    entity_update_collision(entity);
}

static void apply_attr_delta(GameState *state, EditorState *editor_state, int delta)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
    if (!attr) {
        return;
    }
    if (attr->type == ATTR_INT) {
        attr->value.i += delta;
    } else if (attr->type == ATTR_FLOAT) {
        attr->value.f += (float)delta;
    }
}

void handle_attr_edit_input(GameState *state, EditorState *editor_state, UndoHistory *undo_history, float delta_time)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || attr_idx < 0) {
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        editor_state->attr_hold_dir = 0;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        if (is_blueprint_attr(entity, attr_idx)) {
            Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
            if (blueprint) {
                propagate_collision_to_entities(state, blueprint);
            }
        }
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        editor_state->attr_hold_dir = 0;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        Attribute *attr = attr_at_display_index(state, entity, attr_idx);
        if (attr) {
            if (attr->type == ATTR_INT) {
                attr->value.i = editor_state->saved_attr_int;
            } else if (attr->type == ATTR_FLOAT) {
                attr->value.f = editor_state->saved_attr_float;
            }
        }
        undo_history_discard(undo_history);
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        editor_state->attr_hold_dir = 0;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int multi_delta = read_value_delta();
    if (multi_delta != 0) {
        apply_attr_delta(state, editor_state, multi_delta);
    }
    int held = read_held_dir();
    if (held != editor_state->attr_hold_dir) {
        editor_state->attr_hold_dir = held;
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
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

static int radial_sector_from_stick(Vector2 stick, int item_count)
{
    float magnitude = sqrtf((stick.x * stick.x) + (stick.y * stick.y));
    if (magnitude < RADIAL_STICK_THRESHOLD) {
        return -1;
    }
    float angle = atan2f(stick.y, stick.x);
    float two_pi = 2.0F * (float)M_PI;
    float normalized = fmodf(angle + ((float)M_PI / 2.0F) + two_pi, two_pi);
    return (int)(normalized * (float)item_count / two_pi) % item_count;
}

static const char *radial_label(const EditorState *editor_state, int index)
{
    if (editor_state->radial_context == RADIAL_CTX_TOOLS) {
        static const char *tools[] = {"Grab", "Place", "Handles", "Delete"};
        if (index >= 0 && index < 4) {
            return tools[index];
        }
    }
    return "";
}

void draw_radial_picker(ScreenSize screen, const EditorState *editor_state, Font ui_font)
{
    if (editor_state->sub_mode != EDITOR_SUB_RADIAL) {
        return;
    }
    int center_x = screen.width / 2;
    int center_y = screen.height / 2;
    DrawCircle(center_x, center_y, RADIAL_OUTER_RADIUS + RADIAL_BG_PADDING, debug_bg_color);
    int item_count = editor_state->radial_item_count;
    float sector_deg = RADIAL_FULL_CIRCLE_DEG / (float)item_count;
    for (int index = 0; index < item_count; index++) {
        float start = ((float)index * sector_deg) - RADIAL_NORTH_OFFSET_DEG;
        float end = start + sector_deg;
        bool selected = (index == editor_state->radial_selected);
        Color sector_color = selected ? radial_highlight_color
                                      : (Color){radial_highlight_color.r, radial_highlight_color.g,
                                                radial_highlight_color.b, radial_highlight_color.a / 2};
        DrawRing((Vector2){(float)center_x, (float)center_y}, RADIAL_INNER_RADIUS, RADIAL_OUTER_RADIUS, start, end, 16,
                 sector_color);
        float mid_deg = start + (sector_deg / 2.0F);
        float mid_rad = mid_deg * RADIAL_DEG_TO_RAD;
        float label_radius = (RADIAL_INNER_RADIUS + RADIAL_OUTER_RADIUS) / 2.0F;
        int label_x = center_x + (int)(cosf(mid_rad) * label_radius);
        int label_y = center_y + (int)(sinf(mid_rad) * label_radius);
        const char *label = radial_label(editor_state, index);
        int text_width = measure_ui_text(ui_font, label, RADIAL_FONT_SIZE);
        draw_ui_text(ui_font, label, label_x - (text_width / 2), label_y - (RADIAL_FONT_SIZE / 2), RADIAL_FONT_SIZE,
                     sector_color);
    }
    DrawCircle(center_x, center_y, RADIAL_INNER_RADIUS, debug_bg_color);
}

void handle_radial_input(EditorState *editor_state, InputState input)
{
    editor_state->radial_selected = radial_sector_from_stick(input.left_stick, editor_state->radial_item_count);
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->radial_confirmed = -1;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        editor_state->radial_confirmed = editor_state->radial_selected;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
}

/* --- Word builder -------------------------------------------------------- */

static const char *const word_builder_builtin[] = {
    "chest", "locked",  "magic", "key",   "door",  "open", "closed", "hidden",  "secret", "boss",    "enemy",
    "spawn", "trigger", "zone",  "north", "south", "east", "west",   "bridge",  "gate",   "switch",  "lever",
    "fire",  "ice",     "water", "stone", "wood",  "gold", "silver", "sword",   "shield", "bow",     "arrow",
    "heart", "potion",  "fairy", "dark",  "light", "cave", "forest", "dungeon", "castle", "village", "temple",
    "tower", "path",    "wall",  "floor", "roof",  "big",  "small",  "red",     "blue",   "green",
};
#define WORD_BUILDER_BUILTIN_COUNT (int)(sizeof(word_builder_builtin) / sizeof(word_builder_builtin[0]))

static int word_builder_total_count(const GameState *state)
{
    return 1 + WORD_BUILDER_BUILTIN_COUNT + state->gamedata.blueprints.entries.count;
}

static const char *word_builder_item(const GameState *state, int index)
{
    if (index <= 0) {
        return "[ DONE ]";
    }
    int builtin_index = index - 1;
    if (builtin_index < WORD_BUILDER_BUILTIN_COUNT) {
        return word_builder_builtin[builtin_index];
    }
    int blueprint_index = builtin_index - WORD_BUILDER_BUILTIN_COUNT;
    if (blueprint_index < state->gamedata.blueprints.entries.count) {
        const char *name = attr_get_string(&state->gamedata.blueprints.entries.data[blueprint_index].attrs, "name");
        return name ? name : "?";
    }
    return "";
}

static void word_builder_append(EditorState *editor_state, const char *word)
{
    int word_len = (int)strlen(word);
    int separator = (editor_state->word_builder_len > 0) ? 1 : 0;
    int needed = editor_state->word_builder_len + separator + word_len;
    if (needed >= WORD_BUILDER_BUF_SIZE) {
        return;
    }
    if (separator > 0) {
        editor_state->word_builder_buf[editor_state->word_builder_len] = '_';
        editor_state->word_builder_len++;
    }
    memcpy(editor_state->word_builder_buf + editor_state->word_builder_len, word, (size_t)word_len);
    editor_state->word_builder_len += word_len;
    editor_state->word_builder_buf[editor_state->word_builder_len] = '\0';
}

static void word_builder_pop(EditorState *editor_state)
{
    if (editor_state->word_builder_len == 0) {
        return;
    }
    for (int index = editor_state->word_builder_len - 1; index >= 0; index--) {
        if (editor_state->word_builder_buf[index] == '_') {
            editor_state->word_builder_buf[index] = '\0';
            editor_state->word_builder_len = index;
            return;
        }
    }
    editor_state->word_builder_buf[0] = '\0';
    editor_state->word_builder_len = 0;
}

void draw_word_builder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_WORD_BUILDER) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, "[ Word Builder ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("  %s", editor_state->word_builder_buf), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, WHITE);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int total = word_builder_total_count(state);
    int scroll = editor_state->word_builder_scroll;
    int visible = place_visible_count(screen.height);
    int scroll_offset = scroll - (visible / 2);
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }
    int max_scroll = total - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll_offset > max_scroll) {
        scroll_offset = max_scroll;
    }
    int end = scroll_offset + visible;
    if (end > total) {
        end = total;
    }
    for (int index = scroll_offset; index < end; index++) {
        const char *item = word_builder_item(state, index);
        bool selected = (index == scroll);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", item), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void word_builder_confirm(Diag *diag, GameState *state, EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || attr_idx < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
    if (!attr) {
        return;
    }
    AttrSet *target = &entity->attrs;
    if (is_blueprint_attr(entity, attr_idx)) {
        Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        if (blueprint) {
            target = &blueprint->attrs;
        }
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    AttrStringPair pair = {attr->name.ptr, editor_state->word_builder_buf};
    if (!attr_set_string(&alloc, target, pair)) {
        debug_log(diag->debug, "word builder: attr_set_string failed: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

static void word_builder_navigate(EditorState *editor_state, int total)
{
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        if (editor_state->word_builder_scroll > 0) {
            editor_state->word_builder_scroll--;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        if (editor_state->word_builder_scroll < total - 1) {
            editor_state->word_builder_scroll++;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_Q, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        int new_scroll = editor_state->word_builder_scroll - WORD_BUILDER_PAGE_SIZE;
        editor_state->word_builder_scroll = (new_scroll < 0) ? 0 : new_scroll;
    }
    if (toggle_pressed((ToggleBinding){KEY_E, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        int new_scroll = editor_state->word_builder_scroll + WORD_BUILDER_PAGE_SIZE;
        editor_state->word_builder_scroll = (new_scroll >= total) ? total - 1 : new_scroll;
    }
}

void handle_word_builder_input(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    int total = word_builder_total_count(state);
    word_builder_navigate(editor_state, total);
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        if (editor_state->word_builder_scroll == 0) {
            word_builder_confirm(diag, state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else {
            word_builder_append(editor_state, word_builder_item(state, editor_state->word_builder_scroll));
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        if (editor_state->word_builder_len > 0) {
            word_builder_pop(editor_state);
        } else {
            undo_history_discard(undo_history);
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        }
    }
}

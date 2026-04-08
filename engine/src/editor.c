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
#include "gamedata.h"
#include "input.h"
#include "level.h"
#include "map.h"
#include "rect.h"
#include "rule.h"
#include "str.h"
#include "strv.h"
#include "undo.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

#define RADIX_DECIMAL 10

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static const Color handle_color = {0, 255, 255, 255};            /* cyan: collision handles */
static const int place_preview_alpha = 128;                      /* alpha for texture ghost during placement */
static const Color place_ghost_color = {100, 255, 100, 180};     /* semi-transparent green: place preview */
static const Color radial_highlight_color = {255, 200, 50, 200}; /* amber: selected radial sector */
static const Color attr_override_color = {255, 200, 50, 255};    /* amber: overrides blueprint default */
static const Color attr_custom_color = {100, 220, 100, 255};     /* green: instance-only attribute */
static const Color attr_dimmed_color = {120, 130, 140, 255};     /* gray: overridden by instance */

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

void draw_hints_bar(bool editor_mode, const EditorState *editor_state, bool is_dirty, ScreenSize screen, Font ui_font)
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
            hints =
                "A/Ent: Pick / Done  |  B/Esc: Undo / Cancel  |  X/Del: Keyboard  |  Up/Dn: Scroll  |  L1/Q R1/E: Pg";
        } else if (editor_state->sub_mode == EDITOR_SUB_GAMEPAD_KB) {
            hints = "Stick: Pick  |  A/Ent: Select  |  B/Esc: Back / Backspace  |  X/Del: Exit to Words";
        } else if (editor_state->sub_mode == EDITOR_SUB_FUZZY_FINDER) {
            hints = "A/Ent: Pick / New  |  B/Esc: Cancel  |  Up/Dn: Scroll  |  L1/Q: PgUp  |  R1/E: PgDn";
        } else {
            hints = "F5: Play  |  F9/Y: Save  |  Tab/Sel: Tools  |  A: Sel/Drill  |  B: Desel  |  Up/Down: Nav  |  "
                    "X: Del/Rm  |  ]/R2: Type/Props  |  L2: Watch  |  P/R1: Place  |  Stick: Pan";
        }
    } else {
        hints = "F5: Editor  |  F3: Debug  |  F4: Fonts";
    }
    int text_y = bar_y + ((HINTS_BAR_HEIGHT - HINTS_FONT_SIZE) / 2);
    draw_ui_text(ui_font, hints, DEBUG_MARGIN, text_y, HINTS_FONT_SIZE, debug_text_color);
    if (editor_mode && is_dirty) {
        const char *dirty_label = "[*]";
        int dirty_width = measure_ui_text(ui_font, dirty_label, HINTS_FONT_SIZE);
        draw_ui_text(ui_font, dirty_label, screen.width - dirty_width - DEBUG_MARGIN, text_y, HINTS_FONT_SIZE, WHITE);
    }
}

void draw_toast(const EditorState *editor_state, ScreenSize screen, Font ui_font)
{
    if (editor_state->toast_timer <= 0.0F || editor_state->toast_text.len == 0) {
        return;
    }
    float alpha = (editor_state->toast_timer < TOAST_FADE_TIME) ? (editor_state->toast_timer / TOAST_FADE_TIME) : 1.0F;
    unsigned char alpha_byte = (unsigned char)(alpha * ALPHA_MAX);
    const char *text = TextFormat("%.*s", (int)editor_state->toast_text.len, editor_state->toast_text.ptr);
    int text_width = measure_ui_text(ui_font, text, TOAST_FONT_SIZE);
    int pos_x = (screen.width - text_width) / 2;
    int pos_y = DEBUG_MARGIN;
    DrawRectangle(pos_x - DEBUG_MARGIN, pos_y, text_width + (DEBUG_MARGIN * 2), TOAST_FONT_SIZE + DEBUG_MARGIN,
                  (Color){debug_bg_color.r, debug_bg_color.g, debug_bg_color.b, alpha_byte});
    draw_ui_text(ui_font, text, pos_x, pos_y, TOAST_FONT_SIZE,
                 (Color){debug_text_color.r, debug_text_color.g, debug_text_color.b, alpha_byte});
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

static int total_attr_count(const GameState *state, const Entity *entity);

static void draw_attr_section(Font font,
                              const AttrSet *set,
                              bool is_instance_section,
                              const AttrSet *other_set,
                              int panel_x,
                              int *y_offset,
                              int base_index,
                              int selected_attr_index)
{
    for (int index = 0; index < set->entries.count; index++) {
        const Attribute *attr = &set->entries.data[index];
        bool selected = (base_index + index == selected_attr_index);
        bool exists_in_other = other_set && attr_get(other_set, attr->name.ptr);
        const char *prefix = "  ";
        Color base_color = debug_text_color;
        if (is_instance_section) {
            prefix = exists_in_other ? "> " : "+ ";
            base_color = exists_in_other ? attr_override_color : attr_custom_color;
        } else if (exists_in_other) {
            base_color = attr_dimmed_color;
        }
        Color text_color = selected ? WHITE : base_color;
        draw_ui_text(font, TextFormat("%s%s: %s", prefix, attr->name.ptr, attr_display_value(attr)),
                     panel_x + DEBUG_MARGIN, *y_offset, EDITOR_PANEL_FONT_SIZE, text_color);
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
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    /* --- Tree section: parent row + children + ADD CHILD --- */
    const Blueprint *panel_blueprint = blueprint_find(&state->gamedata.blueprints, entity->blueprint_name.ptr);
    int tree_idx = editor_state->selected_tree_index;
    int tree_row = 0;
    if (entity->parent_index >= 0) {
        const Entity *parent = &state->gamedata.current_level.entities.data[entity->parent_index];
        Color parent_color = (tree_idx == tree_row) ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("  ^ parent: %s (id: %d)", parent->blueprint_name.ptr, parent->id),
                     panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, parent_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        tree_row++;
    }
    draw_ui_text(font, "--- children ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    if (panel_blueprint) {
        for (int child_idx = 0; child_idx < panel_blueprint->children.count; child_idx++) {
            const BlueprintChild *child = &panel_blueprint->children.data[child_idx];
            Color child_color = (tree_idx == tree_row) ? WHITE : debug_text_color;
            const char *tag_str = (child->tag.len > 0) ? child->tag.ptr : "";
            draw_ui_text(font,
                         TextFormat("  [%s]  \"%s\"  (%.0f, %.0f)", child->blueprint_name.ptr, tag_str, child->offset.x,
                                    child->offset.y),
                         panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, child_color);
            y_offset += EDITOR_PANEL_LINE_HEIGHT;
            tree_row++;
        }
    }
    Color add_child_color = (tree_idx == tree_row) ? WHITE : attr_custom_color;
    draw_ui_text(font, "  [ + ADD CHILD ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, add_child_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    draw_ui_text(font, "--- instance ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    int sel_attr = editor_state->selected_attr_index;
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    draw_attr_section(font, &entity->attrs, true, defaults, panel_x, &y_offset, 0, sel_attr);
    int sentinel_index = total_attr_count(state, entity);
    Color sentinel_color = (sel_attr == sentinel_index) ? WHITE : attr_custom_color;
    draw_ui_text(font, "  [ + ADD ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, sentinel_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    if (defaults) {
        draw_ui_text(font, "--- blueprint ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                     debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        int blueprint_base = entity->attrs.entries.count;
        draw_attr_section(font, defaults, false, &entity->attrs, panel_x, &y_offset, blueprint_base, sel_attr);
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

/* --- Tree section helpers (parent/child/ADD CHILD navigation) --- */

static int tree_section_total(const Entity *entity, const Blueprint *blueprint)
{
    int parent_row_exists = (entity->parent_index >= 0) ? 1 : 0;
    int child_count = blueprint ? blueprint->children.count : 0;
    return parent_row_exists + child_count + 1; /* +1 for ADD CHILD sentinel */
}

static bool tree_is_parent_row(const Entity *entity, int tree_index)
{
    return entity->parent_index >= 0 && tree_index == 0;
}

static bool tree_is_add_child_row(const Entity *entity, const Blueprint *blueprint, int tree_index)
{
    return tree_index == tree_section_total(entity, blueprint) - 1;
}

/* Returns the index into blueprint->children for a child row, or -1 if not a child row. */
static int tree_child_index(const Entity *entity, int tree_index)
{
    int parent_row_exists = (entity->parent_index >= 0) ? 1 : 0;
    int child_index = tree_index - parent_row_exists;
    if (child_index < 0) {
        return -1;
    }
    return child_index;
}

/* Find the entity index of a child matching parent + blueprint_name + tag. Returns -1 if not found. */
static int find_child_entity(const Level *level, int parent_index, const char *blueprint_name, const char *tag)
{
    for (int index = 0; index < level->entities.count; index++) {
        const Entity *entity = &level->entities.data[index];
        if (entity->parent_index != parent_index) {
            continue;
        }
        if (strcmp(entity->blueprint_name.ptr, blueprint_name) != 0) {
            continue;
        }
        if (tag[0] == '\0' && entity->tag.len == 0) {
            return index;
        }
        if (entity->tag.len > 0 && strcmp(entity->tag.ptr, tag) == 0) {
            return index;
        }
    }
    return -1;
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

static void fuzzy_finder_build_items(GameState *state, EditorState *editor_state);

static void select_entity_and_pan(EditorState *editor_state, Camera2D *camera, const Level *level, int entity_index)
{
    editor_state->selected_entity_index = entity_index;
    editor_state->selected_tree_index = -1;
    editor_state->selected_attr_index = -1;
    camera->target = level->entities.data[entity_index].position;
}

static void handle_tree_select(GameState *state, Camera2D *camera, EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    int tree_idx = editor_state->selected_tree_index;
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, entity->blueprint_name.ptr);

    if (tree_is_parent_row(entity, tree_idx)) {
        select_entity_and_pan(editor_state, camera, &state->gamedata.current_level, entity->parent_index);
        return;
    }
    if (tree_is_add_child_row(entity, blueprint, tree_idx)) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->adding_child = true;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    if (!blueprint) {
        return;
    }
    int child_idx = tree_child_index(entity, tree_idx);
    if (child_idx < 0 || child_idx >= blueprint->children.count) {
        return;
    }
    const BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *tag = child->tag.len > 0 ? child->tag.ptr : "";
    int child_entity_idx = find_child_entity(&state->gamedata.current_level, sel, child->blueprint_name.ptr, tag);
    if (child_entity_idx >= 0) {
        select_entity_and_pan(editor_state, camera, &state->gamedata.current_level, child_entity_idx);
    }
}

static void
handle_browse_select(GameState *state, Camera2D *camera, EditorState *editor_state, UndoHistory *undo_history)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        editor_state->selected_entity_index = find_nearest_entity(&state->gamedata.current_level, camera->target);
        editor_state->selected_attr_index = -1;
        editor_state->selected_tree_index = -1;
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];

    if (editor_state->selected_tree_index >= 0) {
        handle_tree_select(state, camera, editor_state);
        return;
    }

    int attr_idx = editor_state->selected_attr_index;
    if (attr_idx < 0) {
        return;
    }
    int sentinel_index = total_attr_count(state, entity);
    if (attr_idx == sentinel_index) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->adding_attr = true;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
    if (!attr) {
        return;
    }
    if (attr->type == ATTR_BOOL) {
        attr->value.b = !attr->value.b;
        if (is_blueprint_attr(entity, attr_idx)) {
            Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
            if (blueprint) {
                propagate_collision_to_entities(state, blueprint);
            }
        }
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Toggle attribute"));
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

static void handle_browse_cancel(EditorState *editor_state)
{
    if (editor_state->selected_attr_index >= 0) {
        editor_state->selected_attr_index = -1;
    } else if (editor_state->selected_tree_index >= 0) {
        editor_state->selected_tree_index = -1;
    } else {
        editor_state->selected_entity_index = -1;
    }
}

static void handle_browse_navigate(const GameState *state, EditorState *editor_state, int direction)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, entity->blueprint_name.ptr);
    int tree_total = tree_section_total(entity, blueprint);
    int attr_total = total_attr_count(state, entity) + 1; /* +1 for [ + ADD ] sentinel */

    if (editor_state->selected_tree_index >= 0) {
        /* Currently in tree section */
        int next = editor_state->selected_tree_index + direction;
        if (next < 0) {
            /* Wrap up from tree top → attr section bottom */
            editor_state->selected_tree_index = -1;
            editor_state->selected_attr_index = attr_total - 1;
        } else if (next >= tree_total) {
            /* Wrap down from tree bottom → attr section top */
            editor_state->selected_tree_index = -1;
            editor_state->selected_attr_index = 0;
        } else {
            editor_state->selected_tree_index = next;
        }
    } else if (editor_state->selected_attr_index >= 0) {
        /* Currently in attr section */
        int next = editor_state->selected_attr_index + direction;
        if (next < 0) {
            /* Wrap up from attr top → tree section bottom */
            editor_state->selected_attr_index = -1;
            editor_state->selected_tree_index = tree_total - 1;
        } else if (next >= attr_total) {
            /* Wrap down from attr bottom → tree section top */
            editor_state->selected_attr_index = -1;
            editor_state->selected_tree_index = 0;
        } else {
            editor_state->selected_attr_index = next;
        }
    } else {
        /* Nothing selected — enter tree section */
        if (direction > 0) {
            editor_state->selected_tree_index = 0;
        } else {
            editor_state->selected_tree_index = tree_total - 1;
        }
    }
}

static const AttrType attr_type_radial_order[] = {ATTR_FLOAT, ATTR_INT, ATTR_BOOL, ATTR_STRING};

typedef struct {
    float as_float;
    int as_int;
    bool as_bool;
} AttrConvertedValues;

static AttrConvertedValues attr_extract_values(const Attribute *attr, Allocator *alloc)
{
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
        str_free(alloc, &freed);
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
        Str new_str = {0};
        (void)str_from_cstr(alloc, &new_str, formatted);
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

static void
dispatch_attr_type_change(GameState *state, EditorState *editor_state, int confirmed, UndoHistory *undo_history)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || attr_idx < 0 || confirmed < 0 || confirmed >= 4) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
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
                           strv_from_cstr("Change attr type"));
}

/* --- Targeted propagation helpers for blueprint child edits --- */

static void propagate_child_tag(GameState *state, const Blueprint *blueprint, int child_idx, const char *old_tag)
{
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    const BlueprintChild *child = &blueprint->children.data[child_idx];
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        Entity *entity = &state->gamedata.current_level.entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        int child_entity = find_child_entity(&state->gamedata.current_level, index, child->blueprint_name.ptr, old_tag);
        if (child_entity >= 0) {
            Allocator alloc = allocator_arena(&state->gamedata_arena);
            (void)str_from_cstr(&alloc, &state->gamedata.current_level.entities.data[child_entity].tag, child->tag.ptr);
        }
    }
}

static void propagate_child_offset(GameState *state, const Blueprint *blueprint, int child_idx)
{
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    const BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *tag = child->tag.len > 0 ? child->tag.ptr : "";
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        Entity *entity = &state->gamedata.current_level.entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        int child_entity = find_child_entity(&state->gamedata.current_level, index, child->blueprint_name.ptr, tag);
        if (child_entity >= 0) {
            Entity *child_ent = &state->gamedata.current_level.entities.data[child_entity];
            child_ent->offset = child->offset;
            child_ent->position = (Vector2){entity->position.x + child->offset.x, entity->position.y + child->offset.y};
            entity_update_collision(child_ent);
        }
    }
}

static void dispatch_child_props(GameState *state, EditorState *editor_state, int confirmed)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || confirmed < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
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

static void confirm_child_tag_edit(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    (void)diag;
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
    int child_idx = editor_state->child_edit_index;
    if (!blueprint || child_idx < 0 || child_idx >= blueprint->children.count) {
        editor_state->editing_child_tag = false;
        return;
    }
    BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *old_tag = child->tag.len > 0 ? child->tag.ptr : "";
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    (void)str_from_cstr(&alloc, &child->tag, editor_state->word_builder_buf);
    propagate_child_tag(state, blueprint, child_idx, old_tag);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit child tag"));
    editor_state->editing_child_tag = false;
}

static void
confirm_child_offset_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int new_value)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
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

static void
dispatch_radial_confirm(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int confirmed = editor_state->radial_confirmed;
    editor_state->radial_confirmed = -1;
    if (editor_state->radial_context == RADIAL_CTX_TOOLS) {
        int sel = editor_state->selected_entity_index;
        if (confirmed == 0 && sel >= 0) { /* Grab */
            editor_state->saved_position = state->gamedata.current_level.entities.data[sel].position;
            editor_state->sub_mode = EDITOR_SUB_DRAG;
        } else if (confirmed == 1) { /* Place */
            if (state->gamedata.blueprints.entries.count > 0) {
                editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
                editor_state->sub_mode = EDITOR_SUB_PLACE;
            }
        } else if (confirmed == 2 && sel >= 0) { /* Handles */
            editor_state->saved_col_offset = state->gamedata.current_level.entities.data[sel].collision_offset;
            editor_state->saved_col_size = state->gamedata.current_level.entities.data[sel].collision_size;
            editor_state->sub_mode = EDITOR_SUB_HANDLES;
        } else if (confirmed == 3) { /* Delete */
            delete_selected_entity(state, editor_state, watches);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Delete entity"));
        }
    } else if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
        dispatch_attr_type_change(state, editor_state, confirmed, undo_history);
    } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
        dispatch_child_props(state, editor_state, confirmed);
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

typedef struct {
    const char *blueprint_name;
    const char *tag;
} ChildMatch;

static void propagate_child_remove(GameState *state, EditorState *editor_state, const char *bp_name, ChildMatch match)
{
    Level *level = &state->gamedata.current_level;
    int count = level->entities.count;
    SCRATCH_SCOPE(&state->scratch_arena);
    bool *is_deleted = arena_alloc(&state->scratch_arena, (size_t)count * sizeof(bool));
    memset(is_deleted, 0, (size_t)count * sizeof(bool));

    for (int index = 0; index < count; index++) {
        const Entity *entity = &level->entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        int child_entity = find_child_entity(level, index, match.blueprint_name, match.tag);
        if (child_entity >= 0) {
            is_deleted[child_entity] = true;
        }
    }
    mark_deleted_descendants(level, is_deleted, count);

    for (int index = 0; index < count; index++) {
        if (is_deleted[index]) {
            int entity_id = level->entities.data[index].id;
            map_int_str_remove(&state->gamedata.entity_blueprints, entity_id);
            map_entity_ruleset_remove(&state->gamedata.rule_table, entity_id);
        }
    }
    int *new_index_map = arena_alloc(&state->scratch_arena, (size_t)count * sizeof(int));
    int new_count = 0;
    for (int index = 0; index < count; index++) {
        new_index_map[index] = is_deleted[index] ? -1 : new_count++;
    }
    for (int index = 0; index < count; index++) {
        if (!is_deleted[index]) {
            int parent = level->entities.data[index].parent_index;
            if (parent >= 0) {
                level->entities.data[index].parent_index = new_index_map[parent];
            }
        }
    }
    int write = 0;
    for (int index = 0; index < count; index++) {
        if (!is_deleted[index]) {
            level->entities.data[write++] = level->entities.data[index];
        }
    }
    level->entities.count = write;
    if (state->gamedata.player_index >= 0) {
        state->gamedata.player_index = new_index_map[state->gamedata.player_index];
    }
    editor_state->selected_tree_index = -1;
}

static void
remove_blueprint_child(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int child_idx)
{
    int sel = editor_state->selected_entity_index;
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
    if (!blueprint || child_idx < 0 || child_idx >= blueprint->children.count) {
        return;
    }
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    ChildMatch match = {
        .blueprint_name = blueprint->children.data[child_idx].blueprint_name.ptr,
        .tag = blueprint->children.data[child_idx].tag.len > 0 ? blueprint->children.data[child_idx].tag.ptr : "",
    };

    propagate_child_remove(state, editor_state, bp_name, match);

    /* Remove the BlueprintChild from the vec (swap with last) */
    int last = blueprint->children.count - 1;
    if (child_idx < last) {
        blueprint->children.data[child_idx] = blueprint->children.data[last];
    }
    blueprint->children.count--;

    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Remove child"));
}

static void add_blueprint_child(Diag *diag,
                                GameState *state,
                                EditorState *editor_state,
                                UndoHistory *undo_history,
                                const char *child_blueprint_name,
                                TextureLookupFn texture_lookup,
                                void *texture_user_data)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
    if (!blueprint) {
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    BlueprintChild new_child = {0};
    (void)str_from_cstr(&alloc, &new_child.blueprint_name, child_blueprint_name);
    new_child.offset = (Vector2){0, 0};
    if (!vec_blueprint_child_push(&blueprint->children, new_child)) {
        return;
    }
    int child_idx = blueprint->children.count - 1;

    /* Spawn for all instances of this blueprint */
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *parent = &state->gamedata.current_level.entities.data[index];
        if (strcmp(parent->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        if (!level_spawn_single_child(diag, &state->gamedata.current_level, index, &blueprint->children.data[child_idx],
                                      &state->gamedata.blueprints, texture_lookup, texture_user_data, &alloc)) {
            debug_log(diag->debug, "add child: %s", error_get(diag->error));
            error_clear(diag->error);
        }
    }
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Add child"));
}

static void
handle_browse_delete(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int del_sel = editor_state->selected_entity_index;
    if (del_sel < 0) {
        return;
    }

    /* Tree section: X on child row → remove blueprint child */
    int tree_idx = editor_state->selected_tree_index;
    if (tree_idx >= 0) {
        const Entity *entity = &state->gamedata.current_level.entities.data[del_sel];
        const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, entity->blueprint_name.ptr);
        if (blueprint && !tree_is_parent_row(entity, tree_idx) && !tree_is_add_child_row(entity, blueprint, tree_idx)) {
            int child_idx = tree_child_index(entity, tree_idx);
            if (child_idx >= 0 && child_idx < blueprint->children.count) {
                remove_blueprint_child(state, editor_state, undo_history, child_idx);
            }
        }
        return;
    }

    /* Attr section: X on instance attr → remove attr; otherwise → delete entity */
    int del_attr = editor_state->selected_attr_index;
    int del_sentinel = total_attr_count(state, &state->gamedata.current_level.entities.data[del_sel]);
    if (del_attr >= 0 && del_attr < del_sentinel &&
        !is_blueprint_attr(&state->gamedata.current_level.entities.data[del_sel], del_attr)) {
        Entity *del_entity = &state->gamedata.current_level.entities.data[del_sel];
        Attribute *del_target = &del_entity->attrs.entries.data[del_attr];
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        attr_remove(&alloc, &del_entity->attrs, del_target->name.ptr);
        editor_state->selected_attr_index = -1;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Remove attribute"));
    } else if (del_attr < 0 || (del_attr < del_sentinel &&
                                is_blueprint_attr(&state->gamedata.current_level.entities.data[del_sel], del_attr))) {
        delete_selected_entity(state, editor_state, watches);
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Delete entity"));
    }
}

static void reset_editor_selection(EditorState *editor_state, WatchList *watches)
{
    editor_state->selected_entity_index = -1;
    editor_state->selected_attr_index = -1;
    editor_state->selected_tree_index = -1;
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
        handle_browse_navigate(state, editor_state, 1);
    }
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        handle_browse_navigate(state, editor_state, -1);
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT_SHIFT, GAMEPAD_BUTTON_LEFT_TRIGGER_2})) {
        toggle_watch(editor_state, watches);
    }
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        handle_browse_delete(state, editor_state, watches, undo_history);
    }
    if (toggle_pressed((ToggleBinding){KEY_P, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        if (state->gamedata.blueprints.entries.count > 0) {
            editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_PLACE;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT_BRACKET, GAMEPAD_BUTTON_RIGHT_TRIGGER_2})) {
        if (editor_state->selected_attr_index >= 0) {
            editor_state->radial_selected = -1;
            editor_state->radial_confirmed = -1;
            editor_state->radial_item_count = 4;
            editor_state->radial_context = RADIAL_CTX_ATTR_TYPE;
            editor_state->sub_mode = EDITOR_SUB_RADIAL;
        } else if (editor_state->selected_tree_index >= 0) {
            int r2_sel = editor_state->selected_entity_index;
            const Entity *r2_entity = &state->gamedata.current_level.entities.data[r2_sel];
            const Blueprint *r2_bp = blueprint_find(&state->gamedata.blueprints, r2_entity->blueprint_name.ptr);
            int r2_tree = editor_state->selected_tree_index;
            if (r2_bp && !tree_is_parent_row(r2_entity, r2_tree) && !tree_is_add_child_row(r2_entity, r2_bp, r2_tree)) {
                editor_state->radial_selected = -1;
                editor_state->radial_confirmed = -1;
                editor_state->radial_item_count = 3;
                editor_state->radial_context = RADIAL_CTX_CHILD_PROPS;
                editor_state->child_edit_index = tree_child_index(r2_entity, r2_tree);
                editor_state->sub_mode = EDITOR_SUB_RADIAL;
            }
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT})) {
        undo_history_step_back(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        reset_editor_selection(editor_state, watches);
        editor_state->toast_text = undo_history_description(undo_history);
        editor_state->toast_timer = TOAST_DURATION;
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT})) {
        undo_history_step_forward(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        reset_editor_selection(editor_state, watches);
        editor_state->toast_text = undo_history_description(undo_history);
        editor_state->toast_timer = TOAST_DURATION;
    }
    update_editor_camera(camera, input, delta_time);
}

void handle_mode_transitions(GameState *state, EditorState *editor_state)
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
        editor_state->saved_position = entity->position;
        editor_state->sub_mode = EDITOR_SUB_DRAG;
    }
    if (toggle_pressed((ToggleBinding){KEY_H, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
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
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Move entity"));
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        entity->position = editor_state->saved_position;
        entity_update_collision(entity);
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
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Resize collision"));
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        entity->collision_offset = editor_state->saved_col_offset;
        entity->collision_size = editor_state->saved_col_size;
        entity_update_collision(entity);
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

static void
handle_child_offset_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history, float delta_time);

void handle_attr_edit_input(GameState *state, EditorState *editor_state, UndoHistory *undo_history, float delta_time)
{
    if (editor_state->editing_child_offset) {
        handle_child_offset_edit(state, editor_state, undo_history, delta_time);
        return;
    }
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
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Edit attribute"));
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

static void reset_attr_hold(EditorState *editor_state)
{
    editor_state->attr_hold_total = 0.0F;
    editor_state->attr_hold_subtick = 0.0F;
    editor_state->attr_hold_dir = 0;
}

static void
handle_child_offset_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history, float delta_time)
{
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        confirm_child_offset_edit(state, editor_state, undo_history, editor_state->saved_attr_int);
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->editing_child_offset = false;
        reset_attr_hold(editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int multi_delta = read_value_delta();
    if (multi_delta != 0) {
        editor_state->saved_attr_int += multi_delta;
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
        editor_state->saved_attr_int += held;
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
        static const char *const tools[] = {"Grab", "Place", "Handles", "Delete"};
        if (index >= 0 && index < 4) {
            return tools[index];
        }
    } else if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
        static const char *const types[] = {"Float", "Int", "Bool", "String"};
        if (index >= 0 && index < 4) {
            return types[index];
        }
    } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
        static const char *const props[] = {"Tag", "Offset X", "Offset Y"};
        if (index >= 0 && index < 3) {
            return props[index];
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
    if (editor_state->sub_mode != EDITOR_SUB_WORD_BUILDER && editor_state->sub_mode != EDITOR_SUB_GAMEPAD_KB) {
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

static bool add_attr_by_name(Diag *diag, GameState *state, EditorState *editor_state, const char *name)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || !name || name[0] == '\0') {
        return false;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (attr_get(&entity->attrs, name)) {
        return false;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    if (!attr_set_int(&alloc, &entity->attrs, name, 0)) {
        debug_log(diag->debug, "add attr: attr_set_int failed: %s", error_get(diag->error));
        error_clear(diag->error);
        return false;
    }
    return true;
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
        if (editor_state->word_builder_scroll == 0 && editor_state->editing_child_tag) {
            confirm_child_tag_edit(diag, state, editor_state, undo_history);
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 && editor_state->adding_attr) {
            add_attr_by_name(diag, state, editor_state, editor_state->word_builder_buf);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Add attribute"));
            editor_state->adding_attr = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0) {
            word_builder_confirm(diag, state, editor_state);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Edit string"));
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else {
            word_builder_append(editor_state, word_builder_item(state, editor_state->word_builder_scroll));
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        editor_state->keyboard_group = -1;
        editor_state->keyboard_selected = -1;
        editor_state->sub_mode = EDITOR_SUB_GAMEPAD_KB;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        if (editor_state->word_builder_len > 0) {
            word_builder_pop(editor_state);
        } else {
            editor_state->adding_attr = false;
            editor_state->editing_child_tag = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        }
    }
}

/* --- Fuzzy finder (name picker) --- */

static int compare_cstr_ptrs(const void *lhs, const void *rhs)
{
    return strcmp(*(const char *const *)lhs, *(const char *const *)rhs);
}

static bool fuzzy_finder_contains(const char **items, int count, const char *name)
{
    for (int index = 0; index < count; index++) {
        if (strcmp(items[index], name) == 0) {
            return true;
        }
    }
    return false;
}

static void fuzzy_finder_try_add(const char **items, int *count, const char *name)
{
    if (name && name[0] != '\0' && !fuzzy_finder_contains(items, *count, name)) {
        items[(*count)++] = name;
    }
}

static void fuzzy_finder_collect_blueprint_names(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        fuzzy_finder_try_add(items, count, attr_get_string(&gamedata->blueprints.entries.data[index].attrs, "name"));
    }
}

static void fuzzy_finder_collect_flag_names(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->flags.names.count; index++) {
        fuzzy_finder_try_add(items, count, gamedata->flags.names.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_level_names(const GamedataState *gamedata, const char **items, int *count)
{
    fuzzy_finder_try_add(items, count, gamedata->current_level.name.ptr);
    for (int index = 0; index < gamedata->other_levels.count; index++) {
        fuzzy_finder_try_add(items, count, gamedata->other_levels.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_entity_tags(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        fuzzy_finder_try_add(items, count, gamedata->current_level.entities.data[index].tag.ptr);
    }
}

static void fuzzy_finder_collect_attr_names_from_set(const AttrSet *attrs, const char **items, int *count)
{
    for (int index = 0; index < attrs->entries.count; index++) {
        fuzzy_finder_try_add(items, count, attrs->entries.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_all_attr_names(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        fuzzy_finder_collect_attr_names_from_set(&gamedata->blueprints.entries.data[index].attrs, items, count);
    }
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        fuzzy_finder_collect_attr_names_from_set(&gamedata->current_level.entities.data[index].attrs, items, count);
    }
}

static int fuzzy_finder_max_item_count(const GamedataState *gamedata)
{
    int max_count = gamedata->blueprints.entries.count + gamedata->flags.names.count + 1 +
                    gamedata->other_levels.count + gamedata->current_level.entities.count;
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        max_count += gamedata->blueprints.entries.data[index].attrs.entries.count;
    }
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        max_count += gamedata->current_level.entities.data[index].attrs.entries.count;
    }
    return max_count;
}

static void fuzzy_finder_build_items(GameState *state, EditorState *editor_state)
{
    int max_count = fuzzy_finder_max_item_count(&state->gamedata);
    const char **items = (const char **)arena_alloc(&state->gamedata_arena, sizeof(const char *) * (size_t)max_count);
    if (!items) {
        editor_state->fuzzy_finder_items = nullptr;
        editor_state->fuzzy_finder_item_count = 0;
        return;
    }
    int count = 0;

    fuzzy_finder_collect_blueprint_names(&state->gamedata, items, &count);
    fuzzy_finder_collect_flag_names(&state->gamedata, items, &count);
    fuzzy_finder_collect_level_names(&state->gamedata, items, &count);
    fuzzy_finder_collect_entity_tags(&state->gamedata, items, &count);
    fuzzy_finder_collect_all_attr_names(&state->gamedata, items, &count);

    qsort((void *)items, (size_t)count, sizeof(const char *), compare_cstr_ptrs);

    editor_state->fuzzy_finder_items = items;
    editor_state->fuzzy_finder_item_count = count;
}

static const char *fuzzy_finder_item(const EditorState *editor_state, int index)
{
    if (index <= 0) {
        return "[ NEW... ]";
    }
    int item_index = index - 1;
    if (item_index < editor_state->fuzzy_finder_item_count) {
        return editor_state->fuzzy_finder_items[item_index];
    }
    return "";
}

static int fuzzy_finder_total_count(const EditorState *editor_state)
{
    return 1 + editor_state->fuzzy_finder_item_count;
}

void draw_fuzzy_finder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_FUZZY_FINDER) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, TextFormat("[ Name Picker ] (%d)", editor_state->fuzzy_finder_item_count),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int total = fuzzy_finder_total_count(editor_state);
    int scroll = editor_state->fuzzy_finder_scroll;
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
        const char *item = fuzzy_finder_item(editor_state, index);
        bool selected = (index == scroll);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", item), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void fuzzy_finder_navigate(EditorState *editor_state, int total)
{
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        if (editor_state->fuzzy_finder_scroll > 0) {
            editor_state->fuzzy_finder_scroll--;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        if (editor_state->fuzzy_finder_scroll < total - 1) {
            editor_state->fuzzy_finder_scroll++;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_Q, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        int new_scroll = editor_state->fuzzy_finder_scroll - FUZZY_FINDER_PAGE_SIZE;
        editor_state->fuzzy_finder_scroll = (new_scroll < 0) ? 0 : new_scroll;
    }
    if (toggle_pressed((ToggleBinding){KEY_E, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        int new_scroll = editor_state->fuzzy_finder_scroll + FUZZY_FINDER_PAGE_SIZE;
        editor_state->fuzzy_finder_scroll = (new_scroll >= total) ? total - 1 : new_scroll;
    }
}

static void fuzzy_finder_enter_word_builder(GameState *state, EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    const char *existing = "";
    if (sel >= 0 && attr_idx >= 0) {
        Entity *entity = &state->gamedata.current_level.entities.data[sel];
        const Attribute *attr = attr_at_display_index(state, entity, attr_idx);
        if (attr && attr->type == ATTR_STRING && attr->value.str.ptr) {
            existing = attr->value.str.ptr;
        }
    }
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

static void fuzzy_finder_confirm(Diag *diag, GameState *state, EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || attr_idx < 0) {
        return;
    }
    const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
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
    AttrStringPair pair = {attr->name.ptr, chosen};
    if (!attr_set_string(&alloc, target, pair)) {
        debug_log(diag->debug, "fuzzy finder: attr_set_string failed: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

void handle_fuzzy_finder_input(Diag *diag,
                               GameState *state,
                               EditorState *editor_state,
                               UndoHistory *undo_history,
                               TextureLookupFn texture_lookup,
                               void *texture_user_data)
{
    int total = fuzzy_finder_total_count(editor_state);
    fuzzy_finder_navigate(editor_state, total);
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        if (editor_state->adding_child) {
            if (editor_state->fuzzy_finder_scroll == 0) {
                fuzzy_finder_enter_word_builder(state, editor_state);
            } else {
                const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
                add_blueprint_child(diag, state, editor_state, undo_history, chosen, texture_lookup, texture_user_data);
                editor_state->adding_child = false;
                editor_state->sub_mode = EDITOR_SUB_BROWSE;
            }
        } else if (editor_state->adding_attr) {
            if (editor_state->fuzzy_finder_scroll == 0) {
                fuzzy_finder_enter_word_builder(state, editor_state);
            } else {
                const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
                add_attr_by_name(diag, state, editor_state, chosen);
                undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                       strv_from_cstr("Add attribute"));
                editor_state->adding_attr = false;
                editor_state->sub_mode = EDITOR_SUB_BROWSE;
            }
        } else if (editor_state->fuzzy_finder_scroll == 0) {
            fuzzy_finder_enter_word_builder(state, editor_state);
        } else {
            fuzzy_finder_confirm(diag, state, editor_state);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Pick name"));
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->adding_attr = false;
        editor_state->adding_child = false;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
}

/* --- Gamepad keyboard (two-level radial character picker) --- */

static const char keyboard_groups[KEYBOARD_GROUP_COUNT][KEYBOARD_MAX_CHARS_PER_GROUP] = {
    {'a', 'e', 'i', 'o', 'u'},   /* vowels */
    {'t', 'n', 's', 'r', '\0'},  /* common 1 */
    {'l', 'd', 'h', 'c', '\0'},  /* common 2 */
    {'m', 'p', 'f', 'g', '\0'},  /* medium */
    {'b', 'w', 'v', 'k', '\0'},  /* less common */
    {'j', 'x', 'y', 'z', 'q'},   /* rare */
    {'0', '1', '2', '3', '4'},   /* digits low */
    {'5', '6', '7', '8', '9'},   /* digits high */
    {'_', '-', '.', '\0', '\0'}, /* punctuation */
};

static const int keyboard_group_sizes[KEYBOARD_GROUP_COUNT] = {5, 4, 4, 4, 4, 5, 5, 5, 3};

static const char *const keyboard_group_labels[KEYBOARD_GROUP_COUNT] = {
    "aeiou", "tnsr", "ldhc", "mpfg", "bwvk", "jxyzq", "01234", "56789", "_-.",
};

static void keyboard_type_char(EditorState *editor_state, char character)
{
    if (editor_state->word_builder_len >= WORD_BUILDER_BUF_SIZE - 1) {
        return;
    }
    editor_state->word_builder_buf[editor_state->word_builder_len] = character;
    editor_state->word_builder_len++;
    editor_state->word_builder_buf[editor_state->word_builder_len] = '\0';
}

static void keyboard_backspace(EditorState *editor_state)
{
    if (editor_state->word_builder_len <= 0) {
        return;
    }
    editor_state->word_builder_len--;
    editor_state->word_builder_buf[editor_state->word_builder_len] = '\0';
}

static int keyboard_item_count(const EditorState *editor_state)
{
    if (editor_state->keyboard_group < 0) {
        return KEYBOARD_GROUP_COUNT;
    }
    return keyboard_group_sizes[editor_state->keyboard_group];
}

static const char *keyboard_item_label(const EditorState *editor_state, int index)
{
    if (editor_state->keyboard_group < 0) {
        if (index >= 0 && index < KEYBOARD_GROUP_COUNT) {
            return keyboard_group_labels[index];
        }
        return "";
    }
    int group = editor_state->keyboard_group;
    if (index >= 0 && index < keyboard_group_sizes[group]) {
        return TextFormat("%c", keyboard_groups[group][index]);
    }
    return "";
}

void draw_gamepad_kb(ScreenSize screen, const EditorState *editor_state, Font ui_font)
{
    if (editor_state->sub_mode != EDITOR_SUB_GAMEPAD_KB) {
        return;
    }
    int center_x = screen.width / 2;
    int center_y = screen.height / 2;
    DrawCircle(center_x, center_y, RADIAL_OUTER_RADIUS + RADIAL_BG_PADDING, debug_bg_color);
    int total = keyboard_item_count(editor_state);
    float sector_deg = RADIAL_FULL_CIRCLE_DEG / (float)total;
    for (int index = 0; index < total; index++) {
        float start = ((float)index * sector_deg) - RADIAL_NORTH_OFFSET_DEG;
        float end = start + sector_deg;
        bool selected = (index == editor_state->keyboard_selected);
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
        const char *label = keyboard_item_label(editor_state, index);
        int text_width = measure_ui_text(ui_font, label, RADIAL_FONT_SIZE);
        draw_ui_text(ui_font, label, label_x - (text_width / 2), label_y - (RADIAL_FONT_SIZE / 2), RADIAL_FONT_SIZE,
                     sector_color);
    }
    DrawCircle(center_x, center_y, RADIAL_INNER_RADIUS, debug_bg_color);

    /* Buffer status line above the radial */
    const char *buffer_text = TextFormat("> %s|", editor_state->word_builder_buf);
    int buffer_width = measure_ui_text(ui_font, buffer_text, TOAST_FONT_SIZE);
    int buffer_y = center_y - (int)RADIAL_OUTER_RADIUS - TOAST_FONT_SIZE - (int)RADIAL_BG_PADDING;
    draw_ui_text(ui_font, buffer_text, center_x - (buffer_width / 2), buffer_y, TOAST_FONT_SIZE, WHITE);

    /* Group indicator below the radial (level 2 only) */
    if (editor_state->keyboard_group >= 0) {
        const char *group_text = TextFormat("[ %s ]", keyboard_group_labels[editor_state->keyboard_group]);
        int group_width = measure_ui_text(ui_font, group_text, RADIAL_FONT_SIZE);
        int group_y = center_y + (int)RADIAL_OUTER_RADIUS + (int)RADIAL_BG_PADDING;
        draw_ui_text(ui_font, group_text, center_x - (group_width / 2), group_y, RADIAL_FONT_SIZE, debug_text_color);
    }
}

void handle_gamepad_kb_input(EditorState *editor_state, InputState input)
{
    int total = keyboard_item_count(editor_state);
    editor_state->keyboard_selected = radial_sector_from_stick(input.left_stick, total);
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        if (editor_state->keyboard_selected >= 0) {
            if (editor_state->keyboard_group < 0) {
                editor_state->keyboard_group = editor_state->keyboard_selected;
                editor_state->keyboard_selected = -1;
            } else {
                int group = editor_state->keyboard_group;
                int selected = editor_state->keyboard_selected;
                keyboard_type_char(editor_state, keyboard_groups[group][selected]);
                editor_state->keyboard_group = -1;
                editor_state->keyboard_selected = -1;
            }
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        if (editor_state->keyboard_group >= 0) {
            editor_state->keyboard_group = -1;
        } else if (editor_state->word_builder_len > 0) {
            keyboard_backspace(editor_state);
        } else {
            editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
    }
}

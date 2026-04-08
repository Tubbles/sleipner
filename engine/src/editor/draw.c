#include "editor/internal.h"

#include <string.h>

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static const Color handle_color = {0, 255, 255, 255};            /* cyan: collision handles */
static const int place_preview_alpha = 128;                      /* alpha for texture ghost during placement */
static const Color place_ghost_color = {100, 255, 100, 180};     /* semi-transparent green: place preview */
static const Color attr_override_color = {255, 200, 50, 255};    /* amber: overrides blueprint default */
static const Color attr_custom_color = {100, 220, 100, 255};     /* green: instance-only attribute */
static const Color attr_dimmed_color = {120, 130, 140, 255};     /* gray: overridden by instance */

void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color)
{
    DrawTextEx(font, text, (Vector2){(float)pos_x, (float)pos_y}, (float)font_size, 1, color);
}

int measure_ui_text(Font font, const char *text, int font_size)
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

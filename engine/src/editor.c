#include "engine_context.h"
#include "raylib.h"

#include "attribute.h"
#include "editor.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "rect.h"

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

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

void draw_hints_bar(bool editor_mode, const struct EngineContext *hints_ctx)
{
    int bar_y = hints_ctx->screen_height - HINTS_BAR_HEIGHT;
    DrawRectangle(0, bar_y, hints_ctx->screen_width, HINTS_BAR_HEIGHT, debug_bg_color);
    const char *hints;
    if (editor_mode) {
        hints = "F5: Play  |  F9/Y: Save  |  A/Ent: Sel  |  B/Esc: Desel  |  X/Shift: Watch  |  Stick: Pan";
    } else {
        hints = "F5: Editor  |  F3: Debug  |  F4: Fonts";
    }
    int text_y = bar_y + ((HINTS_BAR_HEIGHT - HINTS_FONT_SIZE) / 2);
    DrawText(hints, DEBUG_MARGIN, text_y, HINTS_FONT_SIZE, debug_text_color);
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

static Rectangle entity_outline_rect(const Entity *entity)
{
    Rectangle col = entity->collision;
    if (col.width > 0.0F && col.height > 0.0F) {
        return col;
    }
    Rectangle src = entity_get_source(entity);
    return (Rectangle){entity->position.x, entity->position.y, src.width, src.height};
}

void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index)
{
    if (hover_entity_index >= 0 && hover_entity_index < state->current_level.entities.count) {
        DrawRectangleLinesEx(entity_outline_rect(&state->current_level.entities.data[hover_entity_index]), 1.0F,
                             YELLOW);
    }
    int sel = editor_state->selected_entity_index;
    if (sel >= 0 && sel < state->current_level.entities.count) {
        DrawRectangleLinesEx(entity_outline_rect(&state->current_level.entities.data[sel]), 2.0F, WHITE);
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

static void draw_attr_section(const AttrSet *set, int panel_x, int *y_offset)
{
    for (int index = 0; index < set->entries.count; index++) {
        const Attribute *attr = &set->entries.data[index];
        DrawText(TextFormat("  %s: %s", attr->name.ptr, attr_display_value(attr)), panel_x + DEBUG_MARGIN, *y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
        *y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_editor_panel(const struct EngineContext *ctx, const GameState *state, const EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->current_level.entities.data[sel];
    int panel_x = ctx->screen_width - EDITOR_PANEL_WIDTH;
    int y_offset = 0;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, ctx->screen_height, debug_bg_color);
    DrawText(TextFormat("[ %s ]  id: %d  parent: %d", entity->blueprint_name.ptr, entity->id, entity->parent_index),
             panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    DrawText(TextFormat("pos: %.1f %.1f", entity->position.x, entity->position.y), panel_x + DEBUG_MARGIN, y_offset,
             EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;
    DrawText("--- instance ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_attr_section(&entity->attrs, panel_x, &y_offset);
    if (entity->defaults) {
        DrawText("--- blueprint ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        draw_attr_section(entity->defaults, panel_x, &y_offset);
    }
}

void draw_watch_overlay(const struct EngineContext *ctx, const GameState *state, const WatchList *watches)
{
    if (watches->count <= 0) {
        return;
    }
    int panel_x = ctx->screen_width - EDITOR_PANEL_WIDTH;
    int panel_height = (watches->count * 2 * EDITOR_PANEL_LINE_HEIGHT) + DEBUG_MARGIN;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, panel_height, debug_bg_color);
    int y_offset = 0;
    for (int index = 0; index < watches->count; index++) {
        int entity_index = watches->entity_indices[index];
        if (entity_index >= state->current_level.entities.count) {
            y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;
            continue;
        }
        const Entity *entity = &state->current_level.entities.data[entity_index];
        DrawText(TextFormat("[%s] pos:%.0f,%.0f", entity->blueprint_name.ptr, entity->position.x, entity->position.y),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        const Attribute *hp_attr = entity_get_attr(entity, "hp");
        const Attribute *hp_max_attr = entity_get_attr(entity, "hp_max");
        if (hp_attr && hp_max_attr) {
            DrawText(TextFormat("  hp: %d/%d", hp_attr->value.i, hp_max_attr->value.i), panel_x + DEBUG_MARGIN,
                     y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        }
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

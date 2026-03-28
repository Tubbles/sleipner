#include "engine_context.h"
#include "raylib.h"

#include "alloc.h"
#include "attribute.h"
#include "blueprint.h"
#include "editor.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "rect.h"

#include <string.h>

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static const Color handle_color = {0, 255, 255, 255}; /* cyan: collision handles */

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

void draw_hints_bar(bool editor_mode, const EditorState *editor_state, const struct EngineContext *hints_ctx)
{
    int bar_y = hints_ctx->screen_height - HINTS_BAR_HEIGHT;
    DrawRectangle(0, bar_y, hints_ctx->screen_width, HINTS_BAR_HEIGHT, debug_bg_color);
    const char *hints;
    if (editor_mode) {
        if (editor_state->sub_mode == EDITOR_SUB_DRAG) {
            hints = "F9/Y: Save  |  A/Ent: Confirm  |  B/Esc: Cancel  |  Stick: Move entity";
        } else if (editor_state->sub_mode == EDITOR_SUB_HANDLES) {
            hints = "F9/Y: Save  |  A/Ent: Confirm  |  B/Esc: Cancel  |  Stick: Move  |  R-Stick: Resize";
        } else {
            hints = "F5: Play  |  F9/Y: Save  |  A/Ent: Sel  |  B/Esc: Desel  |  X/Shift: Watch  |  G/L3: Grab  |  "
                    "H/L1: Handles  |  Stick: Pan";
        }
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

static Blueprint *find_blueprint_by_name(GameState *state, const char *name)
{
    for (int index = 0; index < state->blueprints.entries.count; index++) {
        Blueprint *blueprint = &state->blueprints.entries.data[index];
        const char *blueprint_name = attr_get_string(&blueprint->attrs, "name");
        if (blueprint_name != NULL && strcmp(blueprint_name, name) == 0) {
            return blueprint;
        }
    }
    return NULL;
}

static void propagate_collision_to_entities(GameState *state, const Blueprint *blueprint)
{
    const char *blueprint_name = attr_get_string(&blueprint->attrs, "name");
    for (int index = 0; index < state->current_level.entities.count; index++) {
        Entity *entity = &state->current_level.entities.data[index];
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
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    Rectangle col = state->current_level.entities.data[sel].collision;
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

void handle_browse_input(GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         InputState input,
                         float delta_time)
{
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        editor_state->selected_entity_index = find_nearest_entity(&state->current_level, camera->target);
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->selected_entity_index = -1;
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT_SHIFT, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0) {
            bool found = false;
            for (int index = 0; index < watches->count; index++) {
                if (watches->entity_indices[index] == sel) {
                    watches->entity_indices[index] = watches->entity_indices[watches->count - 1];
                    watches->count--;
                    found = true;
                    break;
                }
            }
            if (!found && watches->count < EDITOR_WATCH_MAX) {
                watches->entity_indices[watches->count] = sel;
                watches->count++;
            }
        }
    }
    update_editor_camera(camera, input, delta_time);
}

void handle_mode_transitions(const GameState *state, EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_BROWSE) {
        return;
    }
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->current_level.entities.data[sel];
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

void handle_drag_input(GameState *state, EditorState *editor_state, InputState input, float delta_time)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    Entity *entity = &state->current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
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
    struct EngineContext *ctx, GameState *state, EditorState *editor_state, InputState input, float delta_time)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    Entity *entity = &state->current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        if (blueprint != NULL) {
            Allocator alloc = allocator_arena(ctx, &state->gamedata_arena);
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

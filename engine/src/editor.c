#include "engine_context.h"
#include "raylib.h"

#include "alloc.h"
#include "arena.h"
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

static const Color handle_color = {0, 255, 255, 255};        /* cyan: collision handles */
static const Color place_ghost_color = {100, 255, 100, 180}; /* semi-transparent green: place preview */

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
        } else if (editor_state->sub_mode == EDITOR_SUB_PLACE) {
            hints = "A/Ent: Spawn  |  B/Esc: Cancel  |  Up/Down: Scroll  |  L1/Q: PgUp  |  R1/E: PgDn  |  Stick: Pan";
        } else if (editor_state->sub_mode == EDITOR_SUB_ATTR_EDIT) {
            hints = "A/Ent: Confirm  |  B/Esc: Cancel  |  Left/Right: ±1  |  [/L1: -10  |  ]/R1: +10";
        } else {
            hints = "F5: Play  |  F9/Y: Save  |  A/Ent: Sel  |  B/Esc: Desel  |  Up/Down: Attr  |  "
                    "Shift/L2: Watch  |  Del/X: Delete  |  G/L3: Grab  |  H/L1: Handles  |  P/R1: Place  |  Stick: Pan";
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

static void draw_attr_section(const AttrSet *set, int panel_x, int *y_offset, int base_index, int selected_attr_index)
{
    for (int index = 0; index < set->entries.count; index++) {
        const Attribute *attr = &set->entries.data[index];
        Color text_color = (base_index + index == selected_attr_index) ? WHITE : debug_text_color;
        DrawText(TextFormat("  %s: %s", attr->name.ptr, attr_display_value(attr)), panel_x + DEBUG_MARGIN, *y_offset,
                 EDITOR_PANEL_FONT_SIZE, text_color);
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
    int sel_attr = editor_state->selected_attr_index;
    draw_attr_section(&entity->attrs, panel_x, &y_offset, 0, sel_attr);
    if (entity->defaults) {
        DrawText("--- blueprint ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        int blueprint_base = entity->attrs.entries.count;
        draw_attr_section(entity->defaults, panel_x, &y_offset, blueprint_base, sel_attr);
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

static int total_attr_count(const Entity *entity)
{
    int instance_count = entity->attrs.entries.count;
    int blueprint_count = entity->defaults ? entity->defaults->entries.count : 0;
    return instance_count + blueprint_count;
}

/* entity must not be NULL; resolves blueprint attrs via find_blueprint_by_name */
static Attribute *attr_at_display_index(GameState *state, Entity *entity, int attr_index)
{
    int instance_count = entity->attrs.entries.count;
    if (attr_index < instance_count) {
        return &entity->attrs.entries.data[attr_index];
    }
    Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
    if (!blueprint) {
        return NULL;
    }
    int blueprint_index = attr_index - instance_count;
    if (blueprint_index >= blueprint->attrs.entries.count) {
        return NULL;
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
    if (toggle_pressed((ToggleBinding){KEY_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT})) {
        delta--;
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT})) {
        delta++;
    }
    if (toggle_pressed((ToggleBinding){KEY_LEFT_BRACKET, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        delta -= EDITOR_ATTR_LARGE_STEP;
    }
    if (toggle_pressed((ToggleBinding){KEY_RIGHT_BRACKET, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        delta += EDITOR_ATTR_LARGE_STEP;
    }
    return delta;
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

static int find_place_blueprint_index(const GameState *state, const EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return 0;
    }
    const char *name = state->current_level.entities.data[sel].blueprint_name.ptr;
    for (int index = 0; index < state->blueprints.entries.count; index++) {
        const char *bp_name = attr_get_string(&state->blueprints.entries.data[index].attrs, "name");
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

void draw_place_panel(const struct EngineContext *ctx, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_PLACE) {
        return;
    }
    int count = state->blueprints.entries.count;
    if (count == 0) {
        return;
    }
    int bp_index = editor_state->place_blueprint_index;
    int panel_x = ctx->screen_width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, ctx->screen_height, debug_bg_color);
    int y_offset = 0;
    DrawText("[ Place Mode ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int visible = place_visible_count(ctx->screen_height);
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
        const char *name = attr_get_string(&state->blueprints.entries.data[index].attrs, "name");
        bool selected = (index == bp_index);
        Color color = selected ? WHITE : debug_text_color;
        DrawText(TextFormat("%s %s", selected ? ">" : " ", name ? name : "?"), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_place_preview(const GameState *state, const EditorState *editor_state, Camera2D camera)
{
    if (editor_state->sub_mode != EDITOR_SUB_PLACE) {
        return;
    }
    if (state->blueprints.entries.count == 0) {
        return;
    }
    const Blueprint *blueprint = &state->blueprints.entries.data[editor_state->place_blueprint_index];
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

static void
delete_selected_entity(struct EngineContext *ctx, GameState *state, EditorState *editor_state, WatchList *watches)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        return;
    }
    if (state->current_level.entities.data[sel].parent_index >= 0) {
        return;
    }
    int count = state->current_level.entities.count;
    SCRATCH_SCOPE(&state->scratch_arena);
    bool *is_deleted = arena_alloc_n(ctx, &state->scratch_arena, (size_t)count * sizeof(bool));
    int *new_index_map = arena_alloc_n(ctx, &state->scratch_arena, (size_t)count * sizeof(int));
    memset(is_deleted, 0, (size_t)count * sizeof(bool));
    is_deleted[sel] = true;
    mark_deleted_descendants(&state->current_level, is_deleted, count);
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
            int parent = state->current_level.entities.data[index].parent_index;
            if (parent >= 0) {
                state->current_level.entities.data[index].parent_index = new_index_map[parent];
            }
        }
    }
    int write = 0;
    for (int index = 0; index < count; index++) {
        if (!is_deleted[index]) {
            state->current_level.entities.data[write++] = state->current_level.entities.data[index];
        }
    }
    state->current_level.entities.count = write;
    if (state->player_index >= 0) {
        state->player_index = new_index_map[state->player_index];
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

static void handle_browse_select(GameState *state, Camera2D *camera, EditorState *editor_state)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0) {
        editor_state->selected_entity_index = find_nearest_entity(&state->current_level, camera->target);
        editor_state->selected_attr_index = -1;
        return;
    }
    Entity *entity = &state->current_level.entities.data[sel];
    int attr_idx = editor_state->selected_attr_index;
    if (attr_idx < 0) {
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
    } else if (attr->type == ATTR_INT) {
        editor_state->saved_attr_int = attr->value.i;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    } else if (attr->type == ATTR_FLOAT) {
        editor_state->saved_attr_float = attr->value.f;
        editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
    }
    /* ATTR_STRING: no-op */
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
    if (sel < 0 || sel >= state->current_level.entities.count) {
        return;
    }
    const Entity *entity = &state->current_level.entities.data[sel];
    int total = total_attr_count(entity);
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

void handle_browse_input(struct EngineContext *ctx,
                         GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         InputState input,
                         float delta_time)
{
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        handle_browse_select(state, camera, editor_state);
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
    if (toggle_pressed((ToggleBinding){KEY_DELETE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT})) {
        delete_selected_entity(ctx, state, editor_state, watches);
    }
    if (toggle_pressed((ToggleBinding){KEY_P, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        if (state->blueprints.entries.count > 0) {
            editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_PLACE;
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

void handle_attr_edit_input(GameState *state, EditorState *editor_state, float delta_time)
{
    (void)delta_time;
    int sel = editor_state->selected_entity_index;
    int attr_idx = editor_state->selected_attr_index;
    if (sel < 0 || attr_idx < 0) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    Entity *entity = &state->current_level.entities.data[sel];
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        if (is_blueprint_attr(entity, attr_idx)) {
            Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
            if (blueprint) {
                propagate_collision_to_entities(state, blueprint);
            }
        }
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
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int delta = read_value_delta();
    if (delta == 0) {
        return;
    }
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

#include "editor/internal.h"

#include "collision.h"
#include "entity.h"
#include "raylib.h"
#include "render.h"

#include <stdio.h>
#include <string.h>

#define HINTS_BAR_BUF_CAP 512
#define HINT_LABEL_BUF_CAP 32
#define HINT_OVERFLOW_TAIL_CAP 16

const Color debug_text_color = {200, 220, 240, 255};
const Color debug_log_color = {180, 210, 180, 255};
const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static const Color handle_color = {0, 255, 255, 255};         /* cyan: collision handles */
static const int place_preview_alpha = 128;                   /* alpha for texture ghost during placement */
static const Color place_ghost_color = {100, 255, 100, 180};  /* semi-transparent green: place preview */
static const Color attr_override_color = {255, 200, 50, 255}; /* amber: overrides blueprint default */
static const Color attr_custom_color = {100, 220, 100, 255};  /* green: instance-only attribute */
static const Color attr_dimmed_color = {120, 130, 140, 255};  /* gray: overridden by instance */
static const Color multiselect_color = {255, 165, 0, 255};    /* orange: multi-selected entity (S5.7) */

void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color)
{
    DrawTextEx(font, text, (Vector2){(float)pos_x, (float)pos_y}, (float)font_size, 1, color);
}

int measure_ui_text(Font font, const char *text, int font_size)
{
    return (int)MeasureTextEx(font, text, (float)font_size, 1).x;
}

void update_editor_camera(Camera2D *camera, const InputState *input, const BindingStore *bindings, float delta_time)
{
    Vector2 pan = input_axis_pair(input, bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    camera->target.x += pan.x * EDITOR_CAMERA_SPEED * delta_time;
    camera->target.y += pan.y * EDITOR_CAMERA_SPEED * delta_time;
}

void draw_editor_crosshair(RectU32 game_bounds)
{
    int center_x = (int)game_bounds.width / 2;
    int center_y = (int)game_bounds.height / 2;
    DrawLine(center_x - EDITOR_CROSSHAIR_HALF, center_y, center_x + EDITOR_CROSSHAIR_HALF, center_y, WHITE);
    DrawLine(center_x, center_y - EDITOR_CROSSHAIR_HALF, center_x, center_y + EDITOR_CROSSHAIR_HALF, WHITE);
}

/* --- HUD hint tables for the top-level game loop ---
 *
 * Place mode and play mode aren't owned by an editor handler TU (their
 * handlers live in main.c), so their hint tables sit alongside the only
 * consumer — the hint bar render below. The submode-owning files own
 * theirs, exposed via the accessors declared in editor/internal.h. */
static const EditorActionHint place_hints[] = {
    {ACTION_CONFIRM, "Place"}, {ACTION_CANCEL, "Cancel"},   {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"}, {ACTION_PAGE_UP, "Page up"}, {ACTION_PAGE_DOWN, "Page down"},
};

static const EditorHintTable place_table = {
    .hints = place_hints,
    .count = (int)(sizeof(place_hints) / sizeof(place_hints[0])),
    .mode_label = "Place entity",
};

static const EditorActionHint play_mode_hints[] = {
    {ACTION_MENU_TOGGLE, "Menu"},
    {ACTION_FONT_PREVIEW_TOGGLE, "Fonts"},
    {ACTION_EDITOR_TOGGLE, "Editor"},
};

static const EditorHintTable play_mode_table = {
    .hints = play_mode_hints,
    .count = (int)(sizeof(play_mode_hints) / sizeof(play_mode_hints[0])),
    .mode_label = "Play",
};

static const EditorHintTable *hints_table_for(bool editor_mode, const EditorState *editor_state)
{
    if (!editor_mode) {
        return &play_mode_table;
    }
    switch (editor_state->sub_mode) {
    case EDITOR_SUB_DRAG:
        return drag_hints_table();
    case EDITOR_SUB_HANDLES:
        return handles_hints_table();
    case EDITOR_SUB_PLACE:
        return &place_table;
    case EDITOR_SUB_ATTR_EDIT:
        return attr_edit_hints_table();
    case EDITOR_SUB_RADIAL:
        return radial_hints_table();
    case EDITOR_SUB_WORD_BUILDER:
        return word_builder_hints_table();
    case EDITOR_SUB_GAMEPAD_KB:
        return gamepad_kb_hints_table();
    case EDITOR_SUB_FUZZY_FINDER:
        return fuzzy_finder_hints_table();
    case EDITOR_SUB_WATCH_LIST:
        return watch_list_hints_table();
    case EDITOR_SUB_TILE_PAINT:
        return tile_paint_hints_table();
    case EDITOR_SUB_TILE_PALETTE:
        return tile_palette_hints_table();
    case EDITOR_SUB_ATLAS_BROWSE:
        return atlas_browse_hints_table();
    case EDITOR_SUB_ATLAS_REGION_EDIT:
        return atlas_region_edit_hints_table();
    case EDITOR_SUB_ANIM_EDIT:
        return editor_state->anim_blueprint_index >= 0 ? anim_edit_hints_table() : anim_blueprint_list_hints_table();
    case EDITOR_SUB_ANIM_FRAMES:
        return anim_frames_hints_table();
    case EDITOR_SUB_RULE_LIST:
        if (editor_state->rule_viewing_subroutines) {
            return rule_subroutine_list_hints_table();
        }
        return editor_state->rule_blueprint_index >= 0 ? rule_list_hints_table() : rule_blueprint_list_hints_table();
    case EDITOR_SUB_RULE_TREE:
        return rule_tree_hints_table();
    case EDITOR_SUB_BROWSE:
        if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
            return editor_state->selected_blueprint_index >= 0 ? blueprint_detail_hints_table()
                                                               : blueprint_list_hints_table();
        }
        if (editor_state->top_mode == EDITOR_TOP_LEVEL) {
            return editor_state->level_detail_open ? level_detail_hints_table() : level_list_hints_table();
        }
        return browse_hints_table();
    }
    return browse_hints_table();
}

static int hint_append_str(char *out, int cap, int len, const char *text)
{
    if (len >= cap - 1) {
        return len;
    }
    int remain = cap - 1 - len;
    int text_len = (int)strlen(text);
    if (text_len > remain) {
        text_len = remain;
    }
    memcpy(out + len, text, (size_t)text_len);
    return len + text_len;
}

int editor_hint_table_render(const BindingStore *store, const EditorHintTable *table, char *out, int cap)
{
    if (cap <= 0) {
        return 0;
    }
    int len = 0;
    if (table->mode_label != nullptr && table->mode_label[0] != '\0') {
        len = hint_append_str(out, cap, len, "[");
        len = hint_append_str(out, cap, len, table->mode_label);
        len = hint_append_str(out, cap, len, "]");
    }
    for (int index = 0; index < table->count; index++) {
        const EditorActionHint *hint = &table->hints[index];
        if (hint->description == nullptr || hint->description[0] == '\0') {
            continue;
        }
        char label_buf[HINT_LABEL_BUF_CAP];
        int label_len = input_func_label(store, hint->action, label_buf, sizeof(label_buf));
        if (label_len == 0) {
            continue; /* action has no alternatives — skip rather than render an empty key */
        }
        int before = len;
        if (len > 0) {
            len = hint_append_str(out, cap, len, "  |  ");
        }
        len = hint_append_str(out, cap, len, label_buf);
        len = hint_append_str(out, cap, len, ": ");
        len = hint_append_str(out, cap, len, hint->description);
        if (len >= cap - 1) {
            len = before;
            int remaining = table->count - index;
            char tail[HINT_OVERFLOW_TAIL_CAP];
            (void)snprintf(tail, sizeof(tail), "  +%d more", remaining);
            len = hint_append_str(out, cap, len, tail);
            break;
        }
    }
    out[len] = '\0';
    return len;
}

void draw_hints_bar(bool editor_mode,
                    const EditorState *editor_state,
                    const BindingStore *bindings,
                    bool is_dirty,
                    ScreenSize screen,
                    Font ui_font)
{
    int bar_y = screen.height - HINTS_BAR_HEIGHT;
    DrawRectangle(0, bar_y, screen.width, HINTS_BAR_HEIGHT, debug_bg_color);
    const EditorHintTable *table = hints_table_for(editor_mode, editor_state);
    char hints[HINTS_BAR_BUF_CAP];
    (void)editor_hint_table_render(bindings, table, hints, (int)sizeof(hints));
    int text_y = bar_y + ((HINTS_BAR_HEIGHT - HINTS_FONT_SIZE) / 2);
    draw_ui_text(ui_font, hints, DEBUG_MARGIN, text_y, HINTS_FONT_SIZE, debug_text_color);
    int right_edge = screen.width - DEBUG_MARGIN;
    if (editor_mode && is_dirty) {
        const char *dirty_label = "[*]";
        int dirty_width = measure_ui_text(ui_font, dirty_label, HINTS_FONT_SIZE);
        draw_ui_text(ui_font, dirty_label, right_edge - dirty_width, text_y, HINTS_FONT_SIZE, WHITE);
        right_edge -= dirty_width + DEBUG_MARGIN;
    }
    /* Grid-snap indicator (S5.7, D38): persistent, unlike a toast, so the
     * toggle's current state stays visible for as long as it's on. Sits to
     * the left of the dirty marker's slot, whether or not that marker is
     * currently shown, so its position doesn't jump around. */
    if (editor_mode && editor_state->grid_snap) {
        const char *grid_label = "[GRID]";
        int grid_width = measure_ui_text(ui_font, grid_label, HINTS_FONT_SIZE);
        draw_ui_text(ui_font, grid_label, right_edge - grid_width, text_y, HINTS_FONT_SIZE, attr_custom_color);
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
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    Rectangle col = entity_collision_rect(entity, defaults);
    if (col.width > 0.0F && col.height > 0.0F) {
        return col;
    }
    Rectangle src = get_source_rect(&entity->attrs, defaults);
    return (Rectangle){entity->position.x, entity->position.y, src.width, src.height};
}

void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index)
{
    if (hover_entity_index >= 0 && hover_entity_index < state->gamedata.current_level.entities.count) {
        DrawRectangleLinesEx(
            entity_outline_rect(state, &state->gamedata.current_level.entities.data[hover_entity_index]), 1.0F, YELLOW);
    }
    /* Multi-selection first, in orange, so the anchor's white outline
     * (below) draws on top and stays visually distinct even though the
     * anchor is always itself a member of the multi-selection. */
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        int multiselect_index =
            level_find_entity_by_id(&state->gamedata.current_level, editor_state->multiselect_ids[index]);
        if (multiselect_index >= 0) {
            DrawRectangleLinesEx(
                entity_outline_rect(state, &state->gamedata.current_level.entities.data[multiselect_index]), 2.0F,
                multiselect_color);
        }
    }
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel >= 0) {
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

/* Tri-section renderer for the scene attr panel. Each section walks its AttrSet and colors
 * each row based on whether its name also exists in either of the two "other" sections. */
static void draw_scene_attr_section(Font font,
                                    const AttrSet *set,
                                    AttrSection section,
                                    const AttrSet *other_a,
                                    const AttrSet *other_b,
                                    int panel_x,
                                    int *y_offset,
                                    int base_index,
                                    int selected_attr_index)
{
    for (int index = 0; index < set->entries.count; index++) {
        const Attribute *attr = &set->entries.data[index];
        bool selected = (base_index + index == selected_attr_index);
        bool exists_in_other =
            (other_a && attr_get(other_a, attr->name.ptr)) || (other_b && attr_get(other_b, attr->name.ptr));
        const char *prefix = "  ";
        Color base_color = debug_text_color;
        if (section == ATTR_SECTION_BLUEPRINT) {
            if (exists_in_other) {
                base_color = attr_dimmed_color;
            }
        } else {
            prefix = exists_in_other ? "> " : "+ ";
            base_color = exists_in_other ? attr_override_color : attr_custom_color;
        }
        Color text_color = selected ? WHITE : base_color;
        draw_ui_text(font, TextFormat("%s%s: %s", prefix, attr->name.ptr, attr_display_value(attr)),
                     panel_x + DEBUG_MARGIN, *y_offset, EDITOR_PANEL_FONT_SIZE, text_color);
        *y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void draw_add_sentinel(Font font, int panel_x, int *y_offset, bool selected)
{
    Color color = selected ? WHITE : attr_custom_color;
    draw_ui_text(font, "  [ + ADD ]", panel_x + DEBUG_MARGIN, *y_offset, EDITOR_PANEL_FONT_SIZE, color);
    *y_offset += EDITOR_PANEL_LINE_HEIGHT;
}

static void draw_section_header(Font font, const char *label, int panel_x, int *y_offset)
{
    draw_ui_text(font, label, panel_x + DEBUG_MARGIN, *y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    *y_offset += EDITOR_PANEL_LINE_HEIGHT;
}

void draw_editor_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
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

    int sel_attr = editor_resolve_selected_attr_index(state, entity, editor_state);
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    int base_idx = 0;

    if (entity_has_persisted_section(entity)) {
        draw_section_header(font, "--- persisted ---", panel_x, &y_offset);
        draw_scene_attr_section(font, &entity->persisted_attrs, ATTR_SECTION_PERSISTED, &entity->attrs, defaults,
                                panel_x, &y_offset, base_idx, sel_attr);
        base_idx += entity->persisted_attrs.entries.count;
        draw_add_sentinel(font, panel_x, &y_offset, sel_attr == base_idx);
        base_idx += 1;
    }

    draw_section_header(font, "--- runtime ---", panel_x, &y_offset);
    const AttrSet *persisted_other = entity_has_persisted_section(entity) ? &entity->persisted_attrs : nullptr;
    draw_scene_attr_section(font, &entity->attrs, ATTR_SECTION_RUNTIME, persisted_other, defaults, panel_x, &y_offset,
                            base_idx, sel_attr);
    base_idx += entity->attrs.entries.count;
    draw_add_sentinel(font, panel_x, &y_offset, sel_attr == base_idx);
    base_idx += 1;

    draw_section_header(font, "--- blueprint ---", panel_x, &y_offset);
    if (defaults) {
        draw_scene_attr_section(font, defaults, ATTR_SECTION_BLUEPRINT, persisted_other, &entity->attrs, panel_x,
                                &y_offset, base_idx, sel_attr);
        base_idx += defaults->entries.count;
    }
    draw_add_sentinel(font, panel_x, &y_offset, sel_attr == base_idx);
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
        int entity_index = level_find_entity_by_id(&state->gamedata.current_level, watches->watch_ids[index]);
        if (entity_index < 0) {
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

void draw_watch_list_panel(ScreenSize screen,
                           const GameState *state,
                           const EditorState *editor_state,
                           const WatchList *watches)
{
    if (editor_state->sub_mode != EDITOR_SUB_WATCH_LIST) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, TextFormat("[ Watch List ] (%d)", watches->count), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    for (int index = 0; index < watches->count; index++) {
        bool selected = (index == editor_state->watch_list_scroll);
        Color color = selected ? WHITE : debug_text_color;
        int entity_index = level_find_entity_by_id(&state->gamedata.current_level, watches->watch_ids[index]);
        const char *label = "(gone)";
        if (entity_index >= 0) {
            const Entity *entity = &state->gamedata.current_level.entities.data[entity_index];
            label = TextFormat("[%s]  id:%d", entity->blueprint_name.ptr, entity->id);
        }
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", label), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_collision_handles(const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_HANDLES) {
        return;
    }
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return;
    }
    const Entity *handle_entity = &state->gamedata.current_level.entities.data[sel];
    const AttrSet *handle_defaults = entity_resolve_defaults(state, handle_entity->id);

    /* All primitives of both regions (D28), same green/yellow scheme as the
     * debug overlay (draw_debug_collision_boxes in main.c). trigger_region
     * only draws when an authored composite trigger shape is present — see
     * that function's comment for why the runtime fallback isn't drawn. */
    CollisionPrimitive prim_storage;
    CollisionShape collision_shape = entity_collision_region(handle_entity, handle_defaults, &prim_storage);
    render_collision_shape_outline(collision_shape, handle_entity->position, GREEN);
    if (handle_entity->trigger_region.prims.count > 0) {
        render_collision_shape_outline(handle_entity->trigger_region, handle_entity->position, YELLOW);
    }

    Rectangle col = entity_collision_rect(handle_entity, handle_defaults);
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

void draw_blueprint_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int count = state->gamedata.blueprints.entries.count;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Blueprint Mode ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    int total = count + 1; /* +1 for "+NEW" sentinel */
    int visible = place_visible_count(screen.height);
    int scroll = editor_state->blueprint_list_scroll - (visible / 2);
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
        bool selected = (index == editor_state->blueprint_list_scroll);
        Color color = selected ? WHITE : debug_text_color;
        if (index == count) {
            draw_ui_text(font, TextFormat("%s + NEW BLUEPRINT", selected ? ">" : " "), panel_x + DEBUG_MARGIN, y_offset,
                         EDITOR_PANEL_FONT_SIZE, color);
        } else {
            const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[index];
            const char *name = attr_get_string(&blueprint->attrs, "name");
            draw_ui_text(font,
                         TextFormat("%s %s  (%da %dc %dr)", selected ? ">" : " ", name ? name : "?",
                                    blueprint->attrs.entries.count, blueprint->children.count, blueprint->rules.count),
                         panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        }
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_blueprint_detail_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int bp_idx = editor_state->selected_blueprint_index;
    if (bp_idx < 0 || bp_idx >= state->gamedata.blueprints.entries.count) {
        return;
    }
    const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
    const char *name = attr_get_string(&blueprint->attrs, "name");
    Font font = state->assets.ui_font;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ %s ]", name ? name : "?"), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;

    /* Tree section: children + ADD CHILD */
    int tree_idx = editor_state->blueprint_tree_index;
    int tree_row = 0;
    draw_ui_text(font, "--- children ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    for (int child_idx = 0; child_idx < blueprint->children.count; child_idx++) {
        const BlueprintChild *child = &blueprint->children.data[child_idx];
        Color child_color = (tree_idx == tree_row) ? WHITE : debug_text_color;
        const char *tag_str = (child->tag.len > 0) ? child->tag.ptr : "";
        draw_ui_text(font,
                     TextFormat("  [%s]  \"%s\"  (%.0f, %.0f)", child->blueprint_name.ptr, tag_str, child->offset.x,
                                child->offset.y),
                     panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, child_color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
        tree_row++;
    }
    Color add_child_color = (tree_idx == tree_row) ? WHITE : attr_custom_color;
    draw_ui_text(font, "  [ + ADD CHILD ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, add_child_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    /* Attr section */
    draw_ui_text(font, "--- attributes ---", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    int sel_attr = editor_state->blueprint_attr_index;
    draw_attr_section(font, &blueprint->attrs, true, nullptr, panel_x, &y_offset, 0, sel_attr);
    Color sentinel_color = (sel_attr == blueprint->attrs.entries.count) ? WHITE : attr_custom_color;
    draw_ui_text(font, "  [ + ADD ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, sentinel_color);
}

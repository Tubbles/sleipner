#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "map.h"
#include "rule.h"
#include "strv.h"

#include <string.h>

/* --- Shared helpers (declared in internal.h) --- */

Blueprint *find_blueprint_by_name(GameState *state, const char *name)
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

bool entity_has_persisted_section(const Entity *entity)
{
    return entity->parent_index < 0;
}

/* Row layout: each section is [attrs...][ADD sentinel]. Children skip persisted. */
int total_attr_count(const GameState *state, const Entity *entity)
{
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    int total = 0;
    if (entity_has_persisted_section(entity)) {
        total += entity->persisted_attrs.entries.count + 1;
    }
    total += entity->attrs.entries.count + 1;
    total += (defaults ? defaults->entries.count : 0) + 1;
    return total;
}

AttrRow attr_row_at(const GameState *state, const Entity *entity, int attr_index)
{
    AttrRow row = {.kind = ATTR_ROW_KIND_INVALID, .section = ATTR_SECTION_RUNTIME, .index_in_section = -1};
    if (attr_index < 0) {
        return row;
    }
    int cursor = attr_index;

    if (entity_has_persisted_section(entity)) {
        int persisted_count = entity->persisted_attrs.entries.count;
        if (cursor < persisted_count) {
            row.kind = ATTR_ROW_KIND_ATTR;
            row.section = ATTR_SECTION_PERSISTED;
            row.index_in_section = cursor;
            return row;
        }
        if (cursor == persisted_count) {
            row.kind = ATTR_ROW_KIND_ADD;
            row.section = ATTR_SECTION_PERSISTED;
            return row;
        }
        cursor -= persisted_count + 1;
    }

    int runtime_count = entity->attrs.entries.count;
    if (cursor < runtime_count) {
        row.kind = ATTR_ROW_KIND_ATTR;
        row.section = ATTR_SECTION_RUNTIME;
        row.index_in_section = cursor;
        return row;
    }
    if (cursor == runtime_count) {
        row.kind = ATTR_ROW_KIND_ADD;
        row.section = ATTR_SECTION_RUNTIME;
        return row;
    }
    cursor -= runtime_count + 1;

    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    int bp_count = defaults ? defaults->entries.count : 0;
    if (cursor < bp_count) {
        row.kind = ATTR_ROW_KIND_ATTR;
        row.section = ATTR_SECTION_BLUEPRINT;
        row.index_in_section = cursor;
        return row;
    }
    if (cursor == bp_count) {
        row.kind = ATTR_ROW_KIND_ADD;
        row.section = ATTR_SECTION_BLUEPRINT;
        return row;
    }
    return row;
}

AttrSet *attr_section_set(GameState *state, Entity *entity, AttrSection section)
{
    switch (section) {
    case ATTR_SECTION_PERSISTED:
        return &entity->persisted_attrs;
    case ATTR_SECTION_RUNTIME:
        return &entity->attrs;
    case ATTR_SECTION_BLUEPRINT: {
        Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        return blueprint ? &blueprint->attrs : nullptr;
    }
    }
    return nullptr;
}

Attribute *attr_at_display_index(GameState *state, Entity *entity, int attr_index)
{
    AttrRow row = attr_row_at(state, entity, attr_index);
    if (row.kind != ATTR_ROW_KIND_ATTR) {
        return nullptr;
    }
    AttrSet *target = attr_section_set(state, entity, row.section);
    if (!target || row.index_in_section >= target->entries.count) {
        return nullptr;
    }
    return &target->entries.data[row.index_in_section];
}

bool is_blueprint_attr(const GameState *state, const Entity *entity, int attr_index)
{
    AttrRow row = attr_row_at(state, entity, attr_index);
    return row.kind == ATTR_ROW_KIND_ATTR && row.section == ATTR_SECTION_BLUEPRINT;
}

int tree_section_total(const Entity *entity, const Blueprint *blueprint)
{
    int total = (entity->parent_index >= 0) ? 1 : 0;
    total += blueprint ? blueprint->children.count : 0;
    total += 1; /* ADD CHILD sentinel */
    return total;
}

bool tree_is_parent_row(const Entity *entity, int tree_index)
{
    return entity->parent_index >= 0 && tree_index == 0;
}

bool tree_is_add_child_row(const Entity *entity, const Blueprint *blueprint, int tree_index)
{
    int parent_rows = (entity->parent_index >= 0) ? 1 : 0;
    int child_rows = blueprint ? blueprint->children.count : 0;
    return tree_index == parent_rows + child_rows;
}

int tree_child_index(const Entity *entity, int tree_index)
{
    int parent_rows = (entity->parent_index >= 0) ? 1 : 0;
    int child_idx = tree_index - parent_rows;
    if (child_idx < 0) {
        return -1;
    }
    return child_idx;
}

int place_visible_count(int screen_height)
{
    return (screen_height - HINTS_BAR_HEIGHT - EDITOR_PANEL_LINE_HEIGHT) / EDITOR_PANEL_LINE_HEIGHT;
}

int find_place_blueprint_index(const GameState *state, const EditorState *editor_state)
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

void mark_deleted_descendants(const Level *level, bool *is_deleted, int count)
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

/* --- File-local helpers --- */

static void delete_selected_entity(GameState *state, EditorState *editor_state, WatchList *watches)
{
    int sel = editor_state->selected_entity_index;
    if (sel < 0 || sel >= state->gamedata.current_level.entities.count) {
        return;
    }
    Level *level = &state->gamedata.current_level;
    int count = level->entities.count;
    SCRATCH_SCOPE(&state->scratch_arena);
    bool *is_deleted = arena_alloc(&state->scratch_arena, (size_t)count * sizeof(bool));
    memset(is_deleted, 0, (size_t)count * sizeof(bool));
    is_deleted[sel] = true;
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
    /* Fix watches */
    for (int index = 0; index < watches->count; index++) {
        int old = watches->entity_indices[index];
        if (old >= count || is_deleted[old]) {
            watches->entity_indices[index] = watches->entity_indices[watches->count - 1];
            watches->count--;
            index--;
        } else {
            watches->entity_indices[index] = new_index_map[old];
        }
    }
    editor_state->selected_entity_index = -1;
    editor_state->selected_attr_index = -1;
    editor_state->selected_tree_index = -1;
}

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
    AttrRow row = attr_row_at(state, entity, attr_idx);
    if (row.kind == ATTR_ROW_KIND_ADD) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        if (row.section == ATTR_SECTION_PERSISTED) {
            editor_state->adding_persisted_attr = true;
        } else if (row.section == ATTR_SECTION_RUNTIME) {
            editor_state->adding_attr = true;
        } else {
            editor_state->adding_blueprint_attr = true;
        }
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    Attribute *attr = attr_at_display_index(state, entity, attr_idx);
    if (!attr) {
        return;
    }
    if (attr->type == ATTR_BOOL) {
        attr->value.b = !attr->value.b;
        if (is_blueprint_attr(state, entity, attr_idx)) {
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
        editor_state->selected_attr_index = -1;
        editor_state->selected_tree_index = -1;
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
    int attr_total = total_attr_count(state, entity);
    int attr_last = attr_total - 1; /* last valid attr row (the final ADD sentinel) */

    if (editor_state->selected_tree_index >= 0) {
        int new_tree = editor_state->selected_tree_index + direction;
        if (new_tree < 0) {
            editor_state->selected_tree_index = -1;
        } else if (new_tree >= tree_total) {
            editor_state->selected_tree_index = -1;
            editor_state->selected_attr_index = 0;
        } else {
            editor_state->selected_tree_index = new_tree;
        }
    } else if (editor_state->selected_attr_index >= 0) {
        int new_attr = editor_state->selected_attr_index + direction;
        if (new_attr < 0) {
            editor_state->selected_attr_index = -1;
            editor_state->selected_tree_index = tree_total - 1;
        } else if (new_attr > attr_last) {
            editor_state->selected_attr_index = attr_last;
        } else {
            editor_state->selected_attr_index = new_attr;
        }
    } else {
        if (direction > 0) {
            editor_state->selected_tree_index = 0;
        } else {
            editor_state->selected_attr_index = attr_last;
        }
    }
}

static void
handle_browse_delete(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int del_sel = editor_state->selected_entity_index;
    if (del_sel < 0) {
        return;
    }

    /* Tree section: X on child row -> remove blueprint child */
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

    /* Attr section: X on persisted/runtime attr -> remove from that set;
     * X on blueprint attr (or no attr selected) -> delete entity. */
    Entity *del_entity = &state->gamedata.current_level.entities.data[del_sel];
    int del_attr = editor_state->selected_attr_index;
    AttrRow del_row = attr_row_at(state, del_entity, del_attr);
    if (del_row.kind == ATTR_ROW_KIND_ATTR &&
        (del_row.section == ATTR_SECTION_PERSISTED || del_row.section == ATTR_SECTION_RUNTIME)) {
        AttrSet *target = attr_section_set(state, del_entity, del_row.section);
        Attribute *del_target = &target->entries.data[del_row.index_in_section];
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        attr_remove(&alloc, target, del_target->name.ptr);
        editor_state->selected_attr_index = -1;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Remove attribute"));
    } else if (del_attr < 0 || (del_row.kind == ATTR_ROW_KIND_ATTR && del_row.section == ATTR_SECTION_BLUEPRINT)) {
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
        } else if (confirmed == 4) { /* Blueprints */
            editor_state->top_mode = EDITOR_TOP_BLUEPRINT;
            editor_state->selected_blueprint_index = -1;
            editor_state->blueprint_list_scroll = 0;
            editor_state->blueprint_attr_index = -1;
            editor_state->blueprint_tree_index = -1;
            editor_state->selected_entity_index = -1;
        }
    } else if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
        dispatch_attr_type_change(state, editor_state, confirmed, undo_history);
    } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
        dispatch_child_props(state, editor_state, confirmed);
    }
}

/* --- Public functions --- */

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
        editor_state->radial_item_count = EDITOR_TOOLS_ITEM_COUNT;
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

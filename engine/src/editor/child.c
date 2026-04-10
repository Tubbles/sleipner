#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "debug.h"
#include "error.h"
#include "map.h"
#include "rule.h"
#include "str.h"
#include "strv.h"

#include <string.h>

int find_child_entity(const Level *level, int parent_index, const char *blueprint_name, const char *tag)
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

void propagate_child_tag(GameState *state, const Blueprint *blueprint, int child_idx, const char *old_tag)
{
    const BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *parent = &state->gamedata.current_level.entities.data[index];
        if (strcmp(parent->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        int child_entity = find_child_entity(&state->gamedata.current_level, index, child->blueprint_name.ptr, old_tag);
        if (child_entity >= 0) {
            Allocator alloc = allocator_arena(&state->gamedata_arena);
            state->gamedata.current_level.entities.data[child_entity].tag = str_new(alloc);
            (void)str_from_cstr(&state->gamedata.current_level.entities.data[child_entity].tag, child->tag.ptr);
        }
    }
}

void propagate_child_offset(GameState *state, const Blueprint *blueprint, int child_idx)
{
    const BlueprintChild *child = &blueprint->children.data[child_idx];
    const char *bp_name = attr_get_string(&blueprint->attrs, "name");
    const char *tag = child->tag.len > 0 ? child->tag.ptr : "";
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *parent = &state->gamedata.current_level.entities.data[index];
        if (strcmp(parent->blueprint_name.ptr, bp_name) != 0) {
            continue;
        }
        int child_entity = find_child_entity(&state->gamedata.current_level, index, child->blueprint_name.ptr, tag);
        if (child_entity >= 0) {
            Entity *child_ent = &state->gamedata.current_level.entities.data[child_entity];
            child_ent->position.x = parent->position.x + child->offset.x;
            child_ent->position.y = parent->position.y + child->offset.y;
            entity_update_collision(child_ent);
        }
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

void remove_blueprint_child(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int child_idx)
{
    Blueprint *blueprint = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        if (bp_idx >= 0 && bp_idx < state->gamedata.blueprints.entries.count) {
            blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        }
    } else {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
            Entity *entity = &state->gamedata.current_level.entities.data[sel];
            blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        }
    }
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

void add_blueprint_child(Diag *diag,
                         GameState *state,
                         EditorState *editor_state,
                         UndoHistory *undo_history,
                         const char *child_blueprint_name,
                         TextureLookupFn texture_lookup,
                         void *texture_user_data)
{
    Blueprint *blueprint = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        if (bp_idx >= 0 && bp_idx < state->gamedata.blueprints.entries.count) {
            blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        }
    } else {
        int sel = editor_state->selected_entity_index;
        if (sel >= 0 && sel < state->gamedata.current_level.entities.count) {
            Entity *entity = &state->gamedata.current_level.entities.data[sel];
            blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        }
    }
    if (!blueprint) {
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    BlueprintChild new_child = {0};
    new_child.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&new_child.blueprint_name, child_blueprint_name);
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

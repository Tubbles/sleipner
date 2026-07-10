#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "debug.h"
#include "error.h"
#include "game.h" // game_delete_entity_cascade (S8.7f3a extraction)
#include "net_session.h"
#include "network.h" // network_lock_find (S8.7f3a delete gate)
#include "strv.h"

#include <math.h>
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
    (void)entity;
    return true;
}

/* Row layout: each section is [attrs...][ADD sentinel]. Every entity (root or
 * child) has a persisted section, since S3.3a gave child persisted attrs a
 * TOML round-trip via [level.entity.children.<tag>]. */
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

/* Const, non-blueprint-lookup counterpart of attr_section_set. Blueprint
 * defaults are read via entity_resolve_defaults (keyed by entity id) rather
 * than find_blueprint_by_name, so this stays const-correct end to end —
 * needed by editor_resolve_selected_attr_index below, which only reads. */
static const AttrSet *attr_set_for_section_const(const GameState *state, const Entity *entity, AttrSection section)
{
    switch (section) {
    case ATTR_SECTION_PERSISTED:
        return &entity->persisted_attrs;
    case ATTR_SECTION_RUNTIME:
        return &entity->attrs;
    case ATTR_SECTION_BLUEPRINT:
        return entity_resolve_defaults(state, entity->id);
    }
    return nullptr;
}

static bool attr_row_name_matches(const AttrRow *row, const AttrSet *set, const char *name)
{
    if (!set || row->index_in_section >= set->entries.count) {
        return false;
    }
    const Str *row_name = &set->entries.data[row->index_in_section].name;
    return row_name->len == strlen(name) && strncmp(row_name->ptr, name, row_name->len) == 0;
}

int editor_resolve_selected_attr_index(const GameState *state, const Entity *entity, const EditorState *editor_state)
{
    if (editor_state->selected_attr_kind == EDITOR_ATTR_SEL_NONE) {
        return -1;
    }
    int total = total_attr_count(state, entity);
    for (int index = 0; index < total; index++) {
        AttrRow row = attr_row_at(state, entity, index);
        if (row.section != editor_state->selected_attr_section) {
            continue;
        }
        if (editor_state->selected_attr_kind == EDITOR_ATTR_SEL_ADD) {
            if (row.kind == ATTR_ROW_KIND_ADD) {
                return index;
            }
            continue;
        }
        if (row.kind != ATTR_ROW_KIND_ATTR) {
            continue;
        }
        const AttrSet *set = attr_set_for_section_const(state, entity, row.section);
        if (attr_row_name_matches(&row, set, editor_state->selected_attr_name)) {
            return index;
        }
    }
    return -1;
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
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
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

float editor_snap_to_grid(float value)
{
    return roundf(value / (float)TILE_SIZE) * (float)TILE_SIZE;
}

Vector2 editor_snap_position_to_grid(Vector2 position)
{
    return (Vector2){editor_snap_to_grid(position.x), editor_snap_to_grid(position.y)};
}

/* --- File-local helpers --- */

/* Replace the multi-selection with a single entry, or clear it if
 * entity_id < 0. The single-select path (handle_browse_select's fresh pick,
 * select_entity_and_pan's tree navigation) always reseeds through here so
 * DRAG/COPY only ever need to walk multiselect_ids -- no separate
 * single-entity fallback. */
static void multiselect_reset_to(EditorState *editor_state, int entity_id)
{
    if (entity_id < 0) {
        editor_state->multiselect_count = 0;
        return;
    }
    editor_state->multiselect_ids[0] = entity_id;
    editor_state->multiselect_count = 1;
}

/* Append entity_id to the multi-selection if not already present. Silently
 * no-ops at EDITOR_MULTISELECT_MAX, mirroring toggle_watch's own silent cap
 * below -- an editor-session interaction limit, not an error. */
static void multiselect_add(EditorState *editor_state, int entity_id)
{
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        if (editor_state->multiselect_ids[index] == entity_id) {
            return;
        }
    }
    if (editor_state->multiselect_count >= EDITOR_MULTISELECT_MAX) {
        return;
    }
    editor_state->multiselect_ids[editor_state->multiselect_count++] = entity_id;
}

/* Capture the current position of every multiselect_ids entry into
 * saved_group_positions, in lockstep. Called at every Grab entry point (Tools
 * radial and the mode_transitions shortcut) so DRAG's CANCEL can restore the
 * whole group, not just the anchor entity. */
static void save_group_positions_for_drag(const GameState *state, EditorState *editor_state)
{
    const Level *level = &state->gamedata.current_level;
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        int entity_index = level_find_entity_by_id(level, editor_state->multiselect_ids[index]);
        editor_state->saved_group_positions[index] =
            (entity_index >= 0) ? level->entities.data[entity_index].position : (Vector2){0.0F, 0.0F};
    }
}

static void delete_selected_entity(GameState *state, EditorState *editor_state, WatchList *watches)
{
    Level *level = &state->gamedata.current_level;
    int sel = level_find_entity_by_id(level, editor_state->selected_entity_id);
    if (sel < 0) {
        return;
    }
    /* The level/gamedata mutation core (descendant cascade, map removal,
     * parent-index remap, compaction) lives in game.c (S8.7f3a) so the
     * network DELETE appliers share the exact editor semantics. */
    game_delete_entity_cascade(state, editor_state->selected_entity_id);
    /* Watches are keyed by stable id, so a deleted entity's watch simply
     * stops resolving — prune it and keep the rest in order. Selection
     * (selected_entity_id / selected_attr_*) is left alone: the entity
     * just deleted was the selection, so it naturally resolves to "none"
     * next time it's looked up via level_find_entity_by_id. */
    int watch_write = 0;
    for (int index = 0; index < watches->count; index++) {
        if (level_find_entity_by_id(level, watches->watch_ids[index]) >= 0) {
            watches->watch_ids[watch_write++] = watches->watch_ids[index];
        }
    }
    watches->count = watch_write;
    editor_state->selected_tree_index = -1;
}

/* S8.7f3a: shared Delete entry for both delete-the-selection sites (the
 * Tools radial's Delete item and handle_browse_delete's entity branch).
 * Under NET_CLIENT a selection locked by another player (per the local
 * replica) is refused up front -- toast, nothing deleted, returns false --
 * the same fast local gate try_enter_drag_mode applies to grabs. Otherwise
 * the local delete runs exactly as before (a client's copy is its preview;
 * the host's echo is what makes the delete authoritative on every replica)
 * and, when the selection actually resolved to an entity, the DELETE is
 * committed over the network (network_editor_commit_delete -- a no-op
 * offline). Returns true so each caller pushes its undo entry exactly as it
 * always has. */
static bool delete_selection_networked(GameState *state, EditorState *editor_state, WatchList *watches)
{
    int selected_entity_id = editor_state->selected_entity_id;
    if (state->network.mode == NET_CLIENT) {
        const EntityLock *lock = network_lock_find(&state->network, selected_entity_id);
        if (lock != nullptr && lock->holder_player_id != state->network.local_player_id) {
            editor_state->toast_text = strv_from_cstr("Locked by another player");
            editor_state->toast_timer = TOAST_DURATION;
            return false;
        }
    }
    bool selection_resolved = level_find_entity_by_id(&state->gamedata.current_level, selected_entity_id) >= 0;
    delete_selected_entity(state, editor_state, watches);
    if (selection_resolved) {
        network_editor_commit_delete(state, selected_entity_id);
    }
    return true;
}

static void select_entity_and_pan(EditorState *editor_state, Camera2D *camera, const Level *level, int entity_index)
{
    editor_state->selected_entity_id = level->entities.data[entity_index].id;
    multiselect_reset_to(editor_state, editor_state->selected_entity_id);
    editor_state->selected_tree_index = -1;
    editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
    camera->target = level->entities.data[entity_index].position;
}

static void handle_tree_select(GameState *state, Camera2D *camera, EditorState *editor_state)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
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
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        int nearest = find_nearest_entity(&state->gamedata.current_level, camera->target);
        editor_state->selected_entity_id =
            (nearest >= 0) ? state->gamedata.current_level.entities.data[nearest].id : -1;
        multiselect_reset_to(editor_state, editor_state->selected_entity_id);
        editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
        editor_state->selected_tree_index = -1;
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];

    if (editor_state->selected_tree_index >= 0) {
        handle_tree_select(state, camera, editor_state);
        return;
    }

    int attr_idx = editor_resolve_selected_attr_index(state, entity, editor_state);
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
    if (editor_state->selected_attr_kind != EDITOR_ATTR_SEL_NONE) {
        editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
    } else if (editor_state->selected_tree_index >= 0) {
        editor_state->selected_tree_index = -1;
    } else {
        editor_state->selected_entity_id = -1;
        editor_state->multiselect_count = 0;
        editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
        editor_state->selected_tree_index = -1;
    }
}

/* Write side of the stable attr identity: point selected_attr_* at whatever
 * row_at resolves `display_index` to for `entity`. Pairs with
 * editor_resolve_selected_attr_index (the read side, declared in
 * internal.h since it's called from other editor split files too). This
 * writer is only ever called from handle_browse_navigate below, so it
 * stays file-local. */
static void
editor_set_selected_attr(EditorState *editor_state, const GameState *state, const Entity *entity, int display_index)
{
    AttrRow row = attr_row_at(state, entity, display_index);
    if (row.kind == ATTR_ROW_KIND_ADD) {
        editor_state->selected_attr_kind = EDITOR_ATTR_SEL_ADD;
        editor_state->selected_attr_section = row.section;
        editor_state->selected_attr_name[0] = '\0';
        return;
    }
    if (row.kind == ATTR_ROW_KIND_ATTR) {
        const AttrSet *set = attr_set_for_section_const(state, entity, row.section);
        if (set && row.index_in_section < set->entries.count) {
            const Str *name = &set->entries.data[row.index_in_section].name;
            size_t copy_len = name->len < EDITOR_ATTR_NAME_MAX - 1 ? name->len : EDITOR_ATTR_NAME_MAX - 1;
            memcpy(editor_state->selected_attr_name, name->ptr, copy_len);
            editor_state->selected_attr_name[copy_len] = '\0';
            editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NAMED;
            editor_state->selected_attr_section = row.section;
            return;
        }
    }
    editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
}

static void handle_browse_navigate(const GameState *state, EditorState *editor_state, int direction)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
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
            editor_set_selected_attr(editor_state, state, entity, 0);
        } else {
            editor_state->selected_tree_index = new_tree;
        }
    } else if (editor_state->selected_attr_kind != EDITOR_ATTR_SEL_NONE) {
        int current_attr = editor_resolve_selected_attr_index(state, entity, editor_state);
        int new_attr = current_attr + direction;
        if (new_attr < 0) {
            editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
            editor_state->selected_tree_index = tree_total - 1;
        } else if (new_attr > attr_last) {
            editor_set_selected_attr(editor_state, state, entity, attr_last);
        } else {
            editor_set_selected_attr(editor_state, state, entity, new_attr);
        }
    } else {
        if (direction > 0) {
            editor_state->selected_tree_index = 0;
        } else {
            editor_set_selected_attr(editor_state, state, entity, attr_last);
        }
    }
}

static void
handle_browse_delete(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int del_sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (del_sel < 0) {
        return;
    }
    Entity *del_entity = &state->gamedata.current_level.entities.data[del_sel];

    /* Tree section: X on child row -> remove blueprint child */
    int tree_idx = editor_state->selected_tree_index;
    if (tree_idx >= 0) {
        const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, del_entity->blueprint_name.ptr);
        if (blueprint && !tree_is_parent_row(del_entity, tree_idx) &&
            !tree_is_add_child_row(del_entity, blueprint, tree_idx)) {
            int child_idx = tree_child_index(del_entity, tree_idx);
            if (child_idx >= 0 && child_idx < blueprint->children.count) {
                remove_blueprint_child(state, editor_state, undo_history, child_idx);
            }
        }
        return;
    }

    /* Attr section: X on persisted/runtime attr -> remove from that set;
     * X on blueprint attr (or no attr selected) -> delete entity. */
    int del_attr = editor_resolve_selected_attr_index(state, del_entity, editor_state);
    AttrRow del_row = attr_row_at(state, del_entity, del_attr);
    if (del_row.kind == ATTR_ROW_KIND_ATTR &&
        (del_row.section == ATTR_SECTION_PERSISTED || del_row.section == ATTR_SECTION_RUNTIME)) {
        AttrSet *target = attr_section_set(state, del_entity, del_row.section);
        Attribute *del_target = &target->entries.data[del_row.index_in_section];
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        attr_remove(&alloc, target, del_target->name.ptr);
        editor_state->selected_attr_kind = EDITOR_ATTR_SEL_NONE;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Remove attribute"));
    } else if (del_attr < 0 || (del_row.kind == ATTR_ROW_KIND_ATTR && del_row.section == ATTR_SECTION_BLUEPRINT)) {
        if (delete_selection_networked(state, editor_state, watches)) {
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Delete entity"));
        }
    }
}

/* ACTION_EDITOR_MULTISELECT_ADD (S5.7, D38): resolve the entity nearest the
 * camera target -- the same target find_nearest_entity would pick for a
 * fresh plain CONFIRM -- and append it to the multi-selection instead of
 * replacing it. Does not touch selected_entity_id: a plain CONFIRM already
 * seeds/replaces the anchor selection, this action only ever grows the set
 * alongside it. */
static void handle_browse_multiselect_add(GameState *state, Camera2D *camera, EditorState *editor_state)
{
    int nearest = find_nearest_entity(&state->gamedata.current_level, camera->target);
    if (nearest < 0) {
        return;
    }
    multiselect_add(editor_state, state->gamedata.current_level.entities.data[nearest].id);
}

/* ACTION_EDITOR_COPY (S5.7, D38): snapshot every multiselect_ids entry
 * (always at least the anchor entity, per multiselect_reset_to) into
 * editor_state->copy_buffer -- blueprint name, position relative to the
 * first entry, and persisted attrs. Plain-value copy only: no Str/AttrSet
 * pointers survive into the buffer, so it stays valid across frames, undo,
 * and hot-reload. */
static void copy_cstr_truncate(char *dest, size_t cap, const char *source)
{
    size_t copy_len = strlen(source);
    if (copy_len > cap - 1) {
        copy_len = cap - 1;
    }
    memcpy(dest, source, copy_len);
    dest[copy_len] = '\0';
}

static void copy_one_attr(EditorCopyAttr *dest, const Attribute *source)
{
    copy_cstr_truncate(dest->name, EDITOR_ATTR_NAME_MAX, source->name.ptr);
    dest->type = source->type;
    switch (source->type) {
    case ATTR_FLOAT:
        dest->value.f = source->value.f;
        break;
    case ATTR_INT:
        dest->value.i = source->value.i;
        break;
    case ATTR_BOOL:
        dest->value.b = source->value.b;
        break;
    case ATTR_STRING:
        copy_cstr_truncate(dest->value.str, EDITOR_ATTR_NAME_MAX, source->value.str.ptr ? source->value.str.ptr : "");
        break;
    }
}

static void copy_one_entity(EditorCopyEntity *dest, const Entity *source, Vector2 anchor)
{
    copy_cstr_truncate(dest->blueprint_name, EDITOR_ATTR_NAME_MAX, source->blueprint_name.ptr);
    dest->relative_position = (Vector2){source->position.x - anchor.x, source->position.y - anchor.y};
    dest->attr_count = 0;
    int attr_total = source->persisted_attrs.entries.count;
    for (int attr_index = 0; attr_index < attr_total && dest->attr_count < EDITOR_COPY_ATTR_MAX; attr_index++) {
        copy_one_attr(&dest->attrs[dest->attr_count], &source->persisted_attrs.entries.data[attr_index]);
        dest->attr_count++;
    }
}

static void handle_browse_copy(const GameState *state, EditorState *editor_state)
{
    const Level *level = &state->gamedata.current_level;
    if (editor_state->multiselect_count <= 0) {
        editor_state->toast_text = strv_from_cstr("Select an entity first");
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    int anchor_index = level_find_entity_by_id(level, editor_state->multiselect_ids[0]);
    if (anchor_index < 0) {
        return;
    }
    Vector2 anchor = level->entities.data[anchor_index].position;
    int written = 0;
    for (int index = 0; index < editor_state->multiselect_count && written < EDITOR_COPY_MAX; index++) {
        int entity_index = level_find_entity_by_id(level, editor_state->multiselect_ids[index]);
        if (entity_index < 0) {
            continue;
        }
        copy_one_entity(&editor_state->copy_buffer[written], &level->entities.data[entity_index], anchor);
        written++;
    }
    editor_state->copy_buffer_count = written;
    editor_state->toast_text = strv_from_cstr("Copied");
    editor_state->toast_timer = TOAST_DURATION;
}

/* Re-apply a copied attr onto BOTH an entity's persisted_attrs (what gets
 * saved/emitted) and attrs (the runtime-read set) -- mirrors how
 * parse_entity_from_toml seeds attrs as a copy of persisted_attrs after
 * applying instance overrides (level.c), just without a shared helper to
 * call across the module boundary. */
static void apply_copied_attr(Allocator *alloc, Entity *entity, const EditorCopyAttr *source)
{
    switch (source->type) {
    case ATTR_FLOAT:
        (void)attr_set_float(alloc, &entity->persisted_attrs, source->name, source->value.f);
        (void)attr_set_float(alloc, &entity->attrs, source->name, source->value.f);
        break;
    case ATTR_INT:
        (void)attr_set_int(alloc, &entity->persisted_attrs, source->name, source->value.i);
        (void)attr_set_int(alloc, &entity->attrs, source->name, source->value.i);
        break;
    case ATTR_BOOL:
        (void)attr_set_bool(alloc, &entity->persisted_attrs, source->name, source->value.b);
        (void)attr_set_bool(alloc, &entity->attrs, source->name, source->value.b);
        break;
    case ATTR_STRING:
        (void)attr_set_string(alloc, &entity->persisted_attrs, (AttrStringPair){source->name, source->value.str});
        (void)attr_set_string(alloc, &entity->attrs, (AttrStringPair){source->name, source->value.str});
        break;
    }
}

/* Clone one copy_buffer entry at `spawn_position` via level_spawn_entity
 * (the same spawn path PLACE uses), re-applying its captured persisted
 * attrs onto the new instance. Returns the new entity's id, or -1 if the
 * blueprint no longer exists or the spawn failed. */
static int spawn_copied_entity(Diag *diag, GameState *state, const EditorCopyEntity *source, Vector2 spawn_position)
{
    Blueprint *blueprint = find_blueprint_by_name(state, source->blueprint_name);
    if (!blueprint) {
        return -1;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    int spawned_index = state->gamedata.current_level.entities.count;
    if (!level_spawn_entity(diag, &state->gamedata.current_level, blueprint, spawn_position,
                            &state->gamedata.blueprints, texture_registry_lookup, state, &alloc)) {
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
        return -1;
    }
    Entity *spawned = &state->gamedata.current_level.entities.data[spawned_index];
    for (int attr_index = 0; attr_index < source->attr_count; attr_index++) {
        apply_copied_attr(&alloc, spawned, &source->attrs[attr_index]);
    }
    return spawned->id;
}

/* ACTION_EDITOR_PASTE (S5.7, D38): clone every copy_buffer entry at
 * camera->target plus its stored offset from the first entry, preserving
 * the copied group's relative layout. Spawns via level_spawn_entity (the
 * PLACE path) then rebuilds every count-parallel runtime tracking
 * structure in ONE setup_current_level_runtime call, rather than
 * hand-patching entity_blueprints/rule_table per spawn the way
 * handle_place_input does for a single entity -- a multi-entity batch is
 * exactly the case that helper exists for. Selects the pasted clones
 * (multiselect + anchor) so a follow-up group-move/copy acts on them.
 * Does not consume the copy buffer -- the same buffer can be pasted again
 * at a different camera position. */
static void handle_browse_paste(
    Diag *diag, GameState *state, Camera2D *camera, EditorState *editor_state, UndoHistory *undo_history)
{
    if (editor_state->copy_buffer_count <= 0) {
        editor_state->toast_text = strv_from_cstr("Nothing to paste");
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    int new_ids[EDITOR_COPY_MAX];
    int new_count = 0;
    for (int index = 0; index < editor_state->copy_buffer_count; index++) {
        const EditorCopyEntity *source = &editor_state->copy_buffer[index];
        Vector2 spawn_position = {camera->target.x + source->relative_position.x,
                                  camera->target.y + source->relative_position.y};
        int new_id = spawn_copied_entity(diag, state, source, spawn_position);
        if (new_id >= 0 && new_count < EDITOR_COPY_MAX) {
            new_ids[new_count++] = new_id;
        }
    }
    if (!setup_current_level_runtime(diag, state)) {
        error_wrap(diag->error, "handle_browse_paste");
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
    }
    editor_state->multiselect_count = 0;
    for (int index = 0; index < new_count; index++) {
        multiselect_add(editor_state, new_ids[index]);
    }
    editor_state->selected_entity_id = new_count > 0 ? new_ids[0] : -1;
    editor_state->toast_text = strv_from_cstr("Pasted");
    editor_state->toast_timer = TOAST_DURATION;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Paste entities"));
}

/* Called on undo/redo. The entity/attr selection (selected_entity_id +
 * selected_attr_* identity) and the watch list all resolve by stable id/name
 * against the restored gamedata, so they survive the step — a selection whose
 * entity no longer exists simply resolves to -1 on next use, and dead watch
 * ids are skipped at draw time. Only selected_tree_index is genuinely stale:
 * it's a raw cursor into the rule-tree node list, which has no stable id yet,
 * and undo can restore a different node layout underneath it. */
static void clear_stale_tree_cursor(EditorState *editor_state)
{
    editor_state->selected_tree_index = -1;
}

static void toggle_watch(EditorState *editor_state, WatchList *watches)
{
    int entity_id = editor_state->selected_entity_id;
    if (entity_id < 0) {
        return;
    }
    for (int index = 0; index < watches->count; index++) {
        if (watches->watch_ids[index] == entity_id) {
            watches->watch_ids[index] = watches->watch_ids[watches->count - 1];
            watches->count--;
            return;
        }
    }
    if (watches->count < EDITOR_WATCH_MAX) {
        watches->watch_ids[watches->count] = entity_id;
        watches->count++;
    }
}

/* Removal for the WATCH_LIST picker: shift the tail down over the removed
 * slot, preserving the order of the remaining entries. Distinct from
 * toggle_watch's swap-with-last above, which is fine with reordering since
 * it only ever removes the entry the user is currently looking at. */
static void remove_watch_at(WatchList *watches, int index)
{
    for (int cursor = index; cursor < watches->count - 1; cursor++) {
        watches->watch_ids[cursor] = watches->watch_ids[cursor + 1];
    }
    watches->count--;
}

/* Shared Grab entry for BOTH grab sites (the Tools radial's Grab item in
 * dispatch_radial_confirm and handle_mode_transitions' ACTION_EDITOR_GRAB
 * shortcut): gate on network_editor_try_grab (S8.7d2) -- a refused grab
 * toasts and leaves the submode alone -- then capture the group's positions
 * and enter DRAG. Split out of dispatch_radial_confirm to keep its cognitive
 * complexity under the readability-function-cognitive-complexity threshold,
 * mirroring enter_tile_mode/enter_atlas_mode below. */
static void try_enter_drag_mode(GameState *state, EditorState *editor_state)
{
    if (!network_editor_try_grab(state, editor_state->multiselect_ids, editor_state->multiselect_count)) {
        editor_state->toast_text = strv_from_cstr("Locked by another player");
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    save_group_positions_for_drag(state, editor_state);
    editor_state->sub_mode = EDITOR_SUB_DRAG;
}

/* Entering Tile mode from the Tools radial: clamp the paint cursor in case
 * the last level it was set against had a larger tile grid than the
 * current one (e.g. after switching levels via Level mode). Split out of
 * dispatch_radial_confirm to keep that function's cognitive complexity
 * under the readability-function-cognitive-complexity threshold. */
static void enter_tile_mode(GameState *state, EditorState *editor_state)
{
    const Level *level = &state->gamedata.current_level;
    editor_state->top_mode = EDITOR_TOP_TILE;
    editor_state->sub_mode = EDITOR_SUB_TILE_PAINT;
    editor_state->selected_entity_id = -1;
    editor_state->multiselect_count = 0;
    if (level->tiles_wide <= 0 || editor_state->tile_cursor_col >= level->tiles_wide) {
        editor_state->tile_cursor_col = level->tiles_wide > 0 ? level->tiles_wide - 1 : 0;
    }
    if (level->tiles_high <= 0 || editor_state->tile_cursor_row >= level->tiles_high) {
        editor_state->tile_cursor_row = level->tiles_high > 0 ? level->tiles_high - 1 : 0;
    }
}

/* Entering Atlas mode from the Tools radial: mirrors enter_tile_mode's
 * split-out-for-complexity rationale. atlas_texture_index = -1 lands on
 * the texture-picker view (see editor/atlas.c). */
static void enter_atlas_mode(EditorState *editor_state)
{
    editor_state->top_mode = EDITOR_TOP_ATLAS;
    editor_state->sub_mode = EDITOR_SUB_ATLAS_BROWSE;
    editor_state->atlas_texture_index = -1;
    editor_state->atlas_texture_scroll = 0;
    editor_state->atlas_region_scroll = 0;
    editor_state->selected_entity_id = -1;
    editor_state->multiselect_count = 0;
}

/* S8.7f2: v1 structural editing is host-only. A client structural edit would
 * require a client-to-host full-gamedata upload that does not exist yet (the
 * inverse of the host-to-client resync stream), so the Tools radial's six
 * structural entries -- Blueprints / Levels / Tiles / Atlas / Animation / Rules
 * -- are refused for a connected client with a toast and no mode change; the
 * scene tools (Grab / Place / Handles / Delete / Watch list) stay available.
 * Called once at the top of dispatch_tools_radial_confirm, gated on
 * tools_entry_is_structural, so it returns true and short-circuits only under
 * NET_CLIENT. The structural token returns when the client-to-host upload
 * mechanism lands. */
static bool structural_mode_blocked_for_client(const GameState *state, EditorState *editor_state)
{
    if (state->network.mode != NET_CLIENT) {
        return false;
    }
    editor_state->toast_text = strv_from_cstr("Host only while connected");
    editor_state->toast_timer = TOAST_DURATION;
    return true;
}

/* True for the six structural entries of the RADIAL_CTX_TOOLS picker (see
 * structural_mode_blocked_for_client above); false for the scene tools
 * interleaved with them in the confirmed-index order. */
static bool tools_entry_is_structural(int confirmed)
{
    return confirmed == 4 /* Blueprints */ || confirmed == EDITOR_TOOLS_LEVELS_INDEX ||
           confirmed == EDITOR_TOOLS_TILE_INDEX || confirmed == EDITOR_TOOLS_ATLAS_INDEX ||
           confirmed == EDITOR_TOOLS_ANIM_INDEX || confirmed == EDITOR_TOOLS_RULE_INDEX;
}

/* The RADIAL_CTX_TOOLS branch of dispatch_radial_confirm. Split out to keep
 * that function's cognitive complexity under the
 * readability-function-cognitive-complexity threshold, the same
 * extract-for-complexity rationale as try_enter_drag_mode/enter_tile_mode/
 * enter_atlas_mode above. */
static void dispatch_tools_radial_confirm(
    GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history, int confirmed)
{
    if (tools_entry_is_structural(confirmed) && structural_mode_blocked_for_client(state, editor_state)) {
        return;
    }
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (confirmed == 0 && sel >= 0) { /* Grab */
        try_enter_drag_mode(state, editor_state);
    } else if (confirmed == 1) { /* Place */
        if (state->gamedata.blueprints.entries.count > 0) {
            editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_PLACE;
        }
    } else if (confirmed == 2 && sel >= 0) { /* Handles */
        const Entity *handle_entity = &state->gamedata.current_level.entities.data[sel];
        const AttrSet *handle_defaults = entity_resolve_defaults(state, handle_entity->id);
        editor_state->saved_col_offset = (Vector2){
            attr_get_scoped_float(&handle_entity->attrs, handle_defaults, "collision_offset_x", 0.0F),
            attr_get_scoped_float(&handle_entity->attrs, handle_defaults, "collision_offset_y", 0.0F),
        };
        editor_state->saved_col_size = (Vector2){
            attr_get_scoped_float(&handle_entity->attrs, handle_defaults, "collision_w", 0.0F),
            attr_get_scoped_float(&handle_entity->attrs, handle_defaults, "collision_h", 0.0F),
        };
        editor_state->sub_mode = EDITOR_SUB_HANDLES;
    } else if (confirmed == 2) { /* Handles, nothing selected */
        editor_state->toast_text = strv_from_cstr("Select an entity first");
        editor_state->toast_timer = TOAST_DURATION;
    } else if (confirmed == 3) { /* Delete */
        if (delete_selection_networked(state, editor_state, watches)) {
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Delete entity"));
        }
    } else if (confirmed == 4) { /* Blueprints */
        editor_state->top_mode = EDITOR_TOP_BLUEPRINT;
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_list_scroll = 0;
        editor_state->blueprint_attr_index = -1;
        editor_state->blueprint_tree_index = -1;
        editor_state->selected_entity_id = -1;
        editor_state->multiselect_count = 0;
    } else if (confirmed == EDITOR_TOOLS_WATCH_LIST_INDEX && watches->count > 0) { /* Watch list */
        editor_state->watch_list_scroll = 0;
        editor_state->sub_mode = EDITOR_SUB_WATCH_LIST;
    } else if (confirmed == EDITOR_TOOLS_LEVELS_INDEX) { /* Levels */
        editor_state->top_mode = EDITOR_TOP_LEVEL;
        editor_state->level_list_scroll = 0;
        editor_state->level_switch_confirm_pending = false;
        editor_state->selected_entity_id = -1;
        editor_state->multiselect_count = 0;
    } else if (confirmed == EDITOR_TOOLS_TILE_INDEX) { /* Tiles */
        enter_tile_mode(state, editor_state);
    } else if (confirmed == EDITOR_TOOLS_ATLAS_INDEX) { /* Atlas */
        enter_atlas_mode(editor_state);
    } else if (confirmed == EDITOR_TOOLS_ANIM_INDEX) { /* Animation */
        enter_anim_mode(state, editor_state);
    } else if (confirmed == EDITOR_TOOLS_RULE_INDEX) { /* Rules */
        enter_rule_mode(state, editor_state);
    }
}

static void
dispatch_radial_confirm(GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    int confirmed = editor_state->radial_confirmed;
    editor_state->radial_confirmed = -1;
    if (editor_state->radial_context == RADIAL_CTX_TOOLS) {
        dispatch_tools_radial_confirm(state, editor_state, watches, undo_history, confirmed);
    } else if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
        dispatch_attr_type_change(state, editor_state, confirmed, undo_history);
    } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
        dispatch_child_props(state, editor_state, confirmed);
    }
}

/* --- Public functions --- */

void handle_browse_input(Diag *diag,
                         GameState *state,
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
    /* Every chord action is checked before any single-atom action it shares
     * a physical atom with (the function layer is a binding lookup, not a
     * priority resolver — input_func.h), so e.g. L1+A (multi-select add)
     * doesn't also fire plain CONFIRM, and L1+Up (grid-snap toggle) doesn't
     * also fire NAV_UP. This also pushes ACTION_EDITOR_OPEN_TOOLS (bare
     * Y/Tab, shared with the COPY chord's Y atom) below the chord block. */
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_UNDO)) {
        undo_history_step_back(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        clear_stale_tree_cursor(editor_state);
        editor_state->toast_text = undo_history_description(undo_history);
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_REDO)) {
        undo_history_step_forward(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        clear_stale_tree_cursor(editor_state);
        editor_state->toast_text = undo_history_description(undo_history);
        editor_state->toast_timer = TOAST_DURATION;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_MULTISELECT_ADD)) {
        handle_browse_multiselect_add(state, camera, editor_state);
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_GRID_SNAP_TOGGLE)) {
        editor_state->grid_snap = !editor_state->grid_snap;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_COPY)) {
        handle_browse_copy(state, editor_state);
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_PASTE)) {
        handle_browse_paste(diag, state, camera, editor_state, undo_history);
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_OPEN_TOOLS)) {
        editor_state->radial_selected = -1;
        editor_state->radial_confirmed = -1;
        editor_state->radial_item_count = EDITOR_TOOLS_ITEM_COUNT;
        editor_state->radial_context = RADIAL_CTX_TOOLS;
        editor_state->sub_mode = EDITOR_SUB_RADIAL;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CONFIRM)) {
        handle_browse_select(state, camera, editor_state, undo_history);
    }
    if (input_pressed(&input, &state->bindings, ACTION_CANCEL)) {
        handle_browse_cancel(editor_state);
    }
    if (input_pressed(&input, &state->bindings, ACTION_NAV_DOWN)) {
        handle_browse_navigate(state, editor_state, 1);
    }
    if (input_pressed(&input, &state->bindings, ACTION_NAV_UP)) {
        handle_browse_navigate(state, editor_state, -1);
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_WATCH)) {
        toggle_watch(editor_state, watches);
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_DELETE)) {
        handle_browse_delete(state, editor_state, watches, undo_history);
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_PLACE)) {
        if (state->gamedata.blueprints.entries.count > 0) {
            editor_state->place_blueprint_index = find_place_blueprint_index(state, editor_state);
            editor_state->sub_mode = EDITOR_SUB_PLACE;
        }
    }
    if (input_pressed(&input, &state->bindings, ACTION_EDITOR_TYPE_PROPS)) {
        if (editor_state->selected_attr_kind != EDITOR_ATTR_SEL_NONE) {
            editor_state->radial_selected = -1;
            editor_state->radial_confirmed = -1;
            editor_state->radial_item_count = 4;
            editor_state->radial_context = RADIAL_CTX_ATTR_TYPE;
            editor_state->sub_mode = EDITOR_SUB_RADIAL;
        } else if (editor_state->selected_tree_index >= 0) {
            int r2_sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
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
    update_editor_camera(camera, &input, &state->bindings, delta_time);
}

void handle_mode_transitions(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (editor_state->sub_mode != EDITOR_SUB_BROWSE) {
        return;
    }
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return;
    }
    const Entity *entity = &state->gamedata.current_level.entities.data[sel];
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_GRAB)) {
        try_enter_drag_mode(state, editor_state);
    }
    /* Handles shortcut: keyboard-only (H). Gamepad users reach Handles via
     * Tools radial. L1 is reserved as the undo/redo chord modifier and
     * firing Handles on L1-press would race the chord check — simplest fix
     * is to drop the gamepad L1 shortcut entirely. */
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_HANDLES)) {
        const AttrSet *handle_defaults = entity_resolve_defaults(state, entity->id);
        editor_state->saved_col_offset = (Vector2){
            attr_get_scoped_float(&entity->attrs, handle_defaults, "collision_offset_x", 0.0F),
            attr_get_scoped_float(&entity->attrs, handle_defaults, "collision_offset_y", 0.0F),
        };
        editor_state->saved_col_size = (Vector2){
            attr_get_scoped_float(&entity->attrs, handle_defaults, "collision_w", 0.0F),
            attr_get_scoped_float(&entity->attrs, handle_defaults, "collision_h", 0.0F),
        };
        editor_state->sub_mode = EDITOR_SUB_HANDLES;
    }
}

/* Snap every multiselect_ids entry's CURRENT position to the grid, in
 * place. Only called at DRAG's CONFIRM (commit time), never per-frame while
 * the stick is held -- a live per-frame snap would make the group jump in
 * discrete steps instead of moving smoothly. */
static void snap_group_positions_to_grid(GameState *state, const EditorState *editor_state)
{
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        int entity_index = level_find_entity_by_id(level, editor_state->multiselect_ids[index]);
        if (entity_index < 0) {
            continue;
        }
        level->entities.data[entity_index].position =
            editor_snap_position_to_grid(level->entities.data[entity_index].position);
    }
}

/* Restore every multiselect_ids entry to its saved_group_positions entry
 * (DRAG's CANCEL). Uniform offset means this is exact even for the common
 * single-entity case -- no separate "restore just the one entity" path. */
static void restore_group_positions(GameState *state, const EditorState *editor_state)
{
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        int entity_index = level_find_entity_by_id(level, editor_state->multiselect_ids[index]);
        if (entity_index >= 0) {
            level->entities.data[entity_index].position = editor_state->saved_group_positions[index];
        }
    }
}

/* Group move (S5.7, D38): applies the same per-frame delta to every entity
 * in the multi-selection, resolved fresh by id each frame (never a cached
 * index — a prior push/reorder elsewhere in the frame could have moved
 * it). multiselect_ids always contains at least the anchor entity (see
 * multiselect_reset_to), so the single-entity case is just this loop
 * running once — no separate code path. */
void handle_drag_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CONFIRM)) {
        if (editor_state->grid_snap) {
            snap_group_positions_to_grid(state, editor_state);
        }
        Strv description =
            editor_state->multiselect_count > 1 ? strv_from_cstr("Move entities") : strv_from_cstr("Move entity");
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               description);
        /* S8.7c: propagate the committed move over the network. Both the
         * single-entity and the multi-select group move commit through this
         * one CONFIRM site (multiselect_ids always holds at least the anchor,
         * multiselect_reset_to), so one call per moved entity here covers
         * both. Each entity's final (post grid-snap) position is re-derived
         * by stable id. network_editor_commit_move is a no-op under
         * NET_OFFLINE, so single-player editing is untouched. */
        for (int index = 0; index < editor_state->multiselect_count; index++) {
            int moved_index =
                level_find_entity_by_id(&state->gamedata.current_level, editor_state->multiselect_ids[index]);
            if (moved_index < 0) {
                continue;
            }
            network_editor_commit_move(state, editor_state->multiselect_ids[index],
                                       state->gamedata.current_level.entities.data[moved_index].position);
        }
        /* S8.7d2: release the locks the grab took (no-op offline). */
        network_editor_release_locks(state, editor_state->multiselect_ids, editor_state->multiselect_count);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CANCEL)) {
        restore_group_positions(state, editor_state);
        network_editor_release_locks(state, editor_state->multiselect_ids, editor_state->multiselect_count);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    Vector2 move = input_axis_pair(&input, &state->bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    float delta_x = move.x * EDITOR_CAMERA_SPEED * delta_time;
    float delta_y = move.y * EDITOR_CAMERA_SPEED * delta_time;
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < editor_state->multiselect_count; index++) {
        int entity_index = level_find_entity_by_id(level, editor_state->multiselect_ids[index]);
        if (entity_index < 0) {
            continue;
        }
        Entity *entity = &level->entities.data[entity_index];
        entity->position.x += delta_x;
        entity->position.y += delta_y;
    }
}

/* True if any of the denied entity ids is a member of the current
 * multi-selection -- i.e. the in-flight drag is on an entity the host just
 * refused this peer a lock on. */
static bool multiselect_contains_denied(const EditorState *editor_state, const int *denied_ids, int denied_count)
{
    for (int denied = 0; denied < denied_count; denied++) {
        for (int index = 0; index < editor_state->multiselect_count; index++) {
            if (editor_state->multiselect_ids[index] == denied_ids[denied]) {
                return true;
            }
        }
    }
    return false;
}

/* S8.7d2: drain this client's LOCK_DENY backlog once per editor frame, at the
 * very top of the editor dispatch (frame.c's handle_editor_input) so a stale
 * denial can never linger and abort a future gesture. A DENY means the host
 * refused this peer's optimistic ACQUIRE. If the peer is still dragging an
 * entity that was denied, the whole gesture is aborted: positions are restored,
 * the drag returns to BROWSE, a toast explains why, and every lock the grab
 * took is released (releasing the never-granted ones is harmless -- see
 * network_editor_release_locks). A DENY that arrives AFTER the gesture already
 * committed (no matching drag in flight) is toast-only: the committed positions
 * are NOT rolled back, since the local divergence heals when full-state deltas
 * resume (frame.c's delta gate) and the undo interaction is owned by a later
 * slice. The backlog is drained (count zeroed) in every case. */
void editor_drain_lock_denials(GameState *state, EditorState *editor_state)
{
    if (state->network.lock_denied_count == 0) {
        return;
    }
    bool abort_drag = editor_state->sub_mode == EDITOR_SUB_DRAG &&
                      multiselect_contains_denied(editor_state, state->network.lock_denied_entity_ids,
                                                  state->network.lock_denied_count);
    if (abort_drag) {
        restore_group_positions(state, editor_state);
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        network_editor_release_locks(state, editor_state->multiselect_ids, editor_state->multiselect_count);
    }
    editor_state->toast_text = strv_from_cstr("Locked by another player");
    editor_state->toast_timer = TOAST_DURATION;
    state->network.lock_denied_count = 0;
}

void handle_handle_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return;
    }
    Entity *entity = &state->gamedata.current_level.entities.data[sel];
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    if (input_pressed(&input, &state->bindings, ACTION_CONFIRM)) {
        Blueprint *blueprint = find_blueprint_by_name(state, entity->blueprint_name.ptr);
        if (blueprint != nullptr) {
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_offset_x", editor_state->saved_col_offset.x);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_offset_y", editor_state->saved_col_offset.y);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_w", editor_state->saved_col_size.x);
            (void)attr_set_float(&alloc, &blueprint->attrs, "collision_h", editor_state->saved_col_size.y);
        }
        attr_remove(&alloc, &entity->attrs, "collision_offset_x");
        attr_remove(&alloc, &entity->attrs, "collision_offset_y");
        attr_remove(&alloc, &entity->attrs, "collision_w");
        attr_remove(&alloc, &entity->attrs, "collision_h");
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Resize collision"));
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CANCEL)) {
        attr_remove(&alloc, &entity->attrs, "collision_offset_x");
        attr_remove(&alloc, &entity->attrs, "collision_offset_y");
        attr_remove(&alloc, &entity->attrs, "collision_w");
        attr_remove(&alloc, &entity->attrs, "collision_h");
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    Vector2 offset_delta = input_axis_pair(&input, &state->bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    Vector2 size_delta = input_axis_pair(&input, &state->bindings, AXIS_SECONDARY_X, AXIS_SECONDARY_Y);
    editor_state->saved_col_offset.x += offset_delta.x * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->saved_col_offset.y += offset_delta.y * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->saved_col_size.x += size_delta.x * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->saved_col_size.y += size_delta.y * EDITOR_HANDLE_SPEED * delta_time;
    if (editor_state->saved_col_size.x < 0.0F) {
        editor_state->saved_col_size.x = 0.0F;
    }
    if (editor_state->saved_col_size.y < 0.0F) {
        editor_state->saved_col_size.y = 0.0F;
    }
    (void)attr_set_float(&alloc, &entity->attrs, "collision_offset_x", editor_state->saved_col_offset.x);
    (void)attr_set_float(&alloc, &entity->attrs, "collision_offset_y", editor_state->saved_col_offset.y);
    (void)attr_set_float(&alloc, &entity->attrs, "collision_w", editor_state->saved_col_size.x);
    (void)attr_set_float(&alloc, &entity->attrs, "collision_h", editor_state->saved_col_size.y);
}

/* WATCH_LIST picker: NAV_UP/DOWN clamp the focused row (matches the fuzzy
 * finder / word builder pickers, which clamp rather than wrap). CONFIRM
 * removes the focused entry; emptying the list closes back to BROWSE. */
void handle_watch_list_input(EditorState *editor_state,
                             WatchList *watches,
                             const InputState *input,
                             const BindingStore *bindings)
{
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        if (editor_state->watch_list_scroll > 0) {
            editor_state->watch_list_scroll--;
        }
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        if (editor_state->watch_list_scroll < watches->count - 1) {
            editor_state->watch_list_scroll++;
        }
    }
    if (input_pressed(input, bindings, ACTION_CONFIRM)) {
        remove_watch_at(watches, editor_state->watch_list_scroll);
        if (watches->count == 0) {
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->watch_list_scroll >= watches->count) {
            editor_state->watch_list_scroll = watches->count - 1;
        }
    }
}

/* --- Hint tables --- */

static const EditorActionHint browse_hints[] = {
    {ACTION_EDITOR_OPEN_TOOLS, "Tools"},
    {ACTION_CONFIRM, "Select / Edit"},
    {ACTION_CANCEL, "Back / Deselect"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_EDITOR_WATCH, "Watch"},
    {ACTION_EDITOR_DELETE, "Delete"},
    {ACTION_EDITOR_PLACE, "Place"},
    {ACTION_EDITOR_TYPE_PROPS, "Type / Props"},
    {ACTION_EDITOR_GRAB, "Grab"},
    {ACTION_EDITOR_HANDLES, "Handles"},
    {ACTION_EDITOR_UNDO, "Undo"},
    {ACTION_EDITOR_REDO, "Redo"},
    {ACTION_EDITOR_MULTISELECT_ADD, "Add to selection"},
    {ACTION_EDITOR_GRID_SNAP_TOGGLE, "Grid snap"},
    {ACTION_EDITOR_COPY, "Copy"},
    {ACTION_EDITOR_PASTE, "Paste"},
};

static const EditorHintTable browse_table = {
    .hints = browse_hints,
    .count = (int)(sizeof(browse_hints) / sizeof(browse_hints[0])),
    .mode_label = "Scene",
};

static const EditorActionHint drag_hints[] = {
    {ACTION_CONFIRM, "Confirm move"},
    {ACTION_CANCEL, "Cancel"},
};

static const EditorHintTable drag_table = {
    .hints = drag_hints,
    .count = (int)(sizeof(drag_hints) / sizeof(drag_hints[0])),
    .mode_label = "Drag  (stick: move entity)",
};

static const EditorActionHint handles_hints[] = {
    {ACTION_CONFIRM, "Confirm resize"},
    {ACTION_CANCEL, "Cancel"},
};

static const EditorHintTable handles_table = {
    .hints = handles_hints,
    .count = (int)(sizeof(handles_hints) / sizeof(handles_hints[0])),
    .mode_label = "Handles  (L-stick: offset, R-stick: size)",
};

static const EditorActionHint watch_list_hints[] = {
    {ACTION_NAV_UP, "Move"},
    {ACTION_NAV_DOWN, "Move"},
    {ACTION_CONFIRM, "Remove"},
    {ACTION_CANCEL, "Close"},
};

static const EditorHintTable watch_list_table = {
    .hints = watch_list_hints,
    .count = (int)(sizeof(watch_list_hints) / sizeof(watch_list_hints[0])),
    .mode_label = "Watch list",
};

const EditorHintTable *browse_hints_table(void)
{
    return &browse_table;
}

const EditorHintTable *drag_hints_table(void)
{
    return &drag_table;
}

const EditorHintTable *handles_hints_table(void)
{
    return &handles_table;
}

const EditorHintTable *watch_list_hints_table(void)
{
    return &watch_list_table;
}

#include "internal.h"

#include "alloc.h"
#include "arena.h"
#include "input.h"
#include "input_func.h"
#include "level.h" // level_mark_deleted_descendants
#include "map.h"
#include "str.h"
#include "strv.h"

#include <string.h>

/* --- Helpers --- */

static Blueprint *selected_blueprint(GameState *state, const EditorState *editor_state)
{
    int idx = editor_state->selected_blueprint_index;
    if (idx < 0 || idx >= state->gamedata.blueprints.entries.count) {
        return nullptr;
    }
    return &state->gamedata.blueprints.entries.data[idx];
}

static int blueprint_tree_total(const Blueprint *blueprint)
{
    return blueprint->children.count + 1; /* children + ADD CHILD sentinel */
}

static bool blueprint_tree_is_add_child(const Blueprint *blueprint, int tree_index)
{
    return tree_index == blueprint->children.count;
}

/* --- Navigation --- */

static void handle_blueprint_navigate(const Blueprint *blueprint, EditorState *editor_state, int direction)
{
    int tree_total = blueprint_tree_total(blueprint);
    int attr_count = blueprint->attrs.entries.count;
    int attr_sentinel = attr_count; /* the "+ADD" row */

    if (editor_state->blueprint_tree_index >= 0) {
        int new_tree = editor_state->blueprint_tree_index + direction;
        if (new_tree < 0) {
            editor_state->blueprint_tree_index = -1;
        } else if (new_tree >= tree_total) {
            editor_state->blueprint_tree_index = -1;
            editor_state->blueprint_attr_index = 0;
        } else {
            editor_state->blueprint_tree_index = new_tree;
        }
    } else if (editor_state->blueprint_attr_index >= 0) {
        int new_attr = editor_state->blueprint_attr_index + direction;
        if (new_attr < 0) {
            editor_state->blueprint_attr_index = -1;
            editor_state->blueprint_tree_index = tree_total - 1;
        } else if (new_attr > attr_sentinel) {
            editor_state->blueprint_attr_index = attr_sentinel;
        } else {
            editor_state->blueprint_attr_index = new_attr;
        }
    } else {
        if (direction > 0) {
            editor_state->blueprint_tree_index = 0;
        } else {
            editor_state->blueprint_attr_index = attr_sentinel;
        }
    }
}

/* --- Select (Enter) --- */

static void handle_blueprint_select(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }

    /* Tree section */
    if (editor_state->blueprint_tree_index >= 0) {
        if (blueprint_tree_is_add_child(blueprint, editor_state->blueprint_tree_index)) {
            fuzzy_finder_build_items(state, editor_state);
            editor_state->fuzzy_finder_scroll = 0;
            editor_state->adding_child = true;
            editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        }
        return;
    }

    /* Attr section */
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx < 0) {
        return;
    }
    int sentinel_index = blueprint->attrs.entries.count;
    if (attr_idx == sentinel_index) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->adding_blueprint_attr = true;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    if (attr_idx >= sentinel_index) {
        return;
    }
    Attribute *attr = &blueprint->attrs.entries.data[attr_idx];
    if (attr->type == ATTR_BOOL) {
        attr->value.b = !attr->value.b;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Toggle blueprint attr"));
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

/* --- Cancel (Escape) --- */

static void handle_blueprint_cancel(EditorState *editor_state)
{
    if (editor_state->blueprint_attr_index >= 0) {
        editor_state->blueprint_attr_index = -1;
    } else if (editor_state->blueprint_tree_index >= 0) {
        editor_state->blueprint_tree_index = -1;
    } else {
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_attr_index = -1;
        editor_state->blueprint_tree_index = -1;
    }
}

/* --- Delete (X) --- */

static void handle_blueprint_delete(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }

    /* Tree section: X on child row → remove blueprint child */
    int tree_idx = editor_state->blueprint_tree_index;
    if (tree_idx >= 0) {
        if (!blueprint_tree_is_add_child(blueprint, tree_idx) && tree_idx < blueprint->children.count) {
            remove_blueprint_child(state, editor_state, undo_history, tree_idx);
        }
        return;
    }

    /* Attr section: X on attr → remove from blueprint */
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx >= 0 && attr_idx < blueprint->attrs.entries.count) {
        Attribute *target = &blueprint->attrs.entries.data[attr_idx];
        const char *attr_name = target->name.ptr;
        if (strcmp(attr_name, "name") == 0) {
            return; /* never remove the name attr */
        }
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        attr_remove(&alloc, &blueprint->attrs, attr_name);
        editor_state->blueprint_attr_index = -1;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Remove blueprint attr"));
    }
}

/* --- Type change radial --- */

static void open_type_radial(EditorState *editor_state)
{
    int attr_idx = editor_state->blueprint_attr_index;
    if (attr_idx < 0) {
        return;
    }
    /* Child props radial for tree section */
    if (editor_state->blueprint_tree_index >= 0) {
        editor_state->child_edit_index = editor_state->blueprint_tree_index;
        editor_state->radial_selected = -1;
        editor_state->radial_confirmed = -1;
        editor_state->radial_item_count = 3;
        editor_state->radial_context = RADIAL_CTX_CHILD_PROPS;
        editor_state->sub_mode = EDITOR_SUB_RADIAL;
        return;
    }
    /* Attr type radial */
    editor_state->radial_selected = -1;
    editor_state->radial_confirmed = -1;
    editor_state->radial_item_count = 4;
    editor_state->radial_context = RADIAL_CTX_ATTR_TYPE;
    editor_state->sub_mode = EDITOR_SUB_RADIAL;
}

/* --- Create / Duplicate --- */

void create_blank_blueprint(GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name)
{
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    Blueprint new_bp = {0};
    new_bp.attrs.entries = vec_attribute_new(alloc);
    new_bp.children = vec_blueprint_child_new(alloc);
    new_bp.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &new_bp.attrs, (AttrStringPair){"name", name});
    if (!vec_blueprint_push(&state->gamedata.blueprints.entries, new_bp)) {
        return;
    }
    editor_state->selected_blueprint_index = state->gamedata.blueprints.entries.count - 1;
    editor_state->blueprint_attr_index = -1;
    editor_state->blueprint_tree_index = -1;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Create blueprint"));
}

void duplicate_blueprint(GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name)
{
    Blueprint *source = selected_blueprint(state, editor_state);
    if (!source) {
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    Blueprint new_bp = {0};
    new_bp.attrs.entries = vec_attribute_new(alloc);
    new_bp.children = vec_blueprint_child_new(alloc);
    new_bp.rules = vec_rule_new(alloc);

    /* Copy attrs, overriding name */
    for (int index = 0; index < source->attrs.entries.count; index++) {
        const Attribute *src_attr = &source->attrs.entries.data[index];
        if (strcmp(src_attr->name.ptr, "name") == 0) {
            (void)attr_set_string(&alloc, &new_bp.attrs, (AttrStringPair){"name", name});
        } else if (src_attr->type == ATTR_STRING) {
            (void)attr_set_string(&alloc, &new_bp.attrs, (AttrStringPair){src_attr->name.ptr, src_attr->value.str.ptr});
        } else if (src_attr->type == ATTR_INT) {
            (void)attr_set_int(&alloc, &new_bp.attrs, src_attr->name.ptr, src_attr->value.i);
        } else if (src_attr->type == ATTR_FLOAT) {
            (void)attr_set_float(&alloc, &new_bp.attrs, src_attr->name.ptr, src_attr->value.f);
        } else if (src_attr->type == ATTR_BOOL) {
            (void)attr_set_bool(&alloc, &new_bp.attrs, src_attr->name.ptr, src_attr->value.b);
        }
    }

    /* Copy children */
    for (int index = 0; index < source->children.count; index++) {
        const BlueprintChild *src_child = &source->children.data[index];
        BlueprintChild new_child = {0};
        new_child.blueprint_name = str_new(alloc);
        (void)str_from_cstr(&new_child.blueprint_name, src_child->blueprint_name.ptr);
        if (src_child->tag.len > 0) {
            new_child.tag = str_new(alloc);
            (void)str_from_cstr(&new_child.tag, src_child->tag.ptr);
        }
        new_child.offset = src_child->offset;
        (void)vec_blueprint_child_push(&new_bp.children, new_child);
    }

    /* Rules are not deep-copied — duplicated blueprints start with empty rules.
     * Rules contain complex nested ActionTree structures that require specialized
     * deep-copy logic. Rules can be added via gamedata.toml editing. */

    if (!vec_blueprint_push(&state->gamedata.blueprints.entries, new_bp)) {
        return;
    }
    editor_state->selected_blueprint_index = state->gamedata.blueprints.entries.count - 1;
    editor_state->blueprint_attr_index = -1;
    editor_state->blueprint_tree_index = -1;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Duplicate blueprint"));
}

/* --- Blueprint list view --- */

/* --- Delete blueprint --- */

static void remove_entities_by_blueprint(GameState *state, const char *bp_name)
{
    Level *level = &state->gamedata.current_level;
    int entity_count = level->entities.count;
    if (entity_count == 0) {
        return;
    }
    SCRATCH_SCOPE(&state->scratch_arena);
    bool *is_deleted = arena_alloc(&state->scratch_arena, (size_t)entity_count * sizeof(bool));
    memset(is_deleted, 0, (size_t)entity_count * sizeof(bool));

    for (int index = 0; index < entity_count; index++) {
        if (strcmp(level->entities.data[index].blueprint_name.ptr, bp_name) == 0) {
            is_deleted[index] = true;
        }
    }
    level_mark_deleted_descendants(level, is_deleted, entity_count);

    for (int index = 0; index < entity_count; index++) {
        if (is_deleted[index]) {
            int entity_id = level->entities.data[index].id;
            map_int_str_remove(&state->gamedata.entity_blueprints, entity_id);
            map_entity_ruleset_remove(&state->gamedata.rule_table, entity_id);
        }
    }
    int *new_index_map = arena_alloc(&state->scratch_arena, (size_t)entity_count * sizeof(int));
    int new_count = 0;
    for (int index = 0; index < entity_count; index++) {
        new_index_map[index] = is_deleted[index] ? -1 : new_count++;
    }
    for (int index = 0; index < entity_count; index++) {
        if (!is_deleted[index] && level->entities.data[index].parent_index >= 0) {
            level->entities.data[index].parent_index = new_index_map[level->entities.data[index].parent_index];
        }
    }
    int write = 0;
    for (int index = 0; index < entity_count; index++) {
        if (!is_deleted[index]) {
            level->entities.data[write++] = level->entities.data[index];
        }
    }
    level->entities.count = write;
    if (state->gamedata.player_index >= 0) {
        state->gamedata.player_index = new_index_map[state->gamedata.player_index];
    }
}

static void delete_blueprint(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    int bp_scroll = editor_state->blueprint_list_scroll;
    int count = state->gamedata.blueprints.entries.count;
    if (bp_scroll < 0 || bp_scroll >= count) {
        return;
    }
    const char *bp_name = attr_get_string(&state->gamedata.blueprints.entries.data[bp_scroll].attrs, "name");
    if (!bp_name) {
        return;
    }

    remove_entities_by_blueprint(state, bp_name);

    /* Remove blueprint from vec (swap with last) */
    int last = count - 1;
    if (bp_scroll < last) {
        state->gamedata.blueprints.entries.data[bp_scroll] = state->gamedata.blueprints.entries.data[last];
    }
    state->gamedata.blueprints.entries.count--;

    /* Fix scroll */
    if (editor_state->blueprint_list_scroll >= state->gamedata.blueprints.entries.count) {
        editor_state->blueprint_list_scroll = state->gamedata.blueprints.entries.count;
    }

    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Delete blueprint"));
}

static void enter_word_builder_empty(EditorState *editor_state)
{
    editor_state->word_builder_len = 0;
    editor_state->word_builder_buf[0] = '\0';
    editor_state->word_builder_scroll = 0;
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

static void handle_blueprint_list_input(GameState *state,
                                        EditorState *editor_state,
                                        UndoHistory *undo_history,
                                        const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->selected_blueprint_index = -1;
        editor_state->blueprint_list_scroll = 0;
        return;
    }

    int count = state->gamedata.blueprints.entries.count;
    int total = count + 1; /* +1 for "+NEW" sentinel */

    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN)) {
        editor_state->blueprint_list_scroll = (editor_state->blueprint_list_scroll + 1) % total;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP)) {
        editor_state->blueprint_list_scroll = (editor_state->blueprint_list_scroll - 1 + total) % total;
    }

    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        if (editor_state->blueprint_list_scroll == count) {
            /* "+NEW" sentinel — create new blueprint via word builder */
            editor_state->creating_blueprint = true;
            enter_word_builder_empty(editor_state);
        } else {
            editor_state->selected_blueprint_index = editor_state->blueprint_list_scroll;
            editor_state->blueprint_attr_index = -1;
            editor_state->blueprint_tree_index = -1;
        }
    }
    if (input_pressed(input, &state->bindings, ACTION_BLUEPRINT_REMOVE)) {
        if (editor_state->blueprint_list_scroll < count) {
            delete_blueprint(state, editor_state, undo_history);
        }
    }
}

/* --- Blueprint detail view --- */

static void handle_blueprint_detail_input(GameState *state,
                                          EditorState *editor_state,
                                          UndoHistory *undo_history,
                                          const InputState *input)
{
    Blueprint *blueprint = selected_blueprint(state, editor_state);
    if (!blueprint) {
        editor_state->selected_blueprint_index = -1;
        return;
    }

    /* Dispatch pending radial confirm */
    if (editor_state->radial_confirmed >= 0) {
        int confirmed = editor_state->radial_confirmed;
        editor_state->radial_confirmed = -1;
        if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
            dispatch_attr_type_change(state, editor_state, confirmed, undo_history);
        } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
            dispatch_child_props(state, editor_state, confirmed);
        }
        return;
    }

    /* Undo/redo runs before other actions so chord (L1+Left/Right, Ctrl+Z/Y) is not
     * masked by the plain Left/Right navigate bindings. S8.7h1: a connected client
     * reroutes the step to the host's shared history (editor_client_reroute_*,
     * core.c) instead of touching its own; host/offline run the local step. */
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_UNDO)) {
        if (editor_client_reroute_undo(state, editor_state)) {
            return;
        }
        undo_history_step_back(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_REDO)) {
        if (editor_client_reroute_redo(state, editor_state)) {
            return;
        }
        undo_history_step_forward(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        handle_blueprint_cancel(editor_state);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN)) {
        handle_blueprint_navigate(blueprint, editor_state, 1);
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP)) {
        handle_blueprint_navigate(blueprint, editor_state, -1);
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        handle_blueprint_select(state, editor_state, undo_history);
    }
    if (input_pressed(input, &state->bindings, ACTION_BLUEPRINT_REMOVE)) {
        handle_blueprint_delete(state, editor_state, undo_history);
    }
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_TYPE_PROPS)) {
        open_type_radial(editor_state);
    }
    if (input_pressed(input, &state->bindings, ACTION_BLUEPRINT_DUPLICATE)) {
        editor_state->duplicating_blueprint = true;
        enter_word_builder_empty(editor_state);
    }
}

/* --- Public --- */

void handle_blueprint_browse_input(GameState *state,
                                   EditorState *editor_state,
                                   UndoHistory *undo_history,
                                   const InputState *input)
{
    if (editor_state->selected_blueprint_index < 0) {
        handle_blueprint_list_input(state, editor_state, undo_history, input);
    } else {
        handle_blueprint_detail_input(state, editor_state, undo_history, input);
    }
}

/* --- Hint tables --- */

static const EditorActionHint blueprint_list_hints[] = {
    {ACTION_CANCEL, "Back to scene"}, {ACTION_NAV_DOWN, "Next"},        {ACTION_NAV_UP, "Prev"},
    {ACTION_CONFIRM, "Open / New"},   {ACTION_EDITOR_DELETE, "Delete"},
};

static const EditorHintTable blueprint_list_table = {
    .hints = blueprint_list_hints,
    .count = (int)(sizeof(blueprint_list_hints) / sizeof(blueprint_list_hints[0])),
    .mode_label = "Blueprint list",
};

const EditorHintTable *blueprint_list_hints_table(void)
{
    return &blueprint_list_table;
}

static const EditorActionHint blueprint_detail_hints[] = {
    {ACTION_CANCEL, "Back"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_CONFIRM, "Edit"},
    {ACTION_EDITOR_DELETE, "Remove"},
    {ACTION_EDITOR_TYPE_PROPS, "Type"},
    {ACTION_BLUEPRINT_DUPLICATE, "Duplicate"},
    {ACTION_EDITOR_UNDO, "Undo"},
    {ACTION_EDITOR_REDO, "Redo"},
};

static const EditorHintTable blueprint_detail_table = {
    .hints = blueprint_detail_hints,
    .count = (int)(sizeof(blueprint_detail_hints) / sizeof(blueprint_detail_hints[0])),
    .mode_label = "Blueprint detail",
};

const EditorHintTable *blueprint_detail_hints_table(void)
{
    return &blueprint_detail_table;
}

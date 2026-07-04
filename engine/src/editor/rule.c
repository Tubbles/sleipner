#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "rule.h"
#include "strv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Blueprint selection ---
 *
 * EDITOR_SUB_RULE_LIST double-duty (rule_blueprint_index < 0 shows the list
 * below, >=0 shows the selected blueprint's rule list) mirrors
 * anim_blueprint_index's split in editor/anim.c. A separate field rather
 * than reusing selected_blueprint_index (Blueprint mode) or
 * anim_blueprint_index follows the same precedent: each top mode keeps its
 * own navigation history independent. */

static int rule_resolve_selected_entity_blueprint_index(const GameState *state, const EditorState *editor_state)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    if (sel < 0) {
        return -1;
    }
    const char *name = state->gamedata.current_level.entities.data[sel].blueprint_name.ptr;
    for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
        const char *bp_name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        if (bp_name && strcmp(bp_name, name) == 0) {
            return index;
        }
    }
    return -1;
}

/* Entering Rule mode from the Tools radial (dispatch_radial_confirm,
 * editor/core.c). Read-only (S5.6a): no undo integration. */
void enter_rule_mode(GameState *state, EditorState *editor_state)
{
    editor_state->top_mode = EDITOR_TOP_RULE;
    editor_state->sub_mode = EDITOR_SUB_RULE_LIST;
    editor_state->rule_blueprint_index = rule_resolve_selected_entity_blueprint_index(state, editor_state);
    editor_state->rule_blueprint_scroll =
        editor_state->rule_blueprint_index >= 0 ? editor_state->rule_blueprint_index : 0;
    editor_state->rule_list_scroll = 0;
    editor_state->rule_tree_row = 0;
    editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
    editor_state->selected_entity_id = -1;
}

static const Blueprint *rule_selected_blueprint(const GameState *state, const EditorState *editor_state)
{
    int index = editor_state->rule_blueprint_index;
    if (index < 0 || index >= state->gamedata.blueprints.entries.count) {
        return nullptr;
    }
    return &state->gamedata.blueprints.entries.data[index];
}

static const Rule *rule_selected_rule(const GameState *state, const EditorState *editor_state)
{
    const Blueprint *blueprint = rule_selected_blueprint(state, editor_state);
    if (!blueprint) {
        return nullptr;
    }
    int index = editor_state->rule_list_scroll;
    if (index < 0 || index >= blueprint->rules.count) {
        return nullptr;
    }
    return &blueprint->rules.data[index];
}

/* Mutable counterpart of rule_selected_blueprint/rule_selected_rule, for
 * S5.6b's leaf-edit commit paths below. Duplicated rather than
 * const-cast because rule_selected_blueprint/rule_selected_rule are also
 * called from the const-correct draw functions further down. */
static Blueprint *rule_selected_blueprint_mut(GameState *state, const EditorState *editor_state)
{
    int index = editor_state->rule_blueprint_index;
    if (index < 0 || index >= state->gamedata.blueprints.entries.count) {
        return nullptr;
    }
    return &state->gamedata.blueprints.entries.data[index];
}

static Rule *rule_selected_rule_mut(GameState *state, const EditorState *editor_state)
{
    Blueprint *blueprint = rule_selected_blueprint_mut(state, editor_state);
    if (!blueprint) {
        return nullptr;
    }
    int index = editor_state->rule_list_scroll;
    if (index < 0 || index >= blueprint->rules.count) {
        return nullptr;
    }
    return &blueprint->rules.data[index];
}

/* --- Row model: rule_tree_row_count / rule_tree_flatten (see the RuleTreeRow
 * doc comment, editor/internal.h) --- */

/* Recurses through one children/else_children list, appending one row per
 * node and descending into that node's own children (is_else_branch=false)
 * then else_children (is_else_branch=true) before moving to the next
 * sibling in `indices` -- a depth-first walk matching
 * toml_emitter.c's emit_child_nodes_inline_array/emit_action_node_inline
 * traversal order over the same pool. is_else_start is scoped to THIS
 * specific call (position == 0 within `indices`), not inherited from
 * is_else_branch, so it fires exactly once per else_children list even when
 * nested inside an outer else branch or followed by further siblings. */
// NOLINTBEGIN(misc-no-recursion) -- bounded by control-flow nesting depth in
// authored gamedata (small, finite), same rationale as
// emit_action_node_inline's recursion (toml_emitter.c).
static void rule_tree_flatten_children(
    const vec_action_node *pool, const vec_int *indices, int depth, bool is_else_branch, RuleTreeRow *out, int *cursor)
{
    for (int position = 0; position < indices->count; position++) {
        int node_index = indices->data[position];
        const ActionNode *node = &pool->data[node_index];
        out[*cursor] = (RuleTreeRow){
            .kind = RULE_TREE_ROW_ACTION,
            .node_index = node_index,
            .depth = depth,
            .is_else_branch = is_else_branch,
            .is_else_start = is_else_branch && position == 0,
        };
        (*cursor)++;
        rule_tree_flatten_children(pool, &node->children, depth + 1, false, out, cursor);
        rule_tree_flatten_children(pool, &node->else_children, depth + 1, true, out, cursor);
    }
}
// NOLINTEND(misc-no-recursion)

int rule_tree_row_count(const Rule *rule)
{
    return 1 + rule->conditions.count + rule->action_tree.nodes.count;
}

void rule_tree_flatten(const Rule *rule, RuleTreeRow *out)
{
    int cursor = 0;
    out[cursor++] = (RuleTreeRow){.kind = RULE_TREE_ROW_TRIGGER, .node_index = -1, .depth = 0};
    for (int index = 0; index < rule->conditions.count; index++) {
        out[cursor++] = (RuleTreeRow){.kind = RULE_TREE_ROW_CONDITION, .node_index = index, .depth = 0};
    }
    rule_tree_flatten_children(&rule->action_tree.nodes, &rule->action_tree.roots, 0, false, out, &cursor);
}

/* Re-derives editor_state->rule_tree_row as a RuleTreeRow, by re-flattening
 * `rule` into a scratch-arena buffer (see rule_tree_flatten's doc comment)
 * and indexing it. S5.6b's leaf-edit code re-derives the focused row this
 * way at every step rather than caching node/condition indices in
 * EditorState, because leaf editing never changes the tree shape (no
 * nodes/conditions added or removed), so rule_tree_row stays valid across
 * an entire edit gesture -- there is nothing to keep in sync. Returns a
 * zeroed RuleTreeRow (kind RULE_TREE_ROW_TRIGGER, node_index 0) if the row
 * doesn't resolve, matching row 0 defensively rather than an out-of-range
 * read; callers that need a real row should still guard on rule being
 * non-null. */
static RuleTreeRow rule_focused_row(GameState *state, const EditorState *editor_state, const Rule *rule)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    int row_count = rule_tree_row_count(rule);
    RuleTreeRow *rows = arena_alloc(&state->scratch_arena, sizeof(RuleTreeRow) * (size_t)row_count);
    if (!rows) {
        return (RuleTreeRow){0};
    }
    rule_tree_flatten(rule, rows);
    int index = editor_state->rule_tree_row;
    if (index < 0 || index >= row_count) {
        return (RuleTreeRow){0};
    }
    return rows[index];
}

/* --- Row labels ---
 *
 * Reuse rule.h's trigger_type_label/condition_type_label/action_type_label
 * (the same vocabulary trigger_parse/condition_parse/action_mappings and
 * toml_emitter.c already encode) rather than hand-rolling a second copy. */

static void format_trigger_label(const Trigger *trigger, char *out, int cap)
{
    if (trigger->argument.len > 0) {
        (void)snprintf(out, (size_t)cap, "on %s: %s", trigger_type_label(trigger->type), trigger->argument.ptr);
    } else {
        (void)snprintf(out, (size_t)cap, "on %s", trigger_type_label(trigger->type));
    }
}

/* Comparison operator glyph for the three attr-comparison condition types --
 * condition_type_label collapses these to the bare noun "attr" (the
 * operator isn't part of that type's own vocabulary), so it's rendered
 * separately here. */
static const char *condition_operator(ConditionType type)
{
    switch (type) {
    case COND_ATTR_LT:
        return "<";
    case COND_ATTR_GT:
        return ">";
    case COND_ATTR_EQ:
        return "==";
    default:
        return "";
    }
}

static void format_condition_label(const Condition *condition, char *out, int cap)
{
    const char *operator_glyph = condition_operator(condition->type);
    if (operator_glyph[0] != '\0') {
        (void)snprintf(out, (size_t)cap, "%s: %s %s %g", condition_type_label(condition->type), condition->argument.ptr,
                       operator_glyph, (double)condition->compare_value);
    } else {
        (void)snprintf(out, (size_t)cap, "%s: %s", condition_type_label(condition->type), condition->argument.ptr);
    }
}

/* Control-flow action rows: no argument/second_argument to show via
 * action_type_label (which is empty for these three types -- see rule.c),
 * so summarize the node's own fields directly, using the same bare
 * keywords ("if"/"repeat"/"for_each") toml_emitter.c's
 * emit_action_node_inline already emits as literal TOML table keys. */
static void format_action_label(const ActionNode *node, char *out, int cap)
{
    switch (node->type) {
    case ACTION_IF_ELSE:
        (void)snprintf(out, (size_t)cap, "if (%d condition%s)", node->conditions.count,
                       node->conditions.count == 1 ? "" : "s");
        return;
    case ACTION_REPEAT:
        (void)snprintf(out, (size_t)cap, "repeat %s", node->argument.ptr);
        return;
    case ACTION_FOR_EACH:
        if (node->second_argument.len > 0) {
            (void)snprintf(out, (size_t)cap, "for_each %s as %s", node->argument.ptr, node->second_argument.ptr);
        } else {
            (void)snprintf(out, (size_t)cap, "for_each %s", node->argument.ptr);
        }
        return;
    default:
        break;
    }
    Strv label = action_type_label(node->type);
    if (node->second_argument.len > 0) {
        (void)snprintf(out, (size_t)cap, "%.*s: %s, %s", (int)label.len, label.ptr, node->argument.ptr,
                       node->second_argument.ptr);
    } else if (node->argument.len > 0) {
        (void)snprintf(out, (size_t)cap, "%.*s: %s", (int)label.len, label.ptr, node->argument.ptr);
    } else {
        (void)snprintf(out, (size_t)cap, "%.*s", (int)label.len, label.ptr);
    }
}

static void rule_tree_row_label(const Rule *rule, RuleTreeRow row, char *out, int cap)
{
    switch (row.kind) {
    case RULE_TREE_ROW_TRIGGER:
        format_trigger_label(&rule->trigger, out, cap);
        return;
    case RULE_TREE_ROW_CONDITION:
        format_condition_label(&rule->conditions.data[row.node_index], out, cap);
        return;
    case RULE_TREE_ROW_ACTION:
        format_action_label(&rule->action_tree.nodes.data[row.node_index], out, cap);
        return;
    }
}

/* --- S5.6b: leaf editing ---
 *
 * A focused EDITOR_SUB_RULE_TREE row's edit gesture is staged in
 * EditorState.rule_edit_* (see RuleEditField's doc comment, editor/editor.h)
 * across one or more of the shared transient submodes (RADIAL for the
 * type, FUZZY_FINDER/WORD_BUILDER for text, ATTR_EDIT for numbers) and
 * only committed to the real rule/condition/node once the gesture's last
 * step confirms. This means CANCEL at any point -- including mid-chain --
 * leaves the original untouched, since nothing was ever written to it.
 *
 * rule_tree_row, rule_list_scroll and rule_blueprint_index (S5.6a) already
 * identify exactly which row is focused; leaf editing never changes the
 * tree shape (no node/condition added or removed), so that identity stays
 * valid for the whole gesture and every step below re-derives "which row"
 * via rule_focused_row rather than caching a redundant node/condition
 * index.
 *
 * Row-model scope note: RuleTreeRow (editor/internal.h) only flattens
 * rule-level conditions, not a control-flow node's own predicate
 * (ActionNode.conditions) -- if_else/for_each's own conditions have no
 * navigable row yet. So CONDITION rows here are always top-level
 * rule->conditions entries, and an if_else action row's CONFIRM is a
 * no-op (nothing else on that row is directly editable). Exposing
 * predicate conditions as rows is deferred alongside structural editing
 * (S5.6c), which is also when if_else/for_each authoring needs it. */

#define RULE_EDIT_REPEAT_RADIX 10 /* decimal, matches rule.c's expand_repeat_node parse of the same field */

/* --- Type classification, grounded in rule.c/toml_emitter.c --- */

static bool trigger_type_has_argument(TriggerType type)
{
    switch (type) {
    case TRIGGER_EVENT:
    case TRIGGER_ATTR_CHANGED:
    case TRIGGER_TIMER:
    case TRIGGER_TIMER_PERIODIC:
    case TRIGGER_COLLIDE:
        return true;
    default:
        return false;
    }
}

/* Every ConditionType takes an argument (condition_parse, rule.c, always
 * captures one); only the three attr-comparison variants also need a
 * compare_value. */
static bool condition_type_needs_compare_value(ConditionType type)
{
    return type == COND_ATTR_LT || type == COND_ATTR_GT || type == COND_ATTR_EQ;
}

/* Mirrors toml_emitter.c's action_emit_table ACTION_EMIT_TWO_ARGS entries.
 * Kept as its own small predicate here rather than exported from the
 * emitter: that table's arg_count encodes TOML comma-splitting syntax,
 * not gameplay semantics -- they agree today but are different concerns,
 * matching this file's existing precedent of duplicating a small
 * classification rather than reaching across module boundaries for it
 * (see condition_operator above). */
static bool action_type_has_second_argument(ActionType type)
{
    switch (type) {
    case ACTION_TRANSITION:
    case ACTION_SET_ATTR:
    case ACTION_ADD_ATTR:
    case ACTION_CHANGE_SPRITE:
    case ACTION_SET_VAR:
    case ACTION_CREATE_TIMER:
    case ACTION_CREATE_TIMER_PERIODIC:
        return true;
    default:
        return false;
    }
}

/* Every has_args ActionType argument names an existing gamedata thing
 * (flag/attr/event/level/blueprint/subroutine/...) except dialogue text,
 * which is prose -- decides FUZZY_FINDER vs WORD_BUILDER for the first
 * argument step (begin_argument_step below). Every second_argument is a
 * value/duration/coordinate blob, never a name, so it always goes
 * straight to WORD_BUILDER regardless of this predicate. */
static bool action_argument_is_prose(ActionType type)
{
    return type == ACTION_DIALOGUE;
}

/* Radial label for one RADIAL_CTX_CONDITION_TYPE sector. condition_type_label
 * (rule.h) collapses COND_ATTR_LT/GT/EQ down to the bare noun "attr", which
 * would make three of the nine sectors show the same label -- disambiguate
 * with the comparison operator, mirroring format_condition_label's own use
 * of condition_operator above. Returns a raylib TextFormat ring-buffer
 * string, safe for immediate per-frame draw use only (same contract as
 * every other radial_label branch, editor/widgets.c). */
const char *rule_condition_radial_label(int index)
{
    if (index < 0 || index >= RULE_CONDITION_TYPE_COUNT) {
        return "";
    }
    ConditionType type = (ConditionType)index;
    const char *operator_glyph = condition_operator(type);
    if (operator_glyph[0] != '\0') {
        return TextFormat("%s %s", condition_type_label(type), operator_glyph);
    }
    return condition_type_label(type);
}

/* --- Staging helpers --- */

static void copy_into_pending(char *destination, const char *text)
{
    int length = (int)strlen(text);
    if (length >= EDITOR_ATTR_NAME_MAX) {
        length = EDITOR_ATTR_NAME_MAX - 1;
    }
    memcpy(destination, text, (size_t)length);
    destination[length] = '\0';
}

static void prefill_word_builder(EditorState *editor_state, const char *text)
{
    int length = (int)strlen(text);
    if (length >= WORD_BUILDER_BUF_SIZE) {
        length = WORD_BUILDER_BUF_SIZE - 1;
    }
    memcpy(editor_state->word_builder_buf, text, (size_t)length);
    editor_state->word_builder_buf[length] = '\0';
    editor_state->word_builder_len = length;
    editor_state->word_builder_scroll = 0;
}

/* Current (pre-edit) text of the field editor_state->rule_edit_field names
 * for the focused row. Declared in internal.h (widgets.c's
 * fuzzy_finder_enter_word_builder needs it too, for the "[ NEW... ]" ->
 * word builder path). Must be called with rule_edit_field already set to
 * ARGUMENT or SECOND_ARGUMENT so it knows which field to read. */
const char *rule_edit_current_argument_text(GameState *state, const EditorState *editor_state)
{
    const Rule *rule = rule_selected_rule(state, editor_state);
    if (!rule) {
        return "";
    }
    RuleTreeRow row = rule_focused_row(state, editor_state, rule);
    bool second = editor_state->rule_edit_field == RULE_EDIT_FIELD_SECOND_ARGUMENT;
    switch (row.kind) {
    case RULE_TREE_ROW_TRIGGER:
        return rule->trigger.argument.ptr ? rule->trigger.argument.ptr : "";
    case RULE_TREE_ROW_CONDITION:
        if (row.node_index < 0 || row.node_index >= rule->conditions.count) {
            return "";
        }
        return rule->conditions.data[row.node_index].argument.ptr ? rule->conditions.data[row.node_index].argument.ptr
                                                                  : "";
    case RULE_TREE_ROW_ACTION: {
        if (row.node_index < 0 || row.node_index >= rule->action_tree.nodes.count) {
            return "";
        }
        const ActionNode *node = &rule->action_tree.nodes.data[row.node_index];
        const Str *field = second ? &node->second_argument : &node->argument;
        return field->ptr ? field->ptr : "";
    }
    }
    return "";
}

/* Opens the argument-capturing step for whichever gesture is in flight
 * (editor_state->rule_edit_field already set by the caller to ARGUMENT or
 * SECOND_ARGUMENT): FUZZY_FINDER for a reference-shaped argument,
 * WORD_BUILDER directly otherwise (prose, or any second_argument). */
static void begin_argument_step(GameState *state, EditorState *editor_state, bool use_fuzzy_finder)
{
    editor_state->return_sub_mode = EDITOR_SUB_RULE_TREE;
    if (use_fuzzy_finder) {
        fuzzy_finder_build_items(state, editor_state);
        editor_state->fuzzy_finder_scroll = 0;
        editor_state->sub_mode = EDITOR_SUB_FUZZY_FINDER;
        return;
    }
    prefill_word_builder(editor_state, rule_edit_current_argument_text(state, editor_state));
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

/* Stages into saved_attr_float, not saved_attr_int: compare_value is a
 * float (e.g. "self.attr:health<50.5"), and seeding through an int would
 * truncate the fractional part immediately on opening the editor, even
 * before any adjustment -- unlike attr.c's own ATTR_FLOAT editing, which
 * mutates the live float value directly and so never loses precision for
 * an unchanged value. */
static void begin_compare_value_edit(GameState *state, EditorState *editor_state, RuleTreeRow row)
{
    const Rule *rule = rule_selected_rule(state, editor_state);
    float existing = 0.0F;
    if (rule && row.node_index >= 0 && row.node_index < rule->conditions.count) {
        existing = rule->conditions.data[row.node_index].compare_value;
    }
    editor_state->saved_attr_float = existing;
    editor_state->rule_edit_field = RULE_EDIT_FIELD_COMPARE_VALUE;
    editor_state->return_sub_mode = EDITOR_SUB_RULE_TREE;
    editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
}

static void begin_repeat_count_edit(EditorState *editor_state, const ActionNode *node)
{
    const char *text = node->argument.ptr ? node->argument.ptr : "";
    editor_state->saved_attr_int = (int)strtol(text, nullptr, RULE_EDIT_REPEAT_RADIX);
    editor_state->rule_edit_field = RULE_EDIT_FIELD_REPEAT_COUNT;
    editor_state->return_sub_mode = EDITOR_SUB_RULE_TREE;
    editor_state->sub_mode = EDITOR_SUB_ATTR_EDIT;
}

static void begin_for_each_bind_edit(EditorState *editor_state, const ActionNode *node)
{
    prefill_word_builder(editor_state, node->second_argument.ptr ? node->second_argument.ptr : "");
    editor_state->rule_edit_field = RULE_EDIT_FIELD_FOR_EACH_BIND;
    editor_state->return_sub_mode = EDITOR_SUB_RULE_TREE;
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

static void open_rule_type_radial(EditorState *editor_state, RadialContext context, int item_count)
{
    editor_state->radial_selected = -1;
    editor_state->radial_confirmed = -1;
    editor_state->radial_item_count = item_count;
    editor_state->radial_context = context;
    editor_state->rule_edit_field = RULE_EDIT_FIELD_TYPE;
    editor_state->return_sub_mode = EDITOR_SUB_RULE_TREE;
    editor_state->sub_mode = EDITOR_SUB_RADIAL;
}

/* --- Entry point: EDITOR_SUB_RULE_TREE's CONFIRM --- */

void begin_rule_edit_for_row(GameState *state, EditorState *editor_state)
{
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (!rule) {
        return;
    }
    RuleTreeRow row = rule_focused_row(state, editor_state, rule);
    switch (row.kind) {
    case RULE_TREE_ROW_TRIGGER:
        open_rule_type_radial(editor_state, RADIAL_CTX_TRIGGER_TYPE, RULE_TRIGGER_TYPE_COUNT);
        return;
    case RULE_TREE_ROW_CONDITION:
        open_rule_type_radial(editor_state, RADIAL_CTX_CONDITION_TYPE, RULE_CONDITION_TYPE_COUNT);
        return;
    case RULE_TREE_ROW_ACTION: {
        if (row.node_index < 0 || row.node_index >= rule->action_tree.nodes.count) {
            return;
        }
        const ActionNode *node = &rule->action_tree.nodes.data[row.node_index];
        if (node->type == ACTION_IF_ELSE) {
            return; /* nothing directly editable on the row itself, see the scope note above */
        }
        if (node->type == ACTION_REPEAT) {
            begin_repeat_count_edit(editor_state, node);
            return;
        }
        if (node->type == ACTION_FOR_EACH) {
            begin_for_each_bind_edit(editor_state, node);
            return;
        }
        open_rule_type_radial(editor_state, RADIAL_CTX_ACTION_TYPE, RULE_ACTION_TYPE_COUNT);
        return;
    }
    }
}

/* --- Commit paths: each fully owns the sub_mode/rule_edit_field/
 * return_sub_mode transition and pushes its own undo entry -- callers
 * (widgets.c, attr.c) must not also touch those fields afterward. --- */

static void finalize_rule_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (!rule) {
        editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
        return;
    }
    RuleTreeRow row = rule_focused_row(state, editor_state, rule);
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    const char *description = "Edit rule";
    switch (row.kind) {
    case RULE_TREE_ROW_TRIGGER: {
        TriggerType new_type = (TriggerType)editor_state->rule_edit_pending_type;
        rule->trigger.type = new_type;
        rule->trigger.argument = str_new(alloc);
        if (trigger_type_has_argument(new_type)) {
            (void)str_append_cstr(&rule->trigger.argument, editor_state->rule_edit_pending_argument);
        }
        description = "Edit rule trigger";
        break;
    }
    case RULE_TREE_ROW_CONDITION: {
        if (row.node_index < 0 || row.node_index >= rule->conditions.count) {
            break;
        }
        Condition *condition = &rule->conditions.data[row.node_index];
        ConditionType new_type = (ConditionType)editor_state->rule_edit_pending_type;
        condition->type = new_type;
        condition->argument = str_new(alloc);
        (void)str_append_cstr(&condition->argument, editor_state->rule_edit_pending_argument);
        if (condition_type_needs_compare_value(new_type)) {
            condition->compare_value = editor_state->saved_attr_float;
        }
        description = "Edit rule condition";
        break;
    }
    case RULE_TREE_ROW_ACTION: {
        if (row.node_index < 0 || row.node_index >= rule->action_tree.nodes.count) {
            break;
        }
        ActionNode *node = &rule->action_tree.nodes.data[row.node_index];
        ActionType new_type = (ActionType)editor_state->rule_edit_pending_type;
        node->type = new_type;
        node->argument = str_new(alloc);
        if (new_type != ACTION_DESTROY) {
            (void)str_append_cstr(&node->argument, editor_state->rule_edit_pending_argument);
        }
        node->second_argument = str_new(alloc);
        if (action_type_has_second_argument(new_type)) {
            (void)str_append_cstr(&node->second_argument, editor_state->rule_edit_pending_second_argument);
        }
        description = "Edit rule action";
        break;
    }
    }
    editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
    editor_state->sub_mode = editor_state->return_sub_mode;
    editor_state->return_sub_mode = EDITOR_SUB_BROWSE;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr(description));
}

void rule_edit_argument_step_complete(GameState *state,
                                      EditorState *editor_state,
                                      UndoHistory *undo_history,
                                      const char *text)
{
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (!rule) {
        editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
        return;
    }
    RuleTreeRow row = rule_focused_row(state, editor_state, rule);

    if (editor_state->rule_edit_field == RULE_EDIT_FIELD_SECOND_ARGUMENT) {
        copy_into_pending(editor_state->rule_edit_pending_second_argument, text);
        finalize_rule_edit(state, editor_state, undo_history);
        return;
    }

    copy_into_pending(editor_state->rule_edit_pending_argument, text);
    if (row.kind == RULE_TREE_ROW_CONDITION &&
        condition_type_needs_compare_value((ConditionType)editor_state->rule_edit_pending_type)) {
        begin_compare_value_edit(state, editor_state, row);
        return;
    }
    if (row.kind == RULE_TREE_ROW_ACTION &&
        action_type_has_second_argument((ActionType)editor_state->rule_edit_pending_type)) {
        editor_state->rule_edit_field = RULE_EDIT_FIELD_SECOND_ARGUMENT;
        begin_argument_step(state, editor_state, false /* second_argument is always a value blob, never a name ref */);
        return;
    }
    finalize_rule_edit(state, editor_state, undo_history);
}

void commit_rule_for_each_bind(GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *text)
{
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (rule) {
        RuleTreeRow row = rule_focused_row(state, editor_state, rule);
        if (row.kind == RULE_TREE_ROW_ACTION && row.node_index >= 0 && row.node_index < rule->action_tree.nodes.count) {
            ActionNode *node = &rule->action_tree.nodes.data[row.node_index];
            Allocator alloc = allocator_arena(&state->gamedata_arena);
            node->second_argument = str_new(alloc);
            (void)str_append_cstr(&node->second_argument, text);
        }
    }
    editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
    editor_state->sub_mode = editor_state->return_sub_mode;
    editor_state->return_sub_mode = EDITOR_SUB_BROWSE;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit rule action"));
}

static void commit_rule_repeat_count(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (rule) {
        RuleTreeRow row = rule_focused_row(state, editor_state, rule);
        if (row.kind == RULE_TREE_ROW_ACTION && row.node_index >= 0 && row.node_index < rule->action_tree.nodes.count) {
            ActionNode *node = &rule->action_tree.nodes.data[row.node_index];
            Allocator alloc = allocator_arena(&state->gamedata_arena);
            node->argument = str_new(alloc);
            (void)str_append_cstr(&node->argument, TextFormat("%d", editor_state->saved_attr_int));
        }
    }
    editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
    editor_state->sub_mode = editor_state->return_sub_mode;
    editor_state->return_sub_mode = EDITOR_SUB_BROWSE;
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Edit rule action"));
}

/* Applies one nudge to whichever field is in flight: saved_attr_int for
 * RULE_EDIT_FIELD_REPEAT_COUNT (floors at 1, matching expand_repeat_node's
 * runtime floor, rule.c), saved_attr_float for RULE_EDIT_FIELD_COMPARE_VALUE
 * (no natural floor). Kept as a float accumulator for compare_value rather
 * than staging through saved_attr_int -- that would truncate an existing
 * fractional value (e.g. "self.attr:health<50.5") the instant the editor
 * opens, even before any adjustment. */
static void apply_rule_numeric_delta(EditorState *editor_state, int delta)
{
    if (delta == 0) {
        return;
    }
    if (editor_state->rule_edit_field == RULE_EDIT_FIELD_REPEAT_COUNT) {
        editor_state->saved_attr_int += delta;
        if (editor_state->saved_attr_int < 1) {
            editor_state->saved_attr_int = 1;
        }
    } else {
        editor_state->saved_attr_float += (float)delta;
    }
}

/* Drives the standalone RULE_EDIT_FIELD_REPEAT_COUNT / COMPARE_VALUE
 * numeric sessions -- called from attr.c's handle_attr_edit_input, which
 * reaches EDITOR_SUB_ATTR_EDIT for both entity/blueprint Attribute editing
 * and this. Mirrors attr.c's own handle_child_offset_edit hold/accelerate
 * shape (same exported read_value_delta/read_held_dir/reset_attr_hold,
 * see internal.h's doc comment), but stages purely in
 * saved_attr_int/saved_attr_float and never touches the real
 * condition/node field until CONFIRM -- so CANCEL never mutates, matching
 * every other S5.6b gesture. CONFIRM routes to finalize_rule_edit (not
 * commit_rule_repeat_count) for RULE_EDIT_FIELD_COMPARE_VALUE, since that
 * field is the tail of a type+argument chain that hasn't been committed
 * yet (see rule_edit_argument_step_complete's begin_compare_value_edit
 * call). */
void handle_rule_numeric_edit_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input, float delta_time)
{
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        if (editor_state->rule_edit_field == RULE_EDIT_FIELD_REPEAT_COUNT) {
            commit_rule_repeat_count(state, editor_state, undo_history);
        } else {
            finalize_rule_edit(state, editor_state, undo_history);
        }
        reset_attr_hold(editor_state);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        reset_attr_hold(editor_state);
        editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
        editor_state->sub_mode = editor_state->return_sub_mode;
        editor_state->return_sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    apply_rule_numeric_delta(editor_state, read_value_delta(input, &state->bindings));
    int held = read_held_dir(input, &state->bindings);
    if (held != editor_state->attr_hold_dir) {
        editor_state->attr_hold_dir = held;
        editor_state->attr_hold_total = 0.0F;
        editor_state->attr_hold_subtick = 0.0F;
        apply_rule_numeric_delta(editor_state, held);
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
        apply_rule_numeric_delta(editor_state, held);
    }
}

/* --- RADIAL_CTX_TRIGGER_TYPE / CONDITION_TYPE / ACTION_TYPE confirm --- */

void dispatch_rule_radial_confirm(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    int confirmed = editor_state->radial_confirmed;
    editor_state->radial_confirmed = -1;
    Rule *rule = rule_selected_rule_mut(state, editor_state);
    if (!rule || confirmed < 0) {
        editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
        return;
    }
    RadialContext context = editor_state->radial_context;
    editor_state->rule_edit_pending_type = confirmed;

    if (context == RADIAL_CTX_TRIGGER_TYPE) {
        if (confirmed >= RULE_TRIGGER_TYPE_COUNT) {
            editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
            return;
        }
        if (!trigger_type_has_argument((TriggerType)confirmed)) {
            finalize_rule_edit(state, editor_state, undo_history);
            return;
        }
        editor_state->rule_edit_field = RULE_EDIT_FIELD_ARGUMENT;
        begin_argument_step(state, editor_state, true /* trigger args are always refs (event/attr/timer/collide) */);
        return;
    }
    if (context == RADIAL_CTX_CONDITION_TYPE) {
        if (confirmed >= RULE_CONDITION_TYPE_COUNT) {
            editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
            return;
        }
        editor_state->rule_edit_field = RULE_EDIT_FIELD_ARGUMENT;
        begin_argument_step(state, editor_state, true /* condition args are always refs (flag/attr/item/var) */);
        return;
    }
    if (context == RADIAL_CTX_ACTION_TYPE) {
        if (confirmed >= RULE_ACTION_TYPE_COUNT) {
            editor_state->rule_edit_field = RULE_EDIT_FIELD_NONE;
            return;
        }
        ActionType new_type = (ActionType)confirmed;
        if (new_type == ACTION_DESTROY) {
            finalize_rule_edit(state, editor_state, undo_history);
            return;
        }
        editor_state->rule_edit_field = RULE_EDIT_FIELD_ARGUMENT;
        begin_argument_step(state, editor_state, !action_argument_is_prose(new_type));
    }
}

/* --- RULE_LIST: blueprint picker (rule_blueprint_index < 0) --- */

static void handle_rule_blueprint_list_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int count = state->gamedata.blueprints.entries.count;
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->rule_blueprint_scroll < count - 1) {
        editor_state->rule_blueprint_scroll++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->rule_blueprint_scroll > 0) {
        editor_state->rule_blueprint_scroll--;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM) && count > 0) {
        editor_state->rule_blueprint_index = editor_state->rule_blueprint_scroll;
        editor_state->rule_list_scroll = 0;
    }
}

/* --- RULE_LIST: rule list for the chosen blueprint (rule_blueprint_index >= 0) --- */

static void handle_rule_row_list_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->rule_blueprint_index = -1;
        return;
    }
    const Blueprint *blueprint = rule_selected_blueprint(state, editor_state);
    int count = blueprint ? blueprint->rules.count : 0;
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->rule_list_scroll < count - 1) {
        editor_state->rule_list_scroll++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->rule_list_scroll > 0) {
        editor_state->rule_list_scroll--;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM) && count > 0) {
        editor_state->rule_tree_row = 0;
        editor_state->sub_mode = EDITOR_SUB_RULE_TREE;
    }
}

void handle_rule_list_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (editor_state->rule_blueprint_index < 0) {
        handle_rule_blueprint_list_input(state, editor_state, input);
    } else {
        handle_rule_row_list_input(state, editor_state, input);
    }
}

/* --- RULE_TREE --- */

void handle_rule_tree_input(GameState *state,
                            EditorState *editor_state,
                            UndoHistory *undo_history,
                            const InputState *input)
{
    /* Dispatch a pending TYPE-radial confirm first, before anything else --
     * mirrors handle_blueprint_detail_input's own radial-confirm check
     * (editor/blueprint.c), the precedent for a top mode whose own
     * "resting" submode isn't EDITOR_SUB_BROWSE dispatching its radial
     * confirms itself rather than through core.c's dispatch_radial_confirm. */
    if (editor_state->radial_confirmed >= 0) {
        dispatch_rule_radial_confirm(state, editor_state, undo_history);
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_RULE_LIST;
        return;
    }
    const Rule *rule = rule_selected_rule(state, editor_state);
    if (!rule) {
        return;
    }
    int row_count = rule_tree_row_count(rule);
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->rule_tree_row < row_count - 1) {
        editor_state->rule_tree_row++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->rule_tree_row > 0) {
        editor_state->rule_tree_row--;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        begin_rule_edit_for_row(state, editor_state);
    }
}

/* --- Draw --- */

static void draw_rule_blueprint_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int count = state->gamedata.blueprints.entries.count;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Rules: Pick Blueprint ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    if (count == 0) {
        draw_ui_text(font, "(no blueprints)", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                     debug_text_color);
        return;
    }

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->rule_blueprint_scroll - (visible / 2);
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
        bool selected = (index == editor_state->rule_blueprint_scroll);
        Color color = selected ? WHITE : debug_text_color;
        const char *name = attr_get_string(&state->gamedata.blueprints.entries.data[index].attrs, "name");
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", name ? name : "?"), panel_x + DEBUG_MARGIN,
                     y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void draw_rule_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    const Blueprint *blueprint = rule_selected_blueprint(state, editor_state);
    if (!blueprint) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    const char *name = attr_get_string(&blueprint->attrs, "name");
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ %s: Rules ]", name ? name : "?"), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int count = blueprint->rules.count;
    if (count == 0) {
        draw_ui_text(font, "(no rules)", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
        return;
    }

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->rule_list_scroll - (visible / 2);
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
        bool selected = (index == editor_state->rule_list_scroll);
        Color color = selected ? WHITE : debug_text_color;
        char label[96];
        format_trigger_label(&blueprint->rules.data[index].trigger, label, (int)sizeof(label));
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", label), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

/* Live preview for the focused row while a standalone numeric edit
 * (RULE_EDIT_FIELD_REPEAT_COUNT / COMPARE_VALUE, S5.6b) is in flight --
 * those stage in saved_attr_int/saved_attr_float rather than mutating the
 * real rule data (see RuleEditField's doc comment, editor/editor.h), so
 * the normal rule_tree_row_label call would otherwise show the stale
 * pre-edit value for the whole session. Returns false (out untouched)
 * for every other row/state, so the caller falls back to the normal
 * label. */
static bool
format_rule_edit_preview_label(const Rule *rule, const EditorState *editor_state, RuleTreeRow row, char *out, int cap)
{
    if (editor_state->rule_edit_field == RULE_EDIT_FIELD_REPEAT_COUNT && row.kind == RULE_TREE_ROW_ACTION) {
        (void)snprintf(out, (size_t)cap, "repeat %d", editor_state->saved_attr_int);
        return true;
    }
    if (editor_state->rule_edit_field == RULE_EDIT_FIELD_COMPARE_VALUE && row.kind == RULE_TREE_ROW_CONDITION &&
        row.node_index >= 0 && row.node_index < rule->conditions.count) {
        const Condition *condition = &rule->conditions.data[row.node_index];
        (void)snprintf(out, (size_t)cap, "%s: %s %s %g", condition_type_label(condition->type), condition->argument.ptr,
                       condition_operator(condition->type), (double)editor_state->saved_attr_float);
        return true;
    }
    return false;
}

/* Non-const state (unlike its siblings above): flattens the rule's action
 * tree into a scratch-arena buffer to render it (rule_tree_flatten needs a
 * caller-owned buffer -- see the RuleTreeRow doc comment, editor/internal.h),
 * which needs a mutable Arena. */
static void draw_rule_tree_panel(ScreenSize screen, GameState *state, const EditorState *editor_state)
{
    const Rule *rule = rule_selected_rule(state, editor_state);
    if (!rule) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Rule ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    SCRATCH_SCOPE(&state->scratch_arena);
    int row_count = rule_tree_row_count(rule);
    RuleTreeRow *rows = arena_alloc(&state->scratch_arena, sizeof(RuleTreeRow) * (size_t)row_count);
    if (!rows) {
        return;
    }
    rule_tree_flatten(rule, rows);

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->rule_tree_row - (visible / 2);
    if (scroll < 0) {
        scroll = 0;
    }
    int max_scroll = row_count - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    int end = scroll + visible;
    if (end > row_count) {
        end = row_count;
    }
    for (int index = scroll; index < end; index++) {
        RuleTreeRow row = rows[index];
        int indent = row.depth * RULE_TREE_INDENT_PX;
        if (row.is_else_start) {
            draw_ui_text(font, "else:", panel_x + DEBUG_MARGIN + indent, y_offset, EDITOR_PANEL_FONT_SIZE,
                         debug_text_color);
            y_offset += EDITOR_PANEL_LINE_HEIGHT;
        }
        bool selected = (index == editor_state->rule_tree_row);
        Color color = selected ? WHITE : debug_text_color;
        char label[96];
        if (!selected || !format_rule_edit_preview_label(rule, editor_state, row, label, (int)sizeof(label))) {
            rule_tree_row_label(rule, row, label, (int)sizeof(label));
        }
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", label), panel_x + DEBUG_MARGIN + indent, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

/* sub_mode == EDITOR_SUB_ATTR_EDIT is included alongside RULE_TREE because
 * S5.6b's standalone repeat-count/compare_value numeric edits (see
 * RuleEditField) borrow the shared adjuster submode without a dedicated
 * draw panel of their own -- the tree view's live preview
 * (format_rule_edit_preview_label above) covers it. main.c's
 * draw_active_editor_panel only reaches this function for ATTR_EDIT when
 * top_mode is EDITOR_TOP_RULE, so no other mode's ATTR_EDIT session can
 * reach here. */
void draw_rule_panel(ScreenSize screen, GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode == EDITOR_SUB_RULE_TREE || editor_state->sub_mode == EDITOR_SUB_ATTR_EDIT) {
        draw_rule_tree_panel(screen, state, editor_state);
    } else if (editor_state->rule_blueprint_index >= 0) {
        draw_rule_list_panel(screen, state, editor_state);
    } else {
        draw_rule_blueprint_list_panel(screen, state, editor_state);
    }
}

/* --- Hint tables --- */

static const EditorActionHint rule_blueprint_list_hints[] = {
    {ACTION_CANCEL, "Back to scene"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Select"},
};

static const EditorHintTable rule_blueprint_list_table = {
    .hints = rule_blueprint_list_hints,
    .count = (int)(sizeof(rule_blueprint_list_hints) / sizeof(rule_blueprint_list_hints[0])),
    .mode_label = "Rules: pick blueprint",
};

const EditorHintTable *rule_blueprint_list_hints_table(void)
{
    return &rule_blueprint_list_table;
}

static const EditorActionHint rule_list_hints[] = {
    {ACTION_CANCEL, "Back to blueprints"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Open"},
};

static const EditorHintTable rule_list_table = {
    .hints = rule_list_hints,
    .count = (int)(sizeof(rule_list_hints) / sizeof(rule_list_hints[0])),
    .mode_label = "Rule list",
};

const EditorHintTable *rule_list_hints_table(void)
{
    return &rule_list_table;
}

static const EditorActionHint rule_tree_hints[] = {
    {ACTION_CANCEL, "Back to list"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Edit"},
};

static const EditorHintTable rule_tree_table = {
    .hints = rule_tree_hints,
    .count = (int)(sizeof(rule_tree_hints) / sizeof(rule_tree_hints[0])),
    .mode_label = "Rule tree",
};

const EditorHintTable *rule_tree_hints_table(void)
{
    return &rule_tree_table;
}

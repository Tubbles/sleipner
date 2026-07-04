#include "editor/internal.h"

#include "arena.h"
#include "rule.h"
#include "strv.h"

#include <stdio.h>
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

void handle_rule_tree_input(GameState *state, EditorState *editor_state, const InputState *input)
{
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
        rule_tree_row_label(rule, row, label, (int)sizeof(label));
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", label), panel_x + DEBUG_MARGIN + indent, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_rule_panel(ScreenSize screen, GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode == EDITOR_SUB_RULE_TREE) {
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

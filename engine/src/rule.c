#include "rule.h"

#include "alloc.h"
#include "vec.h"
#include "arena.h"
#include "attribute.h"
#include "debug.h"
#include "diag.h"
#include "effect.h"
#include "entity.h"
#include "error.h"
#include "map.h"
#include "str.h"
#include "strv.h"

#include "toml.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PARSE_CF_STACK 64
#define GLOBAL_VAR_PREFIX "global."
#define MAX_EXEC_STACK 512
#define RADIX_DECIMAL 10

VEC_IMPL(flag_name, FlagName)
VEC_IMPL(condition, Condition)
VEC_IMPL(action_node, ActionNode)
VEC_IMPL(rule, Rule)
VEC_IMPL(trigger_event, TriggerEvent)
VEC_IMPL(subroutine, Subroutine)
VEC_IMPL(timer, Timer)
MAP_IMPL(entity_ruleset, int, vec_rule, map_hash_int, map_eq_int)

/* ---- FlagSet ---- */

static int flag_find(const FlagSet *flags, const char *name)
{
    for (int index = 0; index < flags->names.count; index++) {
        if (strv_eq_cstr(str_to_strv(flags->names.data[index].name), name)) {
            return index;
        }
    }
    return -1;
}

bool flag_get(const FlagSet *flags, const char *name)
{
    return flag_find(flags, name) >= 0;
}

void flag_set(Diag *diag, Allocator *alloc, FlagSet *flags, const char *name)
{
    if (flag_find(flags, name) >= 0) {
        return;
    }
    flags->names.alloc = *alloc;
    FlagName entry = {0};
    entry.name = str_new(*alloc);
    if (!str_from_cstr(&entry.name, name)) {
        debug_log(diag->debug, "flag_set: allocation failed for '%s'", name);
        return;
    }
    if (!vec_flag_name_push(&flags->names, entry)) {
        str_free(&entry.name);
        debug_log(diag->debug, "flag_set: vec push failed for '%s'", name);
    }
}

void flag_clear(Allocator *alloc, FlagSet *flags, const char *name)
{
    (void)alloc;
    int index = flag_find(flags, name);
    if (index < 0) {
        return;
    }
    str_free(&flags->names.data[index].name);
    flags->names.count--;
    if (index < flags->names.count) {
        flags->names.data[index] = flags->names.data[flags->names.count];
    }
}

void flag_set_free(Allocator *alloc, FlagSet *flags)
{
    (void)alloc;
    for (int index = 0; index < flags->names.count; index++) {
        str_free(&flags->names.data[index].name);
    }
    vec_flag_name_free(&flags->names);
    *flags = (FlagSet){0};
}

/* ---- Parsing helpers ---- */

bool trigger_parse(Diag *diag, Allocator *alloc, Trigger *trigger, const char *string)
{
    memset(trigger, 0, sizeof(*trigger));
    trigger->argument = str_new(*alloc);
    Strv strv = strv_from_cstr(string);

    if (strv_eq_cstr(strv, "interact")) {
        trigger->type = TRIGGER_INTERACT;
        return true;
    }
    if (strv_eq_cstr(strv, "enter")) {
        trigger->type = TRIGGER_ENTER;
        return true;
    }
    if (strv_eq_cstr(strv, "on_spawn")) {
        trigger->type = TRIGGER_ON_SPAWN;
        return true;
    }
    if (strv_starts_with_cstr(strv, "event:")) {
        trigger->type = TRIGGER_EVENT;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&trigger->argument, rest)) {
            error_set(diag->error, "trigger_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "attr_changed:")) {
        trigger->type = TRIGGER_ATTR_CHANGED;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&trigger->argument, rest)) {
            error_set(diag->error, "trigger_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "timer_periodic:")) {
        trigger->type = TRIGGER_TIMER_PERIODIC;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&trigger->argument, rest)) {
            error_set(diag->error, "trigger_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "timer:")) {
        trigger->type = TRIGGER_TIMER;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&trigger->argument, rest)) {
            error_set(diag->error, "trigger_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_eq_cstr(strv, "on_destroy")) {
        trigger->type = TRIGGER_ON_DESTROY;
        return true;
    }
    if (strv_eq_cstr(strv, "defeat")) {
        trigger->type = TRIGGER_DEFEAT;
        return true;
    }
    if (strv_starts_with_cstr(strv, "collide:")) {
        trigger->type = TRIGGER_COLLIDE;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&trigger->argument, rest)) {
            error_set(diag->error, "trigger_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_eq_cstr(strv, "collide")) {
        trigger->type = TRIGGER_COLLIDE;
        return true;
    }

    error_set(diag->error, "unknown trigger type: '%s'", string);
    return false;
}

const char *trigger_type_label(TriggerType type)
{
    switch (type) {
    case TRIGGER_INTERACT:
        return "interact";
    case TRIGGER_ENTER:
        return "enter";
    case TRIGGER_ON_SPAWN:
        return "on_spawn";
    case TRIGGER_EVENT:
        return "event";
    case TRIGGER_ATTR_CHANGED:
        return "attr_changed";
    case TRIGGER_TIMER:
        return "timer";
    case TRIGGER_TIMER_PERIODIC:
        return "timer_periodic";
    case TRIGGER_ON_DESTROY:
        return "on_destroy";
    case TRIGGER_DEFEAT:
        return "defeat";
    case TRIGGER_COLLIDE:
        return "collide";
    }
    return "?";
}

// Returns: 1 = comparison found and allocated, 0 = no comparison, -1 = alloc failed
static int parse_condition_attr_comparison(Allocator *alloc, Condition *condition, const char *argument)
{
    (void)alloc;
    const char *less_than = strchr(argument, '<');
    const char *greater_than = strchr(argument, '>');
    const char *equals = strstr(argument, "==");

    if (equals) {
        condition->type = COND_ATTR_EQ;
        Strv name = {.ptr = argument, .len = (size_t)(equals - argument)};
        if (!str_from_strv(&condition->argument, name)) {
            return -1;
        }
        condition->compare_value = strtof(equals + 2, nullptr);
        return 1;
    }
    if (less_than) {
        condition->type = COND_ATTR_LT;
        Strv name = {.ptr = argument, .len = (size_t)(less_than - argument)};
        if (!str_from_strv(&condition->argument, name)) {
            return -1;
        }
        condition->compare_value = strtof(less_than + 1, nullptr);
        return 1;
    }
    if (greater_than) {
        condition->type = COND_ATTR_GT;
        Strv name = {.ptr = argument, .len = (size_t)(greater_than - argument)};
        if (!str_from_strv(&condition->argument, name)) {
            return -1;
        }
        condition->compare_value = strtof(greater_than + 1, nullptr);
        return 1;
    }

    return 0;
}

bool condition_parse(Diag *diag, Allocator *alloc, Condition *condition, const char *string)
{
    memset(condition, 0, sizeof(*condition));
    condition->argument = str_new(*alloc);
    Strv strv = strv_from_cstr(string);

    if (strv_starts_with_cstr(strv, "not_flag:")) {
        condition->type = COND_NOT_FLAG;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "flag:")) {
        condition->type = COND_FLAG;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "has_item:")) {
        condition->type = COND_HAS_ITEM;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "var:")) {
        condition->type = COND_VAR;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }
    if (strv_starts_with_cstr(strv, "not_attr:")) {
        condition->type = COND_NOT_ATTR;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }
    if ((int)strv_starts_with_cstr(strv, "self.attr:") || (int)strv_starts_with_cstr(strv, "attr:")) {
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        char attr_arg[MAX_ARG];
        strv_copy_to_cstr(rest, attr_arg, MAX_ARG);
        int cmp = parse_condition_attr_comparison(alloc, condition, attr_arg);
        if (cmp < 0) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        if (cmp > 0) {
            return true;
        }
        condition->type = COND_ATTR;
        if (!str_from_strv(&condition->argument, rest)) {
            error_set(diag->error, "condition_parse: allocation failed");
            return false;
        }
        return true;
    }

    error_set(diag->error, "unknown condition: '%s'", string);
    return false;
}

/* The three attr-comparison variants (LT/GT/EQ) share "attr" here; callers
 * render the comparison operator themselves (see editor/rule.c's row-label
 * formatting) since the operator isn't part of the type's own vocabulary. */
const char *condition_type_label(ConditionType type)
{
    switch (type) {
    case COND_FLAG:
        return "flag";
    case COND_NOT_FLAG:
        return "not_flag";
    case COND_ATTR:
    case COND_ATTR_LT:
    case COND_ATTR_GT:
    case COND_ATTR_EQ:
        return "attr";
    case COND_NOT_ATTR:
        return "not_attr";
    case COND_HAS_ITEM:
        return "has_item";
    case COND_VAR:
        return "var";
    }
    return "?";
}

static bool parse_action_two_args(Allocator *alloc, ActionNode *node, Strv strv)
{
    node->argument = str_new(*alloc);
    node->second_argument = str_new(*alloc);
    Strv first = strv_split(&strv, ',');
    if (!str_from_strv(&node->argument, first)) {
        return false;
    }
    if (strv.ptr) {
        if (!str_from_strv(&node->second_argument, strv)) {
            str_free(&node->argument);
            return false;
        }
    }
    return true;
}

typedef struct {
    const char *prefix;
    ActionType type;
    bool has_args;
} ActionMapping;

static const ActionMapping action_mappings[] = {
    {"set_flag:", ACTION_SET_FLAG, true},
    {"clear_flag:", ACTION_CLEAR_FLAG, true},
    {"set_attr:", ACTION_SET_ATTR, true},
    {"add_attr:", ACTION_ADD_ATTR, true},
    {"toggle_attr:", ACTION_TOGGLE_ATTR, true},
    {"destroy", ACTION_DESTROY, false},
    {"fire_event:", ACTION_FIRE_EVENT, true},
    {"give_item:", ACTION_GIVE_ITEM, true},
    {"remove_item:", ACTION_REMOVE_ITEM, true},
    {"change_sprite:", ACTION_CHANGE_SPRITE, true},
    {"play_sound:", ACTION_PLAY_SOUND, true},
    {"dialogue:", ACTION_DIALOGUE, true},
    {"transition:", ACTION_TRANSITION, true},
    {"spawn:", ACTION_SPAWN, true},
    {"camera_pan:", ACTION_CAMERA_PAN, true},
    {"camera_shake:", ACTION_CAMERA_SHAKE, true},
    {"call:", ACTION_CALL, true},
    {"set_var:", ACTION_SET_VAR, true},
    {"wait:", ACTION_WAIT, true},
    {"create_timer_periodic:", ACTION_CREATE_TIMER_PERIODIC, true},
    {"create_timer:", ACTION_CREATE_TIMER, true},
    {"destroy_timer:", ACTION_DESTROY_TIMER, true},
    {nullptr, 0, false},
};

Strv action_type_label(ActionType type)
{
    for (int index = 0; action_mappings[index].prefix; index++) {
        if (action_mappings[index].type != type) {
            continue;
        }
        Strv label = strv_from_cstr(action_mappings[index].prefix);
        if (label.len > 0 && label.ptr[label.len - 1] == ':') {
            label.len--;
        }
        return label;
    }
    return (Strv){0};
}

static bool parse_simple_action(Diag *diag, Allocator *alloc, ActionNode *node, const char *string)
{
    memset(node, 0, sizeof(*node));
    Strv strv = strv_from_cstr(string);

    for (int index = 0; action_mappings[index].prefix; index++) {
        const ActionMapping *mapping = &action_mappings[index];
        if (!mapping->has_args) {
            if (strv_eq_cstr(strv, mapping->prefix)) {
                node->type = mapping->type;
                return true;
            }
            continue;
        }
        if (strv_starts_with_cstr(strv, mapping->prefix)) {
            node->type = mapping->type;
            Strv rest = strv;
            (void)strv_split(&rest, ':');
            return parse_action_two_args(alloc, node, rest);
        }
    }

    error_set(diag->error, "unknown action: '%s'", string);
    return false;
}

typedef struct {
    int target_index; /* index into pool of the node to fill in */
    toml_table_t *table;
} ParseCFTask;

/* Parses `array` into fresh pool nodes and appends their indices to
 * parent_index's children (or else_children) list. Each child is pushed onto
 * the pool as a zeroed placeholder first (capturing its stable index), then
 * filled in — either directly (simple actions) or deferred via task_stack
 * (nested control flow, drained by the caller). Every pool dereference is
 * re-derived from parent_index/node_index right before use, since any
 * vec_action_node_push above can reallocate the pool and invalidate a
 * previously taken ActionNode pointer (see CLAUDE.md "Vec growth and pointer
 * stability"). */
static bool parse_branch_array_into(Diag *diag,
                                    Allocator *alloc,
                                    vec_action_node *pool,
                                    int parent_index,
                                    bool into_else,
                                    toml_array_t *array,
                                    Arena *arena,
                                    const char *branch_name,
                                    ParseCFTask *task_stack,
                                    int *task_top)
{
    (void)arena;
    if (!array) {
        return true;
    }
    int count = toml_array_nelem(array);
    if (count <= 0) {
        return true;
    }
    for (int child_index = 0; child_index < count; child_index++) {
        ActionNode blank = {0};
        if (!vec_action_node_push(pool, blank)) {
            error_set(diag->error, "%s[%d]: out of memory", branch_name, child_index);
            return false;
        }
        int node_index = pool->count - 1;

        toml_datum_t value = toml_string_at(array, child_index);
        if (value.ok) {
            if (!action_node_parse(diag, alloc, &pool->data[node_index], value)) {
                error_wrap(diag->error, "%s[%d]", branch_name, child_index);
                free(value.u.s);
                return false;
            }
            free(value.u.s);
        } else {
            toml_table_t *table = toml_table_at(array, child_index);
            if (!table) {
                error_set(diag->error, "%s[%d] is not a string or table", branch_name, child_index);
                return false;
            }
            if (*task_top >= MAX_PARSE_CF_STACK) {
                error_set(diag->error, "parse_branch_array_into: task stack overflow");
                return false;
            }
            task_stack[(*task_top)++] = (ParseCFTask){node_index, table};
        }

        ActionNode *parent = &pool->data[parent_index];
        vec_int *target = into_else ? &parent->else_children : &parent->children;
        target->alloc = *alloc;
        if (!vec_int_push(target, node_index)) {
            error_set(diag->error, "%s[%d]: out of memory", branch_name, child_index);
            return false;
        }
    }
    return true;
}

static bool
parse_conditions_into(Diag *diag, Allocator *alloc, vec_condition *out, toml_array_t *array, const char *context_name)
{
    if (!array) {
        return true;
    }
    int count = toml_array_nelem(array);
    if (count > MAX_CONDITIONS) {
        error_set(diag->error, "%s: too many conditions (%d, max %d)", context_name, count, MAX_CONDITIONS);
        return false;
    }
    out->alloc = *alloc;
    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(array, index);
        if (!value.ok) {
            error_set(diag->error, "%s: condition[%d] is not a string", context_name, index);
            return false;
        }
        Condition cond = {0};
        if (!condition_parse(diag, alloc, &cond, value.u.s)) {
            free(value.u.s);
            error_wrap(diag->error, "%s: condition[%d]", context_name, index);
            return false;
        }
        free(value.u.s);
        if (!vec_condition_push(out, cond)) {
            error_set(diag->error, "%s: condition[%d]: out of memory", context_name, index);
            return false;
        }
    }
    return true;
}

/* Fills in pool->data[node_index] — already a zeroed placeholder pushed by the
 * caller. Every field write is a fresh pool->data[node_index] dereference so
 * that a pool reallocation triggered by a nested parse_branch_array_into call
 * (e.g. parsing "then" before "else") never leaves a stale pointer in play. */
static bool parse_one_cf_node(Diag *diag,
                              Allocator *alloc,
                              vec_action_node *pool,
                              int node_index,
                              toml_table_t *table,
                              Arena *arena,
                              ParseCFTask *task_stack,
                              int *task_top)
{
    toml_array_t *if_conds = toml_array_in(table, "if");
    if (if_conds) {
        pool->data[node_index].type = ACTION_IF_ELSE;
        if (!parse_conditions_into(diag, alloc, &pool->data[node_index].conditions, if_conds, "if")) {
            return false;
        }
        if (!parse_branch_array_into(diag, alloc, pool, node_index, false, toml_array_in(table, "then"), arena, "then",
                                     task_stack, task_top)) {
            return false;
        }
        return parse_branch_array_into(diag, alloc, pool, node_index, true, toml_array_in(table, "else"), arena, "else",
                                       task_stack, task_top);
    }

    toml_datum_t repeat_str = toml_string_in(table, "repeat");
    if (repeat_str.ok) {
        pool->data[node_index].type = ACTION_REPEAT;
        pool->data[node_index].argument = str_new(*alloc);
        bool alloc_ok = str_from_cstr(&pool->data[node_index].argument, repeat_str.u.s);
        free(repeat_str.u.s);
        if (!alloc_ok) {
            error_set(diag->error, "parse_one_cf_node: allocation failed");
            return false;
        }
        return parse_branch_array_into(diag, alloc, pool, node_index, false, toml_array_in(table, "do"), arena, "do",
                                       task_stack, task_top);
    }

    toml_datum_t for_each_str = toml_string_in(table, "for_each");
    if (for_each_str.ok) {
        pool->data[node_index].type = ACTION_FOR_EACH;
        pool->data[node_index].argument = str_new(*alloc);
        bool alloc_ok = str_from_cstr(&pool->data[node_index].argument, for_each_str.u.s);
        free(for_each_str.u.s);
        if (!alloc_ok) {
            error_set(diag->error, "parse_one_cf_node: allocation failed for for_each");
            return false;
        }
        if (strcmp(pool->data[node_index].argument.ptr, "entities") != 0) {
            error_set(diag->error, "for_each: unknown collection '%s' (supported: 'entities')",
                      pool->data[node_index].argument.ptr);
            return false;
        }
        toml_datum_t bind_str = toml_string_in(table, "bind");
        if (bind_str.ok) {
            pool->data[node_index].second_argument = str_new(*alloc);
            bool bind_ok = str_from_cstr(&pool->data[node_index].second_argument, bind_str.u.s);
            free(bind_str.u.s);
            if (!bind_ok) {
                error_set(diag->error, "parse_one_cf_node: allocation failed for bind");
                return false;
            }
        }
        if (!parse_conditions_into(diag, alloc, &pool->data[node_index].conditions, toml_array_in(table, "conditions"),
                                   "for_each")) {
            return false;
        }
        return parse_branch_array_into(diag, alloc, pool, node_index, false, toml_array_in(table, "do"), arena, "do",
                                       task_stack, task_top);
    }

    error_set(diag->error, "unknown control flow structure");
    return false;
}

static bool parse_control_flow_node(
    Diag *diag, Allocator *alloc, vec_action_node *pool, int node_index, toml_table_t *table, Arena *arena)
{
    ParseCFTask task_stack[MAX_PARSE_CF_STACK];
    int task_top = 0;

    if (!parse_one_cf_node(diag, alloc, pool, node_index, table, arena, task_stack, &task_top)) {
        return false;
    }
    while (task_top > 0) {
        ParseCFTask task = task_stack[--task_top];
        if (!parse_one_cf_node(diag, alloc, pool, task.target_index, task.table, arena, task_stack, &task_top)) {
            return false;
        }
    }
    return true;
}

bool action_node_parse(Diag *diag, Allocator *alloc, ActionNode *node, toml_datum_t value)
{
    if (!value.ok || !value.u.s) {
        error_set(diag->error, "action_node_parse: expected string value");
        return false;
    }
    return parse_simple_action(diag, alloc, node, value.u.s);
}

/* ---- TOML rule parsing ---- */

static bool parse_conditions_array(Diag *diag, Allocator *alloc, Rule *rule, toml_array_t *conditions)
{
    return parse_conditions_into(diag, alloc, &rule->conditions, conditions, "rule");
}

/* Parses a top-level actions array into tree->nodes (the tree's flat node
 * pool) and tree->roots (the indices of these top-level entries, in order).
 * Nested control flow grows the same pool — see parse_branch_array_into. */
static bool parse_action_nodes_into(Diag *diag, Allocator *alloc, ActionTree *tree, toml_array_t *actions, Arena *arena)
{
    if (!actions) {
        return true;
    }
    int count = toml_array_nelem(actions);
    if (count > MAX_ACTIONS) {
        error_set(diag->error, "too many actions (%d, max %d)", count, MAX_ACTIONS);
        return false;
    }
    tree->nodes.alloc = *alloc;
    tree->roots.alloc = *alloc;

    for (int index = 0; index < count; index++) {
        ActionNode blank = {0};
        if (!vec_action_node_push(&tree->nodes, blank)) {
            error_set(diag->error, "action[%d]: out of memory", index);
            return false;
        }
        int node_index = tree->nodes.count - 1;

        toml_datum_t value = toml_string_at(actions, index);
        if (value.ok) {
            if (!action_node_parse(diag, alloc, &tree->nodes.data[node_index], value)) {
                error_wrap(diag->error, "action[%d]", index);
                free(value.u.s);
                return false;
            }
            free(value.u.s);
        } else {
            toml_table_t *table_value = toml_table_at(actions, index);
            if (table_value) {
                if (!parse_control_flow_node(diag, alloc, &tree->nodes, node_index, table_value, arena)) {
                    error_wrap(diag->error, "action[%d]", index);
                    return false;
                }
            } else {
                error_set(diag->error, "action[%d] is not a string or table", index);
                return false;
            }
        }
        if (!vec_int_push(&tree->roots, node_index)) {
            error_set(diag->error, "action[%d]: out of memory", index);
            return false;
        }
    }
    return true;
}

static bool parse_actions_array(Diag *diag, Allocator *alloc, Rule *rule, toml_array_t *actions, Arena *arena)
{
    return parse_action_nodes_into(diag, alloc, &rule->action_tree, actions, arena);
}

static bool parse_single_rule(Diag *diag, Allocator *alloc, Rule *rule, toml_table_t *entry, Arena *arena)
{
    memset(rule, 0, sizeof(*rule));

    toml_datum_t trigger_str = toml_string_in(entry, "trigger");
    if (!trigger_str.ok) {
        error_set(diag->error, "rule missing 'trigger' key");
        return false;
    }
    if (!trigger_parse(diag, alloc, &rule->trigger, trigger_str.u.s)) {
        error_wrap(diag->error, "rule trigger");
        free(trigger_str.u.s);
        return false;
    }
    free(trigger_str.u.s);

    toml_array_t *conditions = toml_array_in(entry, "conditions");
    if (!parse_conditions_array(diag, alloc, rule, conditions)) {
        error_wrap(diag->error, "rule conditions");
        return false;
    }

    toml_array_t *actions = toml_array_in(entry, "actions");
    if (!parse_actions_array(diag, alloc, rule, actions, arena)) {
        vec_condition_free(&rule->conditions);
        error_wrap(diag->error, "rule actions");
        return false;
    }

    return true;
}

bool rules_parse(Diag *diag, Allocator *alloc, vec_rule *rules, toml_table_t *toml_blueprint_table, Arena *arena)
{
    *rules = (vec_rule){0};
    rules->alloc = *alloc;

    toml_array_t *rule_array = toml_array_in(toml_blueprint_table, "rule");
    if (!rule_array) {
        return true;
    }

    int count = toml_array_nelem(rule_array);
    if (count == 0) {
        return true;
    }
    if (count > MAX_RULES) {
        error_set(diag->error, "too many rules (%d, max %d)", count, MAX_RULES);
        return false;
    }

    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(rule_array, index);
        if (!entry) {
            error_set(diag->error, "rule[%d]: toml_table_at returned nullptr", index);
            return false;
        }
        Rule rule = {0};
        if (!parse_single_rule(diag, alloc, &rule, entry, arena)) {
            vec_condition_free(&rule.conditions);
            vec_action_node_free(&rule.action_tree.nodes);
            vec_int_free(&rule.action_tree.roots);
            error_wrap(diag->error, "rule[%d]", index);
            return false;
        }
        if (!vec_rule_push(rules, rule)) {
            vec_condition_free(&rule.conditions);
            vec_action_node_free(&rule.action_tree.nodes);
            vec_int_free(&rule.action_tree.roots);
            error_set(diag->error, "rule[%d]: out of memory", index);
            return false;
        }
    }

    return true;
}

/* ---- Trigger matching ---- */

bool trigger_matches(const Trigger *trigger, const TriggerEvent *event)
{
    if (trigger->type != event->type) {
        return false;
    }
    if (trigger->type == TRIGGER_EVENT || trigger->type == TRIGGER_ATTR_CHANGED || trigger->type == TRIGGER_TIMER ||
        trigger->type == TRIGGER_TIMER_PERIODIC) {
        return strcmp(trigger->argument.ptr, event->argument.ptr) == 0;
    }
    if (trigger->type == TRIGGER_COLLIDE && trigger->argument.len > 0) {
        return event->argument.ptr != nullptr && strcmp(trigger->argument.ptr, event->argument.ptr) == 0;
    }
    return true;
}

/* ---- Condition evaluation ---- */

static float attr_to_float(const Attribute *attr)
{
    if (attr->type == ATTR_FLOAT) {
        return attr->value.f;
    }
    if (attr->type == ATTR_INT) {
        return (float)attr->value.i;
    }
    if (attr->type == ATTR_BOOL) {
        if (attr->value.b) {
            return 1.0F;
        }
        return 0.0F;
    }
    return 0.0F;
}

static bool attr_is_truthy(const Attribute *attr)
{
    if (attr->type == ATTR_BOOL) {
        return attr->value.b;
    }
    if (attr->type == ATTR_INT) {
        return attr->value.i != 0;
    }
    if (attr->type == ATTR_FLOAT) {
        return attr->value.f != 0.0F;
    }
    if (attr->type == ATTR_STRING) {
        return attr->value.str.len > 0;
    }
    return false;
}

static bool evaluate_single_condition(const Condition *condition, ConditionContext context)
{
    switch (condition->type) {
    case COND_FLAG:
        return flag_get(context.flags, condition->argument.ptr);
    case COND_NOT_FLAG:
        return !flag_get(context.flags, condition->argument.ptr);
    case COND_ATTR: {
        const AttrSet *defaults = context.views[context.entity_index].defaults;
        const Attribute *attr = attr_get_scoped(&context.entity->attrs, defaults, condition->argument.ptr);
        if (!attr) {
            return false;
        }
        return attr_is_truthy(attr);
    }
    case COND_NOT_ATTR: {
        const AttrSet *defaults = context.views[context.entity_index].defaults;
        const Attribute *attr = attr_get_scoped(&context.entity->attrs, defaults, condition->argument.ptr);
        if (!attr) {
            return true;
        }
        return !attr_is_truthy(attr);
    }
    case COND_ATTR_LT: {
        const AttrSet *defaults = context.views[context.entity_index].defaults;
        const Attribute *attr = attr_get_scoped(&context.entity->attrs, defaults, condition->argument.ptr);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) < condition->compare_value;
    }
    case COND_ATTR_GT: {
        const AttrSet *defaults = context.views[context.entity_index].defaults;
        const Attribute *attr = attr_get_scoped(&context.entity->attrs, defaults, condition->argument.ptr);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) > condition->compare_value;
    }
    case COND_ATTR_EQ: {
        const AttrSet *defaults = context.views[context.entity_index].defaults;
        const Attribute *attr = attr_get_scoped(&context.entity->attrs, defaults, condition->argument.ptr);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) == condition->compare_value;
    }
    case COND_HAS_ITEM:
        return true;
    case COND_VAR: {
        const Attribute *var = nullptr;
        if (context.local_vars) {
            var = attr_get(context.local_vars, condition->argument.ptr);
        }
        if (!var && context.global_vars) {
            var = attr_get(context.global_vars, condition->argument.ptr);
        }
        if (!var) {
            return false;
        }
        if (var->type == ATTR_STRING) {
            return var->value.str.len > 0;
        }
        return attr_to_float(var) != 0.0F;
    }
    }
    return false;
}

bool conditions_evaluate(const Condition *conditions, int count, ConditionContext context)
{
    for (int index = 0; index < count; index++) {
        if (!evaluate_single_condition(&conditions[index], context)) {
            return false;
        }
    }
    return true;
}

/* ---- Action execution ---- */

/* Returns a view index into context.views, or -1 if the target could not be resolved. */
static int resolve_target(const char *target_spec, ActionContext context, char *attr_name_out, int attr_name_max)
{
    Strv strv = strv_from_cstr(target_spec);
    Strv head = strv_split(&strv, '.');
    if (!strv.ptr) {
        strv_copy_to_cstr(head, attr_name_out, (size_t)attr_name_max);
        return context.entity_index;
    }

    char tag[MAX_ARG];
    strv_copy_to_cstr(head, tag, MAX_ARG);
    strv_copy_to_cstr(strv, attr_name_out, (size_t)attr_name_max);

    if (context.view_count > 0) {
        /* Invariant: game.c builds views[i].entity == &entities[i] over one contiguous
         * entity subarray, so views[i].entity == views[0].entity + i. This lets us
         * translate the Entity* returned by entity_find_by_tag_mut back into a view
         * index by subtracting the base of that same contiguous array. */
        Entity *base = context.views[0].entity;
        Entity *tagged = entity_find_by_tag_mut(context.entity, tag, base, context.view_count);
        if (tagged) {
            return (int)(tagged - base);
        }
    }

    /* Fallback: walk the LocalScope chain for entity index bindings */
    for (const LocalScope *scope = context.scope; scope; scope = scope->outer) {
        if (strv_eq_cstr(str_to_strv(scope->bind_name), tag)) {
            int idx = scope->entity_index;
            if (idx >= 0 && idx < context.view_count) {
                return idx;
            }
        }
    }
    return -1;
}

static const Attribute *lookup_var(const char *varname, ActionContext context)
{
    if (context.local_vars) {
        const Attribute *var = attr_get(context.local_vars, varname);
        if (var) {
            return var;
        }
    }
    if (context.global_vars) {
        return attr_get(context.global_vars, varname);
    }
    return nullptr;
}

static void format_var_value(char *out, int out_size, const Attribute *var)
{
    if (var->type == ATTR_FLOAT) {
        (void)snprintf(out, (size_t)out_size, "%g", (double)var->value.f);
    } else if (var->type == ATTR_INT) {
        (void)snprintf(out, (size_t)out_size, "%d", var->value.i);
    } else if (var->type == ATTR_BOOL) {
        if (var->value.b) {
            strncpy(out, "true", (size_t)(out_size - 1));
        } else {
            strncpy(out, "false", (size_t)(out_size - 1));
        }
        out[out_size - 1] = '\0';
    } else {
        strncpy(out, var->value.str.ptr, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
    }
}

static void resolve_arg(char *out, int out_size, const char *arg, ActionContext context)
{
    if (!arg) {
        out[0] = '\0';
        return;
    }
    if (!strchr(arg, '$')) {
        strncpy(out, arg, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
        return;
    }
    int out_pos = 0;
    const char *pos = arg;
    while (*pos != '\0' && out_pos < out_size - 1) {
        if (*pos != '$') {
            out[out_pos++] = *pos++;
            continue;
        }
        pos++;
        char varname[MAX_ARG];
        int varname_len = 0;
        while (*pos != '\0' && (isalnum((unsigned char)*pos) || *pos == '_')) {
            if (varname_len < MAX_ARG - 1) {
                varname[varname_len++] = *pos;
            }
            pos++;
        }
        varname[varname_len] = '\0';
        if (varname_len == 0) {
            out[out_pos++] = '$';
            continue;
        }
        const Attribute *var = lookup_var(varname, context);
        if (!var) {
            continue;
        }
        char val_buf[MAX_ARG];
        format_var_value(val_buf, MAX_ARG, var);
        int val_len = (int)strlen(val_buf);
        int copy_len = val_len < out_size - 1 - out_pos ? val_len : out_size - 1 - out_pos;
        memcpy(out + out_pos, val_buf, (size_t)copy_len);
        out_pos += copy_len;
    }
    out[out_pos] = '\0';
}

static bool execute_set_attr_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ARG];
    int view_index = resolve_target(node->argument.ptr, context, attr_name, MAX_ARG);
    if (view_index < 0) {
        debug_log(diag->debug, "set_attr: target not found: %s", node->argument.ptr);
        return true;
    }
    Entity *target = context.views[view_index].entity;
    const AttrSet *target_defaults = context.views[view_index].defaults;
    float old_health = strcmp(attr_name, "health") == 0
                           ? attr_get_scoped_float(&target->attrs, target_defaults, "health", 0.0F)
                           : 0.0F;
    char resolved[MAX_ARG];
    resolve_arg(resolved, MAX_ARG, node->second_argument.ptr, context);
    float value = strtof(resolved, nullptr);
    bool attr_set_ok;
    if (strchr(resolved, '.')) {
        attr_set_ok = attr_set_float(alloc, &target->attrs, attr_name, value);
    } else if (strcmp(resolved, "true") == 0) {
        attr_set_ok = attr_set_bool(alloc, &target->attrs, attr_name, true);
    } else if (strcmp(resolved, "false") == 0) {
        attr_set_ok = attr_set_bool(alloc, &target->attrs, attr_name, false);
    } else {
        attr_set_ok = attr_set_int(alloc, &target->attrs, attr_name, (int)value);
    }
    if (!attr_set_ok) {
        return false;
    }
    if (strcmp(attr_name, "health") == 0 && old_health > 0.0F &&
        attr_get_scoped_float(&target->attrs, target_defaults, "health", 0.0F) <= 0.0F) {
        TriggerEvent defeat = {.type = TRIGGER_DEFEAT, .entity_index = view_index};
        (void)vec_trigger_event_push(context.event_queue, defeat);
    }
    return true;
}

static bool execute_add_attr_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ARG];
    int view_index = resolve_target(node->argument.ptr, context, attr_name, MAX_ARG);
    if (view_index < 0) {
        debug_log(diag->debug, "add_attr: target not found: %s", node->argument.ptr);
        return true;
    }
    Entity *target = context.views[view_index].entity;
    const AttrSet *target_defaults = context.views[view_index].defaults;
    const Attribute *existing = attr_get_scoped(&target->attrs, target_defaults, attr_name);
    float old_health = strcmp(attr_name, "health") == 0
                           ? attr_get_scoped_float(&target->attrs, target_defaults, "health", 0.0F)
                           : 0.0F;
    char resolved[MAX_ARG];
    resolve_arg(resolved, MAX_ARG, node->second_argument.ptr, context);
    float delta = strtof(resolved, nullptr);
    bool attr_set_ok;
    if (existing && existing->type == ATTR_FLOAT) {
        attr_set_ok = attr_set_float(alloc, &target->attrs, attr_name, existing->value.f + delta);
    } else {
        int current_val = existing ? existing->value.i : 0;
        attr_set_ok = attr_set_int(alloc, &target->attrs, attr_name, current_val + (int)delta);
    }
    if (!attr_set_ok) {
        return false;
    }
    if (strcmp(attr_name, "health") == 0 && old_health > 0.0F &&
        attr_get_scoped_float(&target->attrs, target_defaults, "health", 0.0F) <= 0.0F) {
        TriggerEvent defeat = {.type = TRIGGER_DEFEAT, .entity_index = view_index};
        (void)vec_trigger_event_push(context.event_queue, defeat);
    }
    return true;
}

static bool execute_toggle_attr_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ARG];
    int view_index = resolve_target(node->argument.ptr, context, attr_name, MAX_ARG);
    if (view_index < 0) {
        debug_log(diag->debug, "toggle_attr: target not found: %s", node->argument.ptr);
        return true;
    }
    Entity *target = context.views[view_index].entity;
    const AttrSet *target_defaults = context.views[view_index].defaults;
    bool current_val = attr_get_scoped_bool(&target->attrs, target_defaults, attr_name, false);
    return attr_set_bool(alloc, &target->attrs, attr_name, !current_val);
}

static bool execute_set_var_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    char resolved_value[MAX_ARG];
    resolve_arg(resolved_value, MAX_ARG, node->second_argument.ptr, context);

    AttrSet *target_vars;
    Allocator *var_alloc;
    const char *var_name;
    if (strv_starts_with_cstr(str_to_strv(node->argument), GLOBAL_VAR_PREFIX)) {
        target_vars = context.global_vars;
        var_alloc = context.progression_alloc;
        var_name = node->argument.ptr + strlen(GLOBAL_VAR_PREFIX);
    } else {
        target_vars = context.local_vars;
        var_alloc = alloc;
        var_name = node->argument.ptr;
    }
    if (!target_vars || !var_alloc) {
        debug_log(diag->debug, "set_var: no var storage for '%s'", node->argument.ptr);
        return true;
    }
    if (strcmp(resolved_value, "true") == 0) {
        return attr_set_bool(var_alloc, target_vars, var_name, true);
    }
    if (strcmp(resolved_value, "false") == 0) {
        return attr_set_bool(var_alloc, target_vars, var_name, false);
    }
    if (strchr(resolved_value, '.')) {
        return attr_set_float(var_alloc, target_vars, var_name, strtof(resolved_value, nullptr));
    }
    char *endptr;
    long ival = strtol(resolved_value, &endptr, RADIX_DECIMAL);
    if (endptr != resolved_value && *endptr == '\0') {
        return attr_set_int(var_alloc, target_vars, var_name, (int)ival);
    }
    return attr_set_string(var_alloc, target_vars, (AttrStringPair){.name = var_name, .value = resolved_value});
}

/* Resolves each index in `indices` against `pool` and pushes the resulting
 * node pointers onto exec_stack. Safe to hold these ActionNode pointers past
 * this call because the pool is read-only at runtime — all pushes happen at
 * parse time. */
static bool push_branch_nodes(
    Diag *diag, const vec_action_node *pool, const ActionNode **exec_stack, int *stack_top, const vec_int *indices)
{
    if (!pool) {
        error_set(diag->error, "push_branch_nodes: control flow node reached with no context.action_pool set");
        return false;
    }
    for (int push_index = indices->count - 1; push_index >= 0; push_index--) {
        if (*stack_top >= MAX_EXEC_STACK) {
            error_set(diag->error, "action execution stack overflow");
            return false;
        }
        exec_stack[(*stack_top)++] = &pool->data[indices->data[push_index]];
    }
    return true;
}

static bool expand_if_else_node(
    Diag *diag, const ActionNode *node, ActionContext context, const ActionNode **exec_stack, int *stack_top)
{
    ConditionContext cond_ctx = {
        .entity = context.entity,
        .entity_index = context.entity_index,
        .views = context.views,
        .view_count = context.view_count,
        .flags = context.flags,
        .local_vars = context.local_vars,
        .global_vars = context.global_vars,
    };
    bool condition_met = conditions_evaluate(node->conditions.data, node->conditions.count, cond_ctx);
    const vec_int *branch = condition_met ? &node->children : &node->else_children;
    return push_branch_nodes(diag, context.action_pool, exec_stack, stack_top, branch);
}

static bool expand_repeat_node(
    Diag *diag, const ActionNode *node, const vec_action_node *pool, const ActionNode **exec_stack, int *stack_top)
{
    int repeat_count = (int)strtol(node->argument.ptr, nullptr, RADIX_DECIMAL);
    if (repeat_count <= 0) {
        repeat_count = 1;
    }
    for (int repeat_index = 0; repeat_index < repeat_count; repeat_index++) {
        if (!push_branch_nodes(diag, pool, exec_stack, stack_top, &node->children)) {
            return false;
        }
    }
    return true;
}

static bool execute_create_timer_action(Diag *diag, const ActionNode *node, ActionContext context, bool periodic)
{
    if (!context.timers) {
        debug_log(diag->debug, "create_timer: no timer list in context");
        return true;
    }
    float duration = node->second_argument.len > 0 ? strtof(node->second_argument.ptr, nullptr) : 1.0F;
    /* Replace existing timer with same name + entity, or append */
    for (int timer_index = 0; timer_index < context.timers->count; timer_index++) {
        Timer *existing = &context.timers->data[timer_index];
        if (existing->entity_index == context.entity_index && strcmp(existing->name.ptr, node->argument.ptr) == 0) {
            existing->remaining = duration;
            existing->duration = duration;
            existing->periodic = periodic;
            return true;
        }
    }
    Timer new_timer = {
        .name = node->argument,
        .entity_index = context.entity_index,
        .remaining = duration,
        .duration = duration,
        .periodic = periodic,
    };
    if (!vec_timer_push(context.timers, new_timer)) {
        error_set(diag->error, "create_timer: allocation failed");
        return false;
    }
    return true;
}

static bool execute_destroy_timer_action(Diag *diag, const ActionNode *node, ActionContext context)
{
    if (!context.timers) {
        debug_log(diag->debug, "destroy_timer: no timer list in context");
        return true;
    }
    for (int timer_index = 0; timer_index < context.timers->count; timer_index++) {
        Timer *timer = &context.timers->data[timer_index];
        if (timer->entity_index == context.entity_index && strcmp(timer->name.ptr, node->argument.ptr) == 0) {
            context.timers->data[timer_index] = context.timers->data[context.timers->count - 1];
            context.timers->count--;
            return true;
        }
    }
    return true;
}

static bool execute_transition_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    if (!context.transition) {
        error_set(diag->error, "transition action: no transition context");
        return false;
    }
    /* Action parser splits "level,x,y" into argument="level" and second_argument="x,y" */
    if (!node->argument.ptr || !node->second_argument.ptr) {
        error_set(diag->error, "transition: expected 'level,x,y'");
        return false;
    }
    context.transition->level = str_new(*alloc);
    if (!str_from_strv(&context.transition->level, str_to_strv(node->argument))) {
        error_set(diag->error, "transition: allocation failed for level name");
        return false;
    }
    const char *coords = node->second_argument.ptr;
    const char *comma = strchr(coords, ',');
    if (!comma) {
        error_set(diag->error, "transition: expected 'x,y' in second argument, got '%s'", coords);
        return false;
    }
    context.transition->x = strtof(coords, nullptr);
    context.transition->y = strtof(comma + 1, nullptr);
    context.transition->pending = true;
    return true;
}

static bool dispatch_simple_action(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    switch (node->type) {
    case ACTION_SET_FLAG:
        if (!context.progression_alloc || !context.flags) {
            debug_log(diag->debug, "set_flag: no progression storage for '%s'", node->argument.ptr);
            return true;
        }
        flag_set(diag, context.progression_alloc, context.flags, node->argument.ptr);
        return true;
    case ACTION_CLEAR_FLAG:
        if (!context.progression_alloc || !context.flags) {
            debug_log(diag->debug, "clear_flag: no progression storage for '%s'", node->argument.ptr);
            return true;
        }
        flag_clear(context.progression_alloc, context.flags, node->argument.ptr);
        return true;
    case ACTION_SET_ATTR:
        return execute_set_attr_action(diag, alloc, node, context);
    case ACTION_ADD_ATTR:
        return execute_add_attr_action(diag, alloc, node, context);
    case ACTION_TOGGLE_ATTR:
        return execute_toggle_attr_action(diag, alloc, node, context);
    case ACTION_SET_VAR:
        return execute_set_var_action(diag, alloc, node, context);
    case ACTION_DESTROY: {
        TriggerEvent destroy_event = {.type = TRIGGER_ON_DESTROY, .entity_index = context.entity_index};
        (void)vec_trigger_event_push(context.event_queue, destroy_event);
        (void)attr_set_bool(alloc, &context.entity->attrs, "active", false);
        return true;
    }
    case ACTION_FIRE_EVENT: {
        TriggerEvent fire = {.type = TRIGGER_EVENT, .entity_index = -1, .argument = node->argument};
        return vec_trigger_event_push(context.event_queue, fire);
    }
    case ACTION_CREATE_TIMER:
        return execute_create_timer_action(diag, node, context, false);
    case ACTION_CREATE_TIMER_PERIODIC:
        return execute_create_timer_action(diag, node, context, true);
    case ACTION_DESTROY_TIMER:
        return execute_destroy_timer_action(diag, node, context);
    case ACTION_TRANSITION:
        return execute_transition_action(diag, alloc, node, context);
    default:
        debug_log(diag->debug, "action stub: %s (not yet implemented)", node->argument.ptr);
        return true;
    }
}

/* Forward declarations — execute_from_stack, execute_action_nodes and
 * execute_for_each_node/execute_call_node are mutually recursive. */
static bool execute_action_nodes(Diag *diag, Allocator *alloc, const vec_int *node_indices, ActionContext context);
static bool
execute_from_stack(Diag *diag, Allocator *alloc, ActionContext context, const ActionNode **exec_stack, int stack_top);

// NOLINTBEGIN(misc-no-recursion) -- mutually recursive; bounded by for_each nesting depth
static bool execute_for_each_node(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    bool has_bind = node->second_argument.len > 0;
    for (int entity_index = 0; entity_index < context.view_count; entity_index++) {
        if (!attr_get_scoped_bool(&context.views[entity_index].entity->attrs, context.views[entity_index].defaults,
                                  "active", true)) {
            continue;
        }
        ConditionContext cond_ctx = {
            .entity = context.views[entity_index].entity,
            .entity_index = entity_index,
            .views = context.views,
            .view_count = context.view_count,
            .flags = context.flags,
            .local_vars = context.local_vars,
            .global_vars = context.global_vars,
        };
        if (!conditions_evaluate(node->conditions.data, node->conditions.count, cond_ctx)) {
            continue;
        }
        if (has_bind) {
            /* Bind mode: self = rule owner; iterated entity accessible via bind name */
            LocalScope iter_scope = {
                .bind_name = node->second_argument,
                .entity_index = entity_index,
                .outer = context.scope,
            };
            ActionContext iter_ctx = context;
            iter_ctx.scope = &iter_scope;
            if (!execute_action_nodes(diag, alloc, &node->children, iter_ctx)) {
                return false;
            }
        } else {
            /* Simple mode: self = iterated entity */
            ActionContext iter_ctx = context;
            iter_ctx.entity = context.views[entity_index].entity;
            iter_ctx.entity_index = entity_index;
            if (!execute_action_nodes(diag, alloc, &node->children, iter_ctx)) {
                return false;
            }
        }
    }
    return true;
}

static bool execute_call_node(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    if (context.call_depth >= MAX_CALL_DEPTH) {
        error_set(diag->error, "call: max depth %d exceeded", MAX_CALL_DEPTH);
        return false;
    }
    if (!context.subroutines) {
        debug_log(diag->debug, "call: no subroutine table");
        return true;
    }
    for (int sub_index = 0; sub_index < context.subroutines->count; sub_index++) {
        const Subroutine *sub = &context.subroutines->data[sub_index];
        if (strcmp(sub->name.ptr, node->argument.ptr) == 0) {
            ActionContext sub_ctx = context;
            sub_ctx.call_depth++;
            sub_ctx.action_pool = &sub->action_tree.nodes;
            return execute_action_nodes(diag, alloc, &sub->action_tree.roots, sub_ctx);
        }
    }
    debug_log(diag->debug, "call: subroutine '%s' not found", node->argument.ptr);
    return true;
}

static bool
execute_from_stack(Diag *diag, Allocator *alloc, ActionContext context, const ActionNode **exec_stack, int stack_top)
{
    while (stack_top > 0) {
        const ActionNode *current = exec_stack[--stack_top];
        if (current->type == ACTION_IF_ELSE) {
            if (!expand_if_else_node(diag, current, context, exec_stack, &stack_top)) {
                return false;
            }
        } else if (current->type == ACTION_REPEAT) {
            if (!expand_repeat_node(diag, current, context.action_pool, exec_stack, &stack_top)) {
                return false;
            }
        } else if (current->type == ACTION_FOR_EACH) {
            if (!execute_for_each_node(diag, alloc, current, context)) {
                return false;
            }
        } else if (current->type == ACTION_CALL) {
            if (!execute_call_node(diag, alloc, current, context)) {
                return false;
            }
        } else if (!dispatch_simple_action(diag, alloc, current, context)) {
            return false;
        }
    }
    return true;
}

static bool execute_action_nodes(Diag *diag, Allocator *alloc, const vec_int *node_indices, ActionContext context)
{
    const ActionNode *exec_stack[MAX_EXEC_STACK];
    int stack_top = 0;
    if (!push_branch_nodes(diag, context.action_pool, exec_stack, &stack_top, node_indices)) {
        return false;
    }
    return execute_from_stack(diag, alloc, context, exec_stack, stack_top);
}
// NOLINTEND(misc-no-recursion)

bool action_node_execute(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context)
{
    const ActionNode *exec_stack[MAX_EXEC_STACK];
    int stack_top = 0;
    exec_stack[stack_top++] = node;
    return execute_from_stack(diag, alloc, context, exec_stack, stack_top);
}

/* ---- Evaluation loop ---- */

static bool rule_triggered_by_events(const Rule *rule, int entity_index, const vec_trigger_event *pending_events)
{
    for (int pending_index = 0; pending_index < pending_events->count; pending_index++) {
        const TriggerEvent *pending = &pending_events->data[pending_index];
        if (pending->entity_index >= 0 && pending->entity_index != entity_index) {
            continue;
        }
        if (trigger_matches(&rule->trigger, pending)) {
            return true;
        }
    }
    return false;
}

static void evaluate_entity_rules(Diag *diag,
                                  Allocator *alloc,
                                  Entity *entity,
                                  int entity_index,
                                  EntityView *views,
                                  int view_count,
                                  FlagSet *flags,
                                  AttrSet *global_vars,
                                  Allocator *progression_alloc,
                                  const vec_trigger_event *pending_events,
                                  vec_trigger_event *next_events,
                                  map_entity_ruleset *rule_table,
                                  const vec_subroutine *subroutines,
                                  vec_timer *timers,
                                  TransitionRequest *transition,
                                  EffectQueue *effects)
{
    const vec_rule *ruleset = map_entity_ruleset_get(rule_table, entity->id);
    if (!ruleset) {
        return;
    }
    for (int rule_index = 0; rule_index < ruleset->count; rule_index++) {
        const Rule *rule = &ruleset->data[rule_index];
        if (!rule_triggered_by_events(rule, entity_index, pending_events)) {
            continue;
        }
        AttrSet local_vars = {0};
        ConditionContext cond_ctx = {
            .entity = entity,
            .entity_index = entity_index,
            .views = views,
            .view_count = view_count,
            .flags = flags,
            .local_vars = &local_vars,
            .global_vars = global_vars,
        };
        ActionContext act_ctx = {
            .entity = entity,
            .entity_index = entity_index,
            .views = views,
            .view_count = view_count,
            .flags = flags,
            .event_queue = next_events,
            .local_vars = &local_vars,
            .global_vars = global_vars,
            .progression_alloc = progression_alloc,
            .subroutines = subroutines,
            .timers = timers,
            .transition = transition,
            .effects = effects,
            .action_pool = &rule->action_tree.nodes,
        };
        debug_log(diag->debug, "Rule triggered for entity %d (type: %s), rule %d", entity_index,
                  entity->blueprint_name.ptr, rule_index);
        if (!conditions_evaluate(rule->conditions.data, rule->conditions.count, cond_ctx)) {
            debug_log(diag->debug, "Conditions not met for rule %d on entity %d (type: %s)", rule_index, entity_index,
                      entity->blueprint_name.ptr);
            continue;
        }
        debug_log(diag->debug, "Executing actions for rule %d on entity %d (type: %s)", rule_index, entity_index,
                  entity->blueprint_name.ptr);
        for (int root_index = 0; root_index < rule->action_tree.roots.count; root_index++) {
            int node_index = rule->action_tree.roots.data[root_index];
            (void)action_node_execute(diag, alloc, &rule->action_tree.nodes.data[node_index], act_ctx);
        }
        attr_set_free(alloc, &local_vars);
    }
}

void rules_evaluate_batch(Diag *diag,
                          Allocator *alloc,
                          EntityView *views,
                          int view_count,
                          const TriggerEvent *events,
                          int event_count,
                          FlagSet *flags,
                          AttrSet *global_vars,
                          Allocator *progression_alloc,
                          map_entity_ruleset *rule_table,
                          const vec_subroutine *subroutines,
                          vec_timer *timers,
                          Allocator *scratch_alloc,
                          TransitionRequest *transition,
                          EffectQueue *effects)
{
    vec_trigger_event pending_events = vec_trigger_event_new(*scratch_alloc);
    for (int event_index = 0; event_index < event_count; event_index++) {
        (void)vec_trigger_event_push(&pending_events, events[event_index]);
    }

    for (int cascade = 0; cascade < MAX_EVENT_CASCADES && pending_events.count > 0; cascade++) {
        vec_trigger_event next_events = vec_trigger_event_new(*scratch_alloc);

        for (int entity_index = 0; entity_index < view_count; entity_index++) {
            Entity *entity = views[entity_index].entity;
            if (!attr_get_scoped_bool(&entity->attrs, views[entity_index].defaults, "active", true)) {
                /* Inactive entities skip rule evaluation unless they have a pending
                 * on_destroy event — those rules must still fire. */
                bool has_on_destroy = false;
                for (int event_index = 0; event_index < pending_events.count; event_index++) {
                    if (pending_events.data[event_index].type == TRIGGER_ON_DESTROY &&
                        pending_events.data[event_index].entity_index == entity_index) {
                        has_on_destroy = true;
                        break;
                    }
                }
                if (!has_on_destroy) {
                    continue;
                }
            }
            evaluate_entity_rules(diag, alloc, entity, entity_index, views, view_count, flags, global_vars,
                                  progression_alloc, &pending_events, &next_events, rule_table, subroutines, timers,
                                  transition, effects);
        }

        pending_events = next_events;
    }
}

/* ---- Subroutine parsing ---- */

bool subroutines_parse(Diag *diag, Allocator *alloc, vec_subroutine *subroutines, toml_table_t *toml_root, Arena *arena)
{
    toml_array_t *sub_array = toml_array_in(toml_root, "subroutine");
    if (!sub_array) {
        return true; /* no subroutines — fine */
    }
    int count = toml_array_nelem(sub_array);
    for (int index = 0; index < count; index++) {
        toml_table_t *sub_table = toml_table_at(sub_array, index);
        if (!sub_table) {
            error_set(diag->error, "subroutine[%d]: expected table", index);
            return false;
        }
        toml_datum_t name_datum = toml_string_in(sub_table, "name");
        if (!name_datum.ok) {
            error_set(diag->error, "subroutine[%d]: missing 'name'", index);
            return false;
        }
        /* Push a name-only stub first so the action_tree lives inside the vec element
         * (reachable via subroutines->data), avoiding false-positive leak warnings from
         * the clang-analyzer about arena-backed allocations. */
        Subroutine stub = {0};
        stub.name = str_new(*alloc);
        bool alloc_ok = str_from_cstr(&stub.name, name_datum.u.s);
        free(name_datum.u.s);
        if (!alloc_ok) {
            error_set(diag->error, "subroutine[%d]: allocation failed for name", index);
            return false;
        }
        if (!vec_subroutine_push(subroutines, stub)) {
            error_set(diag->error, "subroutine[%d]: push failed", index);
            return false;
        }
        Subroutine *pushed = &subroutines->data[subroutines->count - 1];
        if (!parse_action_nodes_into(diag, alloc, &pushed->action_tree, toml_array_in(sub_table, "actions"), arena)) {
            error_wrap(diag->error, "subroutine '%s'", pushed->name.ptr);
            return false;
        }
        debug_log(diag->debug, "subroutine: parsed '%s' (%d actions)", pushed->name.ptr,
                  pushed->action_tree.roots.count);
    }
    return true;
}

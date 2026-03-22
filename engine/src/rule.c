#include "rule.h"
#include "vec.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "error.h"
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

VEC_IMPL(trigger_event, TriggerEvent)
VEC_IMPL(flag_name, FlagName)

/* ---- FlagSet ---- */

static int flag_find(const FlagSet *flags, const char *name)
{
    for (int index = 0; index < flags->names.count; index++) {
        if (strcmp(flags->names.data[index].value, name) == 0) {
            return index;
        }
    }
    return -1;
}

bool flag_get(const FlagSet *flags, const char *name)
{
    return flag_find(flags, name) >= 0;
}

void flag_set(struct EngineContext *ctx, FlagSet *flags, const char *name)
{
    if (flag_find(flags, name) >= 0) {
        return;
    }
    FlagName entry = {0};
    strv_copy_to_cstr(strv_from_cstr(name), entry.value, MAX_FLAG_NAME);
    if (!vec_flag_name_push(&flags->names, entry)) {
        debug_log(ctx, "flag_set: allocation failed for '%s'", name);
    }
}

void flag_clear(FlagSet *flags, const char *name)
{
    int index = flag_find(flags, name);
    if (index < 0) {
        return;
    }
    flags->names.count--;
    if (index < flags->names.count) {
        flags->names.data[index] = flags->names.data[flags->names.count];
    }
}

/* ---- Parsing helpers ---- */

bool trigger_parse(struct EngineContext *ctx, Trigger *trigger, const char *string)
{
    memset(trigger, 0, sizeof(*trigger));
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
        strv_copy_to_cstr(rest, trigger->argument, MAX_ARG);
        return true;
    }
    if (strv_starts_with_cstr(strv, "attr_changed:")) {
        trigger->type = TRIGGER_ATTR_CHANGED;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, trigger->argument, MAX_ARG);
        return true;
    }

    error_set(ctx, "unknown trigger type: '%s'", string);
    return false;
}

static bool parse_condition_attr_comparison(Condition *condition, const char *argument)
{
    const char *less_than = strchr(argument, '<');
    const char *greater_than = strchr(argument, '>');
    const char *equals = strstr(argument, "==");

    if (equals) {
        condition->type = COND_ATTR_EQ;
        size_t name_len = (size_t)(equals - argument);
        if (name_len >= MAX_ARG) {
            name_len = MAX_ARG - 1;
        }
        memcpy(condition->argument, argument, name_len);
        condition->argument[name_len] = '\0';
        condition->compare_value = strtof(equals + 2, NULL);
        return true;
    }
    if (less_than) {
        condition->type = COND_ATTR_LT;
        size_t name_len = (size_t)(less_than - argument);
        if (name_len >= MAX_ARG) {
            name_len = MAX_ARG - 1;
        }
        memcpy(condition->argument, argument, name_len);
        condition->argument[name_len] = '\0';
        condition->compare_value = strtof(less_than + 1, NULL);
        return true;
    }
    if (greater_than) {
        condition->type = COND_ATTR_GT;
        size_t name_len = (size_t)(greater_than - argument);
        if (name_len >= MAX_ARG) {
            name_len = MAX_ARG - 1;
        }
        memcpy(condition->argument, argument, name_len);
        condition->argument[name_len] = '\0';
        condition->compare_value = strtof(greater_than + 1, NULL);
        return true;
    }

    return false;
}

bool condition_parse(struct EngineContext *ctx, Condition *condition, const char *string)
{
    memset(condition, 0, sizeof(*condition));
    Strv strv = strv_from_cstr(string);

    if (strv_starts_with_cstr(strv, "not_flag:")) {
        condition->type = COND_NOT_FLAG;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }
    if (strv_starts_with_cstr(strv, "flag:")) {
        condition->type = COND_FLAG;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }
    if (strv_starts_with_cstr(strv, "has_item:")) {
        condition->type = COND_HAS_ITEM;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }
    if (strv_starts_with_cstr(strv, "var:")) {
        condition->type = COND_VAR;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }
    if (strv_starts_with_cstr(strv, "not_attr:")) {
        condition->type = COND_NOT_ATTR;
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }
    if ((int)strv_starts_with_cstr(strv, "self.attr:") || (int)strv_starts_with_cstr(strv, "attr:")) {
        Strv rest = strv;
        (void)strv_split(&rest, ':');
        char attr_arg[MAX_ARG];
        strv_copy_to_cstr(rest, attr_arg, MAX_ARG);
        if (parse_condition_attr_comparison(condition, attr_arg)) {
            return true;
        }
        condition->type = COND_ATTR;
        strv_copy_to_cstr(rest, condition->argument, MAX_ARG);
        return true;
    }

    error_set(ctx, "unknown condition: '%s'", string);
    return false;
}

static bool parse_action_two_args(ActionNode *node, Strv strv)
{
    Strv first = strv_split(&strv, ',');
    strv_copy_to_cstr(first, node->argument, MAX_ARG);
    if (strv.ptr) {
        strv_copy_to_cstr(strv, node->second_argument, MAX_ARG);
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
    {"create_timer:", ACTION_CREATE_TIMER, true},
    {"destroy_timer:", ACTION_DESTROY_TIMER, true},
    {NULL, 0, false},
};

static bool parse_simple_action(struct EngineContext *ctx, ActionNode *node, const char *string)
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
            return parse_action_two_args(node, rest);
        }
    }

    error_set(ctx, "unknown action: '%s'", string);
    return false;
}

typedef struct {
    ActionNode *target;
    toml_table_t *table;
} ParseCFTask;

static bool parse_branch_array_into(struct EngineContext *ctx,
                                    ActionNode **out_nodes,
                                    int *out_count,
                                    toml_array_t *array,
                                    Arena *arena,
                                    const char *branch_name,
                                    ParseCFTask *task_stack,
                                    int *task_top)
{
    if (!array) {
        return true;
    }
    int count = toml_array_nelem(array);
    if (count <= 0) {
        return true;
    }
    ActionNode *nodes = arena_alloc(
        ctx, arena, (AllocRequest){.size = (size_t)count * sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    if (!nodes) {
        error_wrap(ctx, "parse_branch_array_into: arena_alloc");
        return false;
    }
    for (int child_index = 0; child_index < count; child_index++) {
        toml_datum_t value = toml_string_at(array, child_index);
        if (value.ok) {
            if (!action_node_parse(ctx, &nodes[child_index], value, arena)) {
                error_wrap(ctx, "%s[%d]", branch_name, child_index);
                free(value.u.s);
                return false;
            }
            free(value.u.s);
        } else {
            toml_table_t *table = toml_table_at(array, child_index);
            if (!table) {
                error_set(ctx, "%s[%d] is not a string or table", branch_name, child_index);
                return false;
            }
            if (*task_top >= MAX_PARSE_CF_STACK) {
                error_set(ctx, "parse_branch_array_into: task stack overflow");
                return false;
            }
            memset(&nodes[child_index], 0, sizeof(ActionNode));
            task_stack[(*task_top)++] = (ParseCFTask){&nodes[child_index], table};
        }
    }
    *out_nodes = nodes;
    *out_count = count;
    return true;
}

static bool parse_one_cf_node(struct EngineContext *ctx,
                              ActionNode *node,
                              toml_table_t *table,
                              Arena *arena,
                              ParseCFTask *task_stack,
                              int *task_top)
{
    memset(node, 0, sizeof(*node));

    toml_datum_t if_cond = toml_string_in(table, "if");
    if (if_cond.ok) {
        node->type = ACTION_IF_ELSE;
        strncpy(node->argument, if_cond.u.s, MAX_ARG - 1);
        free(if_cond.u.s);
        if (!parse_branch_array_into(ctx, &node->children, &node->child_count, toml_array_in(table, "then"), arena,
                                     "then", task_stack, task_top)) {
            return false;
        }
        return parse_branch_array_into(ctx, &node->else_children, &node->else_child_count, toml_array_in(table, "else"),
                                       arena, "else", task_stack, task_top);
    }

    toml_datum_t repeat_str = toml_string_in(table, "repeat");
    if (repeat_str.ok) {
        node->type = ACTION_REPEAT;
        strncpy(node->argument, repeat_str.u.s, MAX_ARG - 1);
        free(repeat_str.u.s);
        return parse_branch_array_into(ctx, &node->children, &node->child_count, toml_array_in(table, "do"), arena,
                                       "do", task_stack, task_top);
    }

    error_set(ctx, "unknown control flow structure");
    return false;
}

static bool parse_control_flow_node(struct EngineContext *ctx, ActionNode *node, toml_table_t *table, Arena *arena)
{
    ParseCFTask task_stack[MAX_PARSE_CF_STACK];
    int task_top = 0;

    if (!parse_one_cf_node(ctx, node, table, arena, task_stack, &task_top)) {
        return false;
    }
    while (task_top > 0) {
        ParseCFTask task = task_stack[--task_top];
        if (!parse_one_cf_node(ctx, task.target, task.table, arena, task_stack, &task_top)) {
            return false;
        }
    }
    return true;
}

bool action_node_parse(struct EngineContext *ctx, ActionNode *node, toml_datum_t value, Arena *arena)
{
    (void)arena;
    if (!value.ok || !value.u.s) {
        error_set(ctx, "action_node_parse: expected string value");
        return false;
    }
    return parse_simple_action(ctx, node, value.u.s);
}

/* ---- TOML rule parsing ---- */

static bool parse_conditions_array(struct EngineContext *ctx, Rule *rule, toml_array_t *conditions)
{
    if (!conditions) {
        return true;
    }
    int count = toml_array_nelem(conditions);
    if (count > MAX_CONDITIONS) {
        error_set(ctx, "too many conditions (%d, max %d)", count, MAX_CONDITIONS);
        return false;
    }
    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(conditions, index);
        if (!value.ok) {
            error_set(ctx, "condition[%d] is not a string", index);
            return false;
        }
        if (!condition_parse(ctx, &rule->conditions[rule->condition_count], value.u.s)) {
            error_wrap(ctx, "condition[%d]", index);
            free(value.u.s);
            return false;
        }
        rule->condition_count++;
        free(value.u.s);
    }
    return true;
}

static bool parse_actions_array(struct EngineContext *ctx, Rule *rule, toml_array_t *actions, Arena *arena)
{
    if (!actions) {
        return true;
    }
    int count = toml_array_nelem(actions);
    if (count > MAX_ACTIONS) {
        error_set(ctx, "too many actions (%d, max %d)", count, MAX_ACTIONS);
        return false;
    }

    ActionNode *nodes = arena_alloc(
        ctx, arena, (AllocRequest){.size = (size_t)count * sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    if (!nodes) {
        error_wrap(ctx, "parse_actions_array: arena_alloc");
        return false;
    }

    for (int index = 0; index < count; index++) {
        // Try string first
        toml_datum_t value = toml_string_at(actions, index);
        if (value.ok) {
            if (!action_node_parse(ctx, &nodes[index], value, arena)) {
                error_wrap(ctx, "action[%d]", index);
                free(value.u.s);
                return false;
            }
            free(value.u.s);
        } else {
            toml_table_t *table_value = toml_table_at(actions, index);
            if (table_value) {
                if (!parse_control_flow_node(ctx, &nodes[index], table_value, arena)) {
                    error_wrap(ctx, "action[%d]", index);
                    return false;
                }
            } else {
                error_set(ctx, "action[%d] is not a string or table", index);
                return false;
            }
        }
    }

    rule->action_tree.nodes = nodes;
    rule->action_tree.count = count;
    return true;
}

static bool parse_single_rule(struct EngineContext *ctx, Rule *rule, toml_table_t *entry, Arena *arena)
{
    memset(rule, 0, sizeof(*rule));

    toml_datum_t trigger_str = toml_string_in(entry, "trigger");
    if (!trigger_str.ok) {
        error_set(ctx, "rule missing 'trigger' key");
        return false;
    }
    if (!trigger_parse(ctx, &rule->trigger, trigger_str.u.s)) {
        error_wrap(ctx, "rule trigger");
        free(trigger_str.u.s);
        return false;
    }
    free(trigger_str.u.s);

    toml_array_t *conditions = toml_array_in(entry, "conditions");
    if (!parse_conditions_array(ctx, rule, conditions)) {
        error_wrap(ctx, "rule conditions");
        return false;
    }

    toml_array_t *actions = toml_array_in(entry, "actions");
    if (!parse_actions_array(ctx, rule, actions, arena)) {
        error_wrap(ctx, "rule actions");
        return false;
    }

    return true;
}

bool rules_parse(struct EngineContext *ctx, RuleSet *rules, void *toml_blueprint_table, Arena *arena)
{
    rules->entries = NULL;
    rules->count = 0;

    toml_array_t *rule_array = toml_array_in(toml_blueprint_table, "rule");
    if (!rule_array) {
        return true;
    }

    int count = toml_array_nelem(rule_array);
    if (count == 0) {
        return true;
    }
    if (count > MAX_RULES) {
        error_set(ctx, "too many rules (%d, max %d)", count, MAX_RULES);
        return false;
    }

    Rule *entries =
        arena_alloc(ctx, arena, (AllocRequest){.size = (size_t)count * sizeof(Rule), .alignment = _Alignof(Rule)});
    if (!entries) {
        error_wrap(ctx, "rules_parse: arena_alloc");
        return false;
    }

    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(rule_array, index);
        if (!entry) {
            error_set(ctx, "rule[%d]: toml_table_at returned NULL", index);
            return false;
        }
        if (!parse_single_rule(ctx, &entries[index], entry, arena)) {
            error_wrap(ctx, "rule[%d]", index);
            return false;
        }
    }

    rules->entries = entries;
    rules->count = count;
    return true;
}

/* ---- Trigger matching ---- */

bool trigger_matches(const Trigger *trigger, const TriggerEvent *event)
{
    if (trigger->type != event->type) {
        return false;
    }
    if (trigger->type == TRIGGER_EVENT || trigger->type == TRIGGER_ATTR_CHANGED) {
        return strcmp(trigger->argument, event->argument) == 0;
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
        return attr->value.s[0] != '\0';
    }
    return false;
}

static bool evaluate_single_condition(const Condition *condition, ConditionContext context)
{
    switch (condition->type) {
    case COND_FLAG:
        return flag_get(context.flags, condition->argument);
    case COND_NOT_FLAG:
        return (bool)!flag_get(context.flags, condition->argument);
    case COND_ATTR: {
        const Attribute *attr = entity_get_attr(context.entity, condition->argument);
        if (!attr) {
            return false;
        }
        return attr_is_truthy(attr);
    }
    case COND_NOT_ATTR: {
        const Attribute *attr = entity_get_attr(context.entity, condition->argument);
        if (!attr) {
            return true;
        }
        return (bool)!attr_is_truthy(attr);
    }
    case COND_ATTR_LT: {
        const Attribute *attr = entity_get_attr(context.entity, condition->argument);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) < condition->compare_value;
    }
    case COND_ATTR_GT: {
        const Attribute *attr = entity_get_attr(context.entity, condition->argument);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) > condition->compare_value;
    }
    case COND_ATTR_EQ: {
        const Attribute *attr = entity_get_attr(context.entity, condition->argument);
        if (!attr) {
            return false;
        }
        return attr_to_float(attr) == condition->compare_value;
    }
    case COND_HAS_ITEM:
        return true;
    case COND_VAR: {
        const Attribute *var = NULL;
        if (context.local_vars) {
            var = attr_get(context.local_vars, condition->argument);
        }
        if (!var && context.global_vars) {
            var = attr_get(context.global_vars, condition->argument);
        }
        if (!var) {
            return false;
        }
        if (var->type == ATTR_STRING) {
            return var->value.s[0] != '\0';
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

static Entity *resolve_target(const char *target_spec, ActionContext context, char *attr_name_out, int attr_name_max)
{
    Strv strv = strv_from_cstr(target_spec);
    Strv head = strv_split(&strv, '.');
    if (!strv.ptr) {
        strv_copy_to_cstr(head, attr_name_out, (size_t)attr_name_max);
        return context.entity;
    }

    char tag[MAX_TAG];
    strv_copy_to_cstr(head, tag, MAX_TAG);
    strv_copy_to_cstr(strv, attr_name_out, (size_t)attr_name_max);
    return entity_find_by_tag_mut(context.entity, tag, context.entities, context.entity_count);
}

static bool evaluate_condition_string(struct EngineContext *ctx, const char *condition_str, ConditionContext context)
{
    Condition temp_cond;
    if (!condition_parse(ctx, &temp_cond, condition_str)) {
        debug_log(ctx, "control flow: failed to parse condition '%s'", condition_str);
        return false;
    }
    return evaluate_single_condition(&temp_cond, context);
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
    return NULL;
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
        strncpy(out, var->value.s, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
    }
}

static void resolve_arg(char *out, int out_size, const char *arg, ActionContext context)
{
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

static bool execute_set_attr_action(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ATTR_NAME];
    Entity *target = resolve_target(node->argument, context, attr_name, MAX_ATTR_NAME);
    if (!target) {
        debug_log(ctx, "set_attr: target not found: %s", node->argument);
        return true;
    }
    char resolved[MAX_ARG];
    resolve_arg(resolved, MAX_ARG, node->second_argument, context);
    float value = strtof(resolved, NULL);
    if (strchr(resolved, '.')) {
        return attr_set_float(ctx, &target->attrs, attr_name, value);
    }
    if (strcmp(resolved, "true") == 0) {
        return attr_set_bool(ctx, &target->attrs, attr_name, true);
    }
    if (strcmp(resolved, "false") == 0) {
        return attr_set_bool(ctx, &target->attrs, attr_name, false);
    }
    return attr_set_int(ctx, &target->attrs, attr_name, (int)value);
}

static bool execute_add_attr_action(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ATTR_NAME];
    Entity *target = resolve_target(node->argument, context, attr_name, MAX_ATTR_NAME);
    if (!target) {
        debug_log(ctx, "add_attr: target not found: %s", node->argument);
        return true;
    }
    const Attribute *existing = entity_get_attr(target, attr_name);
    char resolved[MAX_ARG];
    resolve_arg(resolved, MAX_ARG, node->second_argument, context);
    float delta = strtof(resolved, NULL);
    if (existing && existing->type == ATTR_FLOAT) {
        return attr_set_float(ctx, &target->attrs, attr_name, existing->value.f + delta);
    }
    int current_val = existing ? existing->value.i : 0;
    return attr_set_int(ctx, &target->attrs, attr_name, current_val + (int)delta);
}

static bool execute_toggle_attr_action(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    char attr_name[MAX_ATTR_NAME];
    Entity *target = resolve_target(node->argument, context, attr_name, MAX_ATTR_NAME);
    if (!target) {
        debug_log(ctx, "toggle_attr: target not found: %s", node->argument);
        return true;
    }
    bool current_val = entity_get_bool(target, attr_name, false);
    return attr_set_bool(ctx, &target->attrs, attr_name, (bool)!current_val);
}

static bool execute_set_var_action(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    char resolved_value[MAX_ARG];
    resolve_arg(resolved_value, MAX_ARG, node->second_argument, context);

    AttrSet *target_vars;
    const char *var_name;
    if (strv_starts_with_cstr(strv_from_cstr(node->argument), GLOBAL_VAR_PREFIX)) {
        target_vars = context.global_vars;
        var_name = node->argument + strlen(GLOBAL_VAR_PREFIX);
    } else {
        target_vars = context.local_vars;
        var_name = node->argument;
    }
    if (!target_vars) {
        debug_log(ctx, "set_var: no var storage for '%s'", node->argument);
        return true;
    }
    if (strcmp(resolved_value, "true") == 0) {
        return attr_set_bool(ctx, target_vars, var_name, true);
    }
    if (strcmp(resolved_value, "false") == 0) {
        return attr_set_bool(ctx, target_vars, var_name, false);
    }
    if (strchr(resolved_value, '.')) {
        return attr_set_float(ctx, target_vars, var_name, strtof(resolved_value, NULL));
    }
    char *endptr;
    long ival = strtol(resolved_value, &endptr, RADIX_DECIMAL);
    if (endptr != resolved_value && *endptr == '\0') {
        return attr_set_int(ctx, target_vars, var_name, (int)ival);
    }
    return attr_set_string(ctx, target_vars, (AttrStringPair){.name = var_name, .value = resolved_value});
}

static bool push_branch_nodes(
    struct EngineContext *ctx, const ActionNode **exec_stack, int *stack_top, const ActionNode *nodes, int count)
{
    for (int push_index = count - 1; push_index >= 0; push_index--) {
        if (*stack_top >= MAX_EXEC_STACK) {
            error_set(ctx, "action execution stack overflow");
            return false;
        }
        exec_stack[(*stack_top)++] = &nodes[push_index];
    }
    return true;
}

static bool expand_if_else_node(struct EngineContext *ctx,
                                const ActionNode *node,
                                ActionContext context,
                                const ActionNode **exec_stack,
                                int *stack_top)
{
    ConditionContext cond_ctx = {
        .entity = context.entity,
        .entities = context.entities,
        .entity_count = context.entity_count,
        .flags = context.flags,
        .local_vars = context.local_vars,
        .global_vars = context.global_vars,
    };
    bool condition_met = evaluate_condition_string(ctx, node->argument, cond_ctx);
    const ActionNode *branch;
    int branch_count;
    if (condition_met) {
        branch = node->children;
        branch_count = node->child_count;
    } else {
        branch = node->else_children;
        branch_count = node->else_child_count;
    }
    return push_branch_nodes(ctx, exec_stack, stack_top, branch, branch_count);
}

static bool
expand_repeat_node(struct EngineContext *ctx, const ActionNode *node, const ActionNode **exec_stack, int *stack_top)
{
    int repeat_count = (int)strtol(node->argument, NULL, RADIX_DECIMAL);
    if (repeat_count <= 0) {
        repeat_count = 1;
    }
    for (int repeat_index = 0; repeat_index < repeat_count; repeat_index++) {
        if (!push_branch_nodes(ctx, exec_stack, stack_top, node->children, node->child_count)) {
            return false;
        }
    }
    return true;
}

static bool dispatch_simple_action(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    switch (node->type) {
    case ACTION_SET_FLAG:
        flag_set(ctx, context.flags, node->argument);
        return true;
    case ACTION_CLEAR_FLAG:
        flag_clear(context.flags, node->argument);
        return true;
    case ACTION_SET_ATTR:
        return execute_set_attr_action(ctx, node, context);
    case ACTION_ADD_ATTR:
        return execute_add_attr_action(ctx, node, context);
    case ACTION_TOGGLE_ATTR:
        return execute_toggle_attr_action(ctx, node, context);
    case ACTION_SET_VAR:
        return execute_set_var_action(ctx, node, context);
    case ACTION_DESTROY:
        context.entity->active = false;
        return true;
    case ACTION_FIRE_EVENT: {
        TriggerEvent fire = {.type = TRIGGER_EVENT, .entity_index = -1};
        strncpy(fire.argument, node->argument, MAX_ARG - 1);
        return vec_trigger_event_push(context.event_queue, fire);
    }
    default:
        debug_log(ctx, "action stub: %s (not yet implemented)", node->argument);
        return true;
    }
}

bool action_node_execute(struct EngineContext *ctx, const ActionNode *node, ActionContext context)
{
    const ActionNode *exec_stack[MAX_EXEC_STACK];
    int stack_top = 0;
    exec_stack[stack_top++] = node;

    while (stack_top > 0) {
        const ActionNode *current = exec_stack[--stack_top];
        if (current->type == ACTION_IF_ELSE) {
            if (!expand_if_else_node(ctx, current, context, exec_stack, &stack_top)) {
                return false;
            }
        } else if (current->type == ACTION_REPEAT) {
            if (!expand_repeat_node(ctx, current, exec_stack, &stack_top)) {
                return false;
            }
        } else if (!dispatch_simple_action(ctx, current, context)) {
            return false;
        }
    }
    return true;
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

static void evaluate_entity_rules(struct EngineContext *ctx,
                                  Entity *entity,
                                  int entity_index,
                                  Entity *entities,
                                  int entity_count,
                                  FlagSet *flags,
                                  AttrSet *global_vars,
                                  const vec_trigger_event *pending_events,
                                  vec_trigger_event *next_events)
{
    const RuleSet *ruleset = &entity->blueprint->rules;
    for (int rule_index = 0; rule_index < ruleset->count; rule_index++) {
        const Rule *rule = &ruleset->entries[rule_index];
        if (!rule_triggered_by_events(rule, entity_index, pending_events)) {
            continue;
        }
        AttrSet local_vars = {0};
        ConditionContext cond_ctx = {
            .entity = entity,
            .entities = entities,
            .entity_count = entity_count,
            .flags = flags,
            .local_vars = &local_vars,
            .global_vars = global_vars,
        };
        ActionContext act_ctx = {
            .entity = entity,
            .entity_index = entity_index,
            .entities = entities,
            .entity_count = entity_count,
            .flags = flags,
            .event_queue = next_events,
            .local_vars = &local_vars,
            .global_vars = global_vars,
        };
        debug_log(ctx, "Rule triggered for entity %d (type: %s), rule %d", entity_index, entity->blueprint->name,
                  rule_index);
        if (!conditions_evaluate(rule->conditions, rule->condition_count, cond_ctx)) {
            debug_log(ctx, "Conditions not met for rule %d on entity %d (type: %s)", rule_index, entity_index,
                      entity->blueprint->name);
            continue;
        }
        debug_log(ctx, "Executing actions for rule %d on entity %d (type: %s)", rule_index, entity_index,
                  entity->blueprint->name);
        for (int action_index = 0; action_index < rule->action_tree.count; action_index++) {
            (void)action_node_execute(ctx, &rule->action_tree.nodes[action_index], act_ctx);
        }
        vec_attribute_free(&local_vars.entries);
    }
}

void rules_evaluate_batch(struct EngineContext *ctx,
                          Entity *entities,
                          int entity_count,
                          const TriggerEvent *events,
                          int event_count,
                          FlagSet *flags,
                          AttrSet *global_vars)
{
    vec_trigger_event pending_events = {0};
    for (int event_index = 0; event_index < event_count; event_index++) {
        (void)vec_trigger_event_push(&pending_events, events[event_index]);
    }

    for (int cascade = 0; cascade < MAX_EVENT_CASCADES && pending_events.count > 0; cascade++) {
        vec_trigger_event next_events = {0};

        for (int entity_index = 0; entity_index < entity_count; entity_index++) {
            Entity *entity = &entities[entity_index];
            if (!entity->active || !entity->blueprint) {
                continue;
            }
            if (entity->blueprint->rules.count == 0) {
                continue;
            }
            evaluate_entity_rules(ctx, entity, entity_index, entities, entity_count, flags, global_vars,
                                  &pending_events, &next_events);
        }

        vec_trigger_event_free(&pending_events);
        pending_events = next_events;
    }

    vec_trigger_event_free(&pending_events);
}

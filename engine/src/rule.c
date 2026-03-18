#include "rule.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "error.h"

#include "toml.h"

#include <stdlib.h>
#include <string.h>

/* ---- FlagSet ---- */

static int flag_find(const FlagSet *flags, const char *name)
{
    for (int index = 0; index < flags->count; index++) {
        if (strcmp(flags->names[index], name) == 0) {
            return index;
        }
    }
    return -1;
}

bool flag_get(const FlagSet *flags, const char *name)
{
    return flag_find(flags, name) >= 0;
}

void flag_set(FlagSet *flags, const char *name)
{
    if (flag_find(flags, name) >= 0) {
        return;
    }
    if (flags->count >= MAX_FLAGS) {
        debug_log("flag_set: flag table full (%d), cannot add '%s'", MAX_FLAGS, name);
        return;
    }
    strncpy(flags->names[flags->count], name, MAX_FLAG_NAME - 1);
    flags->names[flags->count][MAX_FLAG_NAME - 1] = '\0';
    flags->count++;
}

void flag_clear(FlagSet *flags, const char *name)
{
    int index = flag_find(flags, name);
    if (index < 0) {
        return;
    }
    flags->count--;
    if (index < flags->count) {
        memcpy(flags->names[index], flags->names[flags->count], MAX_FLAG_NAME);
    }
}

/* ---- EventQueue ---- */

void event_queue_clear(EventQueue *queue)
{
    queue->count = 0;
}

bool event_queue_push(EventQueue *queue, TriggerEvent event)
{
    if (queue->count >= MAX_TRIGGER_EVENTS) {
        error_set("event queue full (%d)", MAX_TRIGGER_EVENTS);
        return false;
    }
    queue->entries[queue->count] = event;
    queue->count++;
    return true;
}

/* ---- Parsing helpers ---- */

static bool starts_with(const char *string, const char *prefix)
{
    return strncmp(string, prefix, strlen(prefix)) == 0;
}

static void copy_after_colon(const char *string, char *out, int max_len)
{
    const char *colon = strchr(string, ':');
    if (colon) {
        strncpy(out, colon + 1, (size_t)(max_len - 1));
        out[max_len - 1] = '\0';
    }
}

bool trigger_parse(Trigger *trigger, const char *string)
{
    memset(trigger, 0, sizeof(*trigger));

    if (strcmp(string, "interact") == 0) {
        trigger->type = TRIGGER_INTERACT;
        return true;
    }
    if (strcmp(string, "enter") == 0) {
        trigger->type = TRIGGER_ENTER;
        return true;
    }
    if (strcmp(string, "on_spawn") == 0) {
        trigger->type = TRIGGER_ON_SPAWN;
        return true;
    }
    if (starts_with(string, "event:")) {
        trigger->type = TRIGGER_EVENT;
        copy_after_colon(string, trigger->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "attr_changed:")) {
        trigger->type = TRIGGER_ATTR_CHANGED;
        copy_after_colon(string, trigger->argument, MAX_ARG);
        return true;
    }

    error_set("unknown trigger type: '%s'", string);
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

bool condition_parse(Condition *condition, const char *string)
{
    memset(condition, 0, sizeof(*condition));

    if (starts_with(string, "not_flag:")) {
        condition->type = COND_NOT_FLAG;
        copy_after_colon(string, condition->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "flag:")) {
        condition->type = COND_FLAG;
        copy_after_colon(string, condition->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "has_item:")) {
        condition->type = COND_HAS_ITEM;
        copy_after_colon(string, condition->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "var:")) {
        condition->type = COND_VAR;
        copy_after_colon(string, condition->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "not_attr:")) {
        condition->type = COND_NOT_ATTR;
        copy_after_colon(string, condition->argument, MAX_ARG);
        return true;
    }
    if (starts_with(string, "self.attr:")) {
        const char *attr_arg = strchr(string, ':') + 1;
        if (parse_condition_attr_comparison(condition, attr_arg)) {
            return true;
        }
        condition->type = COND_ATTR;
        strncpy(condition->argument, attr_arg, MAX_ARG - 1);
        return true;
    }
    if (starts_with(string, "attr:")) {
        const char *attr_arg = strchr(string, ':') + 1;
        if (parse_condition_attr_comparison(condition, attr_arg)) {
            return true;
        }
        condition->type = COND_ATTR;
        strncpy(condition->argument, attr_arg, MAX_ARG - 1);
        return true;
    }

    error_set("unknown condition: '%s'", string);
    return false;
}

static bool parse_action_two_args(Action *action, const char *after_colon)
{
    const char *comma = strchr(after_colon, ',');
    if (!comma) {
        strncpy(action->argument, after_colon, MAX_ARG - 1);
        return true;
    }

    size_t first_len = (size_t)(comma - after_colon);
    if (first_len >= MAX_ARG) {
        first_len = MAX_ARG - 1;
    }
    memcpy(action->argument, after_colon, first_len);
    action->argument[first_len] = '\0';

    strncpy(action->second_argument, comma + 1, MAX_ARG - 1);
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

bool action_parse(Action *action, const char *string)
{
    memset(action, 0, sizeof(*action));

    for (int index = 0; action_mappings[index].prefix; index++) {
        const ActionMapping *mapping = &action_mappings[index];
        if (!mapping->has_args) {
            if (strcmp(string, mapping->prefix) == 0) {
                action->type = mapping->type;
                return true;
            }
            continue;
        }
        if (starts_with(string, mapping->prefix)) {
            action->type = mapping->type;
            const char *after_prefix = string + strlen(mapping->prefix);
            return parse_action_two_args(action, after_prefix);
        }
    }

    error_set("unknown action: '%s'", string);
    return false;
}

/* ---- TOML rule parsing ---- */

static bool parse_conditions_array(Rule *rule, toml_array_t *conditions)
{
    if (!conditions) {
        return true;
    }
    int count = toml_array_nelem(conditions);
    if (count > MAX_CONDITIONS) {
        error_set("too many conditions (%d, max %d)", count, MAX_CONDITIONS);
        return false;
    }
    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(conditions, index);
        if (!value.ok) {
            error_set("condition[%d] is not a string", index);
            return false;
        }
        if (!condition_parse(&rule->conditions[rule->condition_count], value.u.s)) {
            error_wrap("condition[%d]", index);
            free(value.u.s);
            return false;
        }
        rule->condition_count++;
        free(value.u.s);
    }
    return true;
}

static bool parse_actions_array(Rule *rule, toml_array_t *actions)
{
    if (!actions) {
        return true;
    }
    int count = toml_array_nelem(actions);
    if (count > MAX_ACTIONS) {
        error_set("too many actions (%d, max %d)", count, MAX_ACTIONS);
        return false;
    }
    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(actions, index);
        if (!value.ok) {
            error_set("action[%d] is not a string", index);
            return false;
        }
        if (!action_parse(&rule->actions[rule->action_count], value.u.s)) {
            error_wrap("action[%d]", index);
            free(value.u.s);
            return false;
        }
        rule->action_count++;
        free(value.u.s);
    }
    return true;
}

static bool parse_single_rule(Rule *rule, toml_table_t *entry)
{
    memset(rule, 0, sizeof(*rule));

    toml_datum_t trigger_str = toml_string_in(entry, "trigger");
    if (!trigger_str.ok) {
        error_set("rule missing 'trigger' key");
        return false;
    }
    if (!trigger_parse(&rule->trigger, trigger_str.u.s)) {
        error_wrap("rule trigger");
        free(trigger_str.u.s);
        return false;
    }
    free(trigger_str.u.s);

    toml_array_t *conditions = toml_array_in(entry, "conditions");
    if (!parse_conditions_array(rule, conditions)) {
        error_wrap("rule conditions");
        return false;
    }

    toml_array_t *actions = toml_array_in(entry, "actions");
    if (!parse_actions_array(rule, actions)) {
        error_wrap("rule actions");
        return false;
    }

    return true;
}

bool rules_parse(RuleSet *rules, void *toml_blueprint_table, Arena *arena)
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
        error_set("too many rules (%d, max %d)", count, MAX_RULES);
        return false;
    }

    Rule *entries =
        arena_alloc(arena, (AllocRequest){.size = (size_t)count * sizeof(Rule), .alignment = _Alignof(Rule)});
    if (!entries) {
        error_wrap("rules_parse: arena_alloc");
        return false;
    }

    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(rule_array, index);
        if (!entry) {
            error_set("rule[%d]: toml_table_at returned NULL", index);
            return false;
        }
        if (!parse_single_rule(&entries[index], entry)) {
            error_wrap("rule[%d]", index);
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
    case COND_VAR:
        return true;
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
    const char *dot = strchr(target_spec, '.');
    if (!dot) {
        strncpy(attr_name_out, target_spec, (size_t)(attr_name_max - 1));
        attr_name_out[attr_name_max - 1] = '\0';
        return context.entity;
    }

    size_t tag_len = (size_t)(dot - target_spec);
    char tag[MAX_TAG];
    if (tag_len >= MAX_TAG) {
        tag_len = MAX_TAG - 1;
    }
    memcpy(tag, target_spec, tag_len);
    tag[tag_len] = '\0';

    strncpy(attr_name_out, dot + 1, (size_t)(attr_name_max - 1));
    attr_name_out[attr_name_max - 1] = '\0';

    return entity_find_by_tag_mut(context.entity, tag, context.entities, context.entity_count);
}

bool action_execute(const Action *action, ActionContext context)
{
    switch (action->type) {
    case ACTION_SET_FLAG:
        flag_set(context.flags, action->argument);
        return true;

    case ACTION_CLEAR_FLAG:
        flag_clear(context.flags, action->argument);
        return true;

    case ACTION_SET_ATTR: {
        char attr_name[MAX_ATTR_NAME];
        Entity *target = resolve_target(action->argument, context, attr_name, MAX_ATTR_NAME);
        if (!target) {
            debug_log("set_attr: target not found: %s", action->argument);
            return true;
        }
        float value = strtof(action->second_argument, NULL);
        if (strchr(action->second_argument, '.')) {
            return attr_set_float(&target->attrs, attr_name, value);
        }
        if (strcmp(action->second_argument, "true") == 0) {
            return attr_set_bool(&target->attrs, attr_name, true);
        }
        if (strcmp(action->second_argument, "false") == 0) {
            return attr_set_bool(&target->attrs, attr_name, false);
        }
        return attr_set_int(&target->attrs, attr_name, (int)value);
    }

    case ACTION_ADD_ATTR: {
        char attr_name[MAX_ATTR_NAME];
        Entity *target = resolve_target(action->argument, context, attr_name, MAX_ATTR_NAME);
        if (!target) {
            debug_log("add_attr: target not found: %s", action->argument);
            return true;
        }
        const Attribute *existing = entity_get_attr(target, attr_name);
        float delta = strtof(action->second_argument, NULL);
        if (existing && existing->type == ATTR_FLOAT) {
            return attr_set_float(&target->attrs, attr_name, existing->value.f + delta);
        }
        int current = existing ? existing->value.i : 0;
        return attr_set_int(&target->attrs, attr_name, current + (int)delta);
    }

    case ACTION_TOGGLE_ATTR: {
        char attr_name[MAX_ATTR_NAME];
        Entity *target = resolve_target(action->argument, context, attr_name, MAX_ATTR_NAME);
        if (!target) {
            debug_log("toggle_attr: target not found: %s", action->argument);
            return true;
        }
        bool current = entity_get_bool(target, attr_name, false);
        return attr_set_bool(&target->attrs, attr_name, (bool)!current);
    }

    case ACTION_DESTROY:
        context.entity->active = false;
        return true;

    case ACTION_FIRE_EVENT: {
        TriggerEvent fire = {.type = TRIGGER_EVENT, .entity_index = -1};
        strncpy(fire.argument, action->argument, MAX_ARG - 1);
        return event_queue_push(context.event_queue, fire);
    }

    case ACTION_GIVE_ITEM:
    case ACTION_REMOVE_ITEM:
    case ACTION_CHANGE_SPRITE:
    case ACTION_PLAY_SOUND:
    case ACTION_DIALOGUE:
    case ACTION_TRANSITION:
    case ACTION_SPAWN:
    case ACTION_CAMERA_PAN:
    case ACTION_CAMERA_SHAKE:
    case ACTION_CALL:
    case ACTION_SET_VAR:
    case ACTION_WAIT:
    case ACTION_CREATE_TIMER:
    case ACTION_DESTROY_TIMER:
        debug_log("action stub: %s (not yet implemented)", action->argument);
        return true;
    }
    return true;
}

/* ---- Evaluation loop ---- */

static bool rule_triggered_by_events(const Rule *rule, int entity_index, const EventQueue *pending_events)
{
    for (int pending_index = 0; pending_index < pending_events->count; pending_index++) {
        const TriggerEvent *pending = &pending_events->entries[pending_index];
        if (pending->entity_index >= 0 && pending->entity_index != entity_index) {
            continue;
        }
        if (trigger_matches(&rule->trigger, pending)) {
            return true;
        }
    }
    return false;
}

static void evaluate_entity_rules(Entity *entity,
                                  int entity_index,
                                  Entity *entities,
                                  int entity_count,
                                  FlagSet *flags,
                                  const EventQueue *pending_events,
                                  EventQueue *next_events)
{
    const RuleSet *ruleset = &entity->blueprint->rules;
    ConditionContext cond_ctx = {
        .entity = entity,
        .entities = entities,
        .entity_count = entity_count,
        .flags = flags,
    };
    ActionContext act_ctx = {
        .entity = entity,
        .entity_index = entity_index,
        .entities = entities,
        .entity_count = entity_count,
        .flags = flags,
        .event_queue = next_events,
    };

    for (int rule_index = 0; rule_index < ruleset->count; rule_index++) {
        const Rule *rule = &ruleset->entries[rule_index];
        if (!rule_triggered_by_events(rule, entity_index, pending_events)) {
            continue;
        }
        if (!conditions_evaluate(rule->conditions, rule->condition_count, cond_ctx)) {
            continue;
        }
        for (int action_index = 0; action_index < rule->action_count; action_index++) {
            (void)action_execute(&rule->actions[action_index], act_ctx);
        }
    }
}

void rules_evaluate_batch(
    Entity *entities, int entity_count, const TriggerEvent *events, int event_count, FlagSet *flags)
{
    EventQueue pending_events;
    event_queue_clear(&pending_events);

    for (int event_index = 0; event_index < event_count; event_index++) {
        (void)event_queue_push(&pending_events, events[event_index]);
    }

    EventQueue next_events;

    for (int cascade = 0; cascade < MAX_EVENT_CASCADES && pending_events.count > 0; cascade++) {
        event_queue_clear(&next_events);

        for (int entity_index = 0; entity_index < entity_count; entity_index++) {
            Entity *entity = &entities[entity_index];
            if (!entity->active || !entity->blueprint) {
                continue;
            }
            if (entity->blueprint->rules.count == 0) {
                continue;
            }
            evaluate_entity_rules(entity, entity_index, entities, entity_count, flags, &pending_events, &next_events);
        }

        pending_events = next_events;
    }
}

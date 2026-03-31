#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "arena.h"
#include "attribute.h"
#include "entity.h"
#include "game.h"
#include "rule.h"
#include "test_helpers.h"

#include "toml.h"

#include <stdlib.h>
#include <string.h>

/* ---- FlagSet tests ---- */

void test_flag_set_and_get(void)
{
    FlagSet flags = {0};
    TEST_ASSERT_FALSE(flag_get(&flags, "chest_opened"));

    flag_set(NULL, &flags, "chest_opened");
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    test_flag_set_free(&flags);
}

void test_flag_clear(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "door_locked");
    TEST_ASSERT_TRUE(flag_get(&flags, "door_locked"));

    flag_clear(NULL, &flags, "door_locked");
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    test_flag_set_free(&flags);
}

void test_flag_unset_returns_false(void)
{
    FlagSet flags = {0};
    TEST_ASSERT_FALSE(flag_get(&flags, "never_set"));
    test_flag_set_free(&flags);
}

void test_flag_set_idempotent(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "test_flag");
    flag_set(NULL, &flags, "test_flag");
    TEST_ASSERT_EQUAL_INT(1, flags.names.count);
    test_flag_set_free(&flags);
}

void test_flag_clear_nonexistent(void)
{
    FlagSet flags = {0};
    flag_clear(NULL, &flags, "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, flags.names.count);
    test_flag_set_free(&flags);
}

/* ---- Trigger parsing tests ---- */

void test_trigger_parse_interact(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, NULL, &trigger, "interact"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, trigger.type);
}

void test_trigger_parse_enter(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, NULL, &trigger, "enter"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ENTER, trigger.type);
}

void test_trigger_parse_on_spawn(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, NULL, &trigger, "on_spawn"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ON_SPAWN, trigger.type);
}

void test_trigger_parse_event(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, NULL, &trigger, "event:boss_defeated"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, trigger.type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", trigger.argument.ptr);
    str_free(NULL, &trigger.argument);
}

void test_trigger_parse_attr_changed(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, NULL, &trigger, "attr_changed:health"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ATTR_CHANGED, trigger.type);
    TEST_ASSERT_EQUAL_STRING("health", trigger.argument.ptr);
    str_free(NULL, &trigger.argument);
}

void test_trigger_parse_unknown(void)
{
    Trigger trigger;
    TEST_ASSERT_FALSE(trigger_parse(&ctx, NULL, &trigger, "nonexistent"));
}

/* ---- Condition parsing tests ---- */

void test_condition_parse_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(COND_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_not_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "not_flag:boss_alive"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("boss_alive", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_attr_truthy(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "self.attr:is_locked"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("is_locked", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_attr_short_form(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "attr:visible"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("visible", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_not_attr(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "not_attr:dead"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("dead", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_attr_less_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "attr:health<10"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_LT, condition.type);
    TEST_ASSERT_EQUAL_STRING("health", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, condition.compare_value);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_attr_greater_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "attr:speed>5"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_GT, condition.type);
    TEST_ASSERT_EQUAL_STRING("speed", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, condition.compare_value);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_attr_equals(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "attr:level==3"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_EQ, condition.type);
    TEST_ASSERT_EQUAL_STRING("level", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.0F, condition.compare_value);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_has_item(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, NULL, &condition, "has_item:key"));
    TEST_ASSERT_EQUAL_INT(COND_HAS_ITEM, condition.type);
    TEST_ASSERT_EQUAL_STRING("key", condition.argument.ptr);
    str_free(NULL, &condition.argument);
}

void test_condition_parse_unknown(void)
{
    Condition condition;
    TEST_ASSERT_FALSE(condition_parse(&ctx, NULL, &condition, "garbage_condition"));
}

/* ---- Action parsing tests ---- */

static bool parse_action_str(ActionNode *node, const char *str)
{
    char buf[MAX_ARG * 4];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    toml_datum_t value = {.ok = 1};
    value.u.s = buf;
    return action_node_parse(&ctx, NULL, node, value);
}

void test_action_parse_set_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", action.argument.ptr);
    str_free(NULL, &action.argument);
}

void test_action_parse_clear_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "clear_flag:door_locked"));
    TEST_ASSERT_EQUAL_INT(ACTION_CLEAR_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("door_locked", action.argument.ptr);
    str_free(NULL, &action.argument);
}

void test_action_parse_set_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_attr:self.is_locked,false"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.is_locked", action.argument.ptr);
    TEST_ASSERT_EQUAL_STRING("false", action.second_argument.ptr);
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
}

void test_action_parse_add_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "add_attr:root.health,-2"));
    TEST_ASSERT_EQUAL_INT(ACTION_ADD_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("root.health", action.argument.ptr);
    TEST_ASSERT_EQUAL_STRING("-2", action.second_argument.ptr);
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
}

void test_action_parse_toggle_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "toggle_attr:self.visible"));
    TEST_ASSERT_EQUAL_INT(ACTION_TOGGLE_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.visible", action.argument.ptr);
    str_free(NULL, &action.argument);
}

void test_action_parse_destroy(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "destroy"));
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, action.type);
}

void test_action_parse_fire_event(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "fire_event:boss_defeated"));
    TEST_ASSERT_EQUAL_INT(ACTION_FIRE_EVENT, action.type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", action.argument.ptr);
    str_free(NULL, &action.argument);
}

void test_action_parse_unknown(void)
{
    ActionNode action;
    TEST_ASSERT_FALSE(parse_action_str(&action, "unknown_action:foo"));
}

/* ---- Trigger matching tests ---- */

void test_trigger_matches_simple(void)
{
    Trigger trigger = {.type = TRIGGER_INTERACT};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    TEST_ASSERT_TRUE(trigger_matches(&trigger, &event));
}

void test_trigger_no_match_different_type(void)
{
    Trigger trigger = {.type = TRIGGER_INTERACT};
    TriggerEvent event = {.type = TRIGGER_ENTER, .entity_index = 0};
    TEST_ASSERT_FALSE(trigger_matches(&trigger, &event));
}

void test_trigger_matches_event_with_argument(void)
{
    Trigger trigger = {.type = TRIGGER_EVENT};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &trigger.argument, "boss_defeated"));

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &event.argument, "boss_defeated"));

    TEST_ASSERT_TRUE(trigger_matches(&trigger, &event));
    str_free(NULL, &trigger.argument);
    str_free(NULL, &event.argument);
}

void test_trigger_no_match_event_wrong_argument(void)
{
    Trigger trigger = {.type = TRIGGER_EVENT};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &trigger.argument, "boss_defeated"));

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &event.argument, "door_opened"));

    TEST_ASSERT_FALSE(trigger_matches(&trigger, &event));
    str_free(NULL, &trigger.argument);
    str_free(NULL, &event.argument);
}

/* ---- Condition evaluation tests ---- */

void test_condition_flag_true(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "test_flag");

    Condition condition = {.type = COND_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "test_flag"));

    Entity entity = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    test_flag_set_free(&flags);
}

void test_condition_flag_false(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "test_flag"));

    Entity entity = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    test_flag_set_free(&flags);
}

void test_condition_not_flag(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_NOT_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "test_flag"));

    Entity entity = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    test_flag_set_free(&flags);
}

void test_condition_attr_truthy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(NULL, &entity.attrs, "is_locked", true);

    Condition condition = {.type = COND_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "is_locked"));

    FlagSet flags = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_condition_attr_falsy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(NULL, &entity.attrs, "is_locked", false);

    Condition condition = {.type = COND_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "is_locked"));

    FlagSet flags = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_condition_attr_missing(void)
{
    Entity entity = {0};

    Condition condition = {.type = COND_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "nonexistent"));

    FlagSet flags = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    test_flag_set_free(&flags);
}

void test_condition_attr_less_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(NULL, &entity.attrs, "health", 5);

    Condition condition = {.type = COND_ATTR_LT, .compare_value = 10.0F};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "health"));

    FlagSet flags = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_condition_attr_greater_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(NULL, &entity.attrs, "speed", 15);

    Condition condition = {.type = COND_ATTR_GT, .compare_value = 10.0F};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &condition.argument, "speed"));

    FlagSet flags = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(NULL, &condition.argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_condition_and_logic_all_pass(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "flag_a");
    flag_set(NULL, &flags, "flag_b");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &conditions[0].argument, "flag_a"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &conditions[1].argument, "flag_b"));

    Entity entity = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_TRUE(conditions_evaluate(conditions, 2, context));
    str_free(NULL, &conditions[0].argument);
    str_free(NULL, &conditions[1].argument);
    test_flag_set_free(&flags);
}

void test_condition_and_logic_one_fails(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "flag_a");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &conditions[0].argument, "flag_a"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &conditions[1].argument, "flag_b"));

    Entity entity = {0};
    const AttrSet *entity_defs[] = {NULL};
    ConditionContext context = {.entity = &entity, .flags = &flags, .entity_defaults = entity_defs};
    TEST_ASSERT_FALSE(conditions_evaluate(conditions, 2, context));
    str_free(NULL, &conditions[0].argument);
    str_free(NULL, &conditions[1].argument);
    test_flag_set_free(&flags);
}

/* ---- Action execution tests ---- */

void test_action_set_flag_executes(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_SET_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "chest_opened"));

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    str_free(NULL, &action.argument);
    test_flag_set_free(&flags);
}

void test_action_clear_flag_executes(void)
{
    FlagSet flags = {0};
    flag_set(NULL, &flags, "door_locked");
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_CLEAR_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "door_locked"));

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    str_free(NULL, &action.argument);
    test_flag_set_free(&flags);
}

void test_action_set_attr_bool(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    (void)attr_set_bool(NULL, &entity.attrs, "is_locked", true);

    ActionNode action = {.type = ACTION_SET_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "is_locked"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "false"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));

    const Attribute *attr = attr_get(&entity.attrs, "is_locked");
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr->type);
    TEST_ASSERT_FALSE(attr->value.b);
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_action_set_attr_int(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_SET_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "health"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "42"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&entity.attrs, "health", 0));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_action_add_attr(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    (void)attr_set_int(NULL, &entity.attrs, "health", 10);

    ActionNode action = {.type = ACTION_ADD_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "health"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "-3"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(7, attr_get_int(&entity.attrs, "health", 0));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_action_toggle_attr(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    (void)attr_set_bool(NULL, &entity.attrs, "visible", true);

    ActionNode action = {.type = ACTION_TOGGLE_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "visible"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_FALSE(attr_get_bool(&entity.attrs, "visible", true));
    str_free(NULL, &action.argument);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
}

void test_action_destroy(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_DESTROY};

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_FALSE(attr_get_bool(&entity.attrs, "active", true));
    test_flag_set_free(&flags);
    test_attr_set_free(&entity.attrs);
}

void test_action_fire_event_queues(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_FIRE_EVENT};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "boss_defeated"));

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(1, queue.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, queue.events[0].type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", queue.events[0].argument.ptr);
    str_free(NULL, &action.argument);
    test_flag_set_free(&flags);
}

void test_action_execution_order(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};

    ActionNode actions[2] = {
        {.type = ACTION_SET_FLAG},
        {.type = ACTION_SET_FLAG},
    };
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &actions[0].argument, "first"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &actions[1].argument, "second"));

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    (void)action_node_execute(&ctx, NULL, &actions[0], context);
    (void)action_node_execute(&ctx, NULL, &actions[1], context);

    TEST_ASSERT_TRUE(flag_get(&flags, "first"));
    TEST_ASSERT_TRUE(flag_get(&flags, "second"));
    TEST_ASSERT_EQUAL_INT(2, flags.names.count);
    str_free(NULL, &actions[0].argument);
    str_free(NULL, &actions[1].argument);
    test_flag_set_free(&flags);
}

/* ---- TOML round-trip parsing tests ---- */

void test_rules_parse_from_toml(void)
{
    static const char *toml_str = "name = \"chest\"\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "conditions = [\"flag:has_key\"]\n"
                                  "actions = [\"set_flag:chest_opened\", \"destroy\"]\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator alloc = allocator_arena(&ctx, &arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&ctx, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(1, rules.count);

    const Rule *rule = &rules.data[0];
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rule->trigger.type);
    TEST_ASSERT_EQUAL_INT(1, rule->conditions.count);
    TEST_ASSERT_EQUAL_INT(COND_FLAG, rule->conditions.data[0].type);
    TEST_ASSERT_EQUAL_STRING("has_key", rule->conditions.data[0].argument.ptr);
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, rule->action_tree.nodes.data[0].type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", rule->action_tree.nodes.data[0].argument.ptr);
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, rule->action_tree.nodes.data[1].type);

    toml_free(root);
    arena_free(&arena);
}

void test_rules_parse_no_rules(void)
{
    static const char *toml_str = "name = \"tree\"\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator alloc = allocator_arena(&ctx, &arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&ctx, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(0, rules.count);

    toml_free(root);
    arena_free(&arena);
}

void test_rules_parse_multiple_rules(void)
{
    static const char *toml_str = "name = \"door\"\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "actions = [\"set_flag:opened\"]\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"event:reset\"\n"
                                  "actions = [\"clear_flag:opened\"]\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator alloc = allocator_arena(&ctx, &arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&ctx, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(2, rules.count);

    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rules.data[0].trigger.type);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, rules.data[1].trigger.type);
    TEST_ASSERT_EQUAL_STRING("reset", rules.data[1].trigger.argument.ptr);

    toml_free(root);
    arena_free(&arena);
}

/* ---- Evaluation loop tests ---- */

void test_evaluate_interact_sets_flag(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &blueprint.attrs, (AttrStringPair){.name = "name", .value = "chest"}));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator rule_alloc = allocator_arena(&ctx, &arena);

    Rule *rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    ActionNode node_interact = {.type = ACTION_SET_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &node_interact.argument, "chest_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&rule->action_tree.nodes, node_interact, &rule_alloc));

    TEST_ASSERT_TRUE(vec_rule_push(&blueprint.rules, *rule, &rule_alloc));

    Entity entity = {0};
    entity.id = 0;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entity.blueprint_name, "chest"));

    map_entity_ruleset rule_table = {0};
    Allocator heap_alloc = allocator_heap();
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entity.id, blueprint.rules, &heap_alloc));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    const AttrSet *defaults_array[] = {&blueprint.attrs};

    rules_evaluate_batch(&ctx, NULL, &entity, 1, &event, 1, &flags, &global_vars, &rule_table, NULL, NULL,
                         defaults_array);
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    attr_set_free(NULL, &blueprint.attrs);
    str_free(NULL, &entity.blueprint_name);
    map_entity_ruleset_free(&rule_table, &heap_alloc);
    test_flag_set_free(&flags);
    test_attr_set_free(&global_vars);
}

void test_evaluate_condition_blocks_action(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &blueprint.attrs, (AttrStringPair){.name = "name", .value = "chest"}));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator rule_alloc = allocator_arena(&ctx, &arena);

    Rule *rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    Condition cond_blocked = {.type = COND_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &cond_blocked.argument, "has_key"));
    TEST_ASSERT_TRUE(vec_condition_push(&rule->conditions, cond_blocked, &rule_alloc));
    ActionNode node_blocked = {.type = ACTION_SET_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &node_blocked.argument, "chest_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&rule->action_tree.nodes, node_blocked, &rule_alloc));

    TEST_ASSERT_TRUE(vec_rule_push(&blueprint.rules, *rule, &rule_alloc));

    Entity entity = {0};
    entity.id = 0;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entity.blueprint_name, "chest"));

    map_entity_ruleset rule_table = {0};
    Allocator heap_alloc = allocator_heap();
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entity.id, blueprint.rules, &heap_alloc));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    const AttrSet *defaults_array[] = {&blueprint.attrs};

    rules_evaluate_batch(&ctx, NULL, &entity, 1, &event, 1, &flags, &global_vars, &rule_table, NULL, NULL,
                         defaults_array);
    TEST_ASSERT_FALSE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    attr_set_free(NULL, &blueprint.attrs);
    str_free(NULL, &entity.blueprint_name);
    map_entity_ruleset_free(&rule_table, &heap_alloc);
    test_flag_set_free(&flags);
    test_attr_set_free(&global_vars);
}

void test_evaluate_fire_event_cascading(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    Allocator rule_alloc = allocator_arena(&ctx, &arena);

    Blueprint bp_switch = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &bp_switch.attrs, (AttrStringPair){.name = "name", .value = "switch"}));

    Rule *switch_rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    memset(switch_rule, 0, sizeof(*switch_rule));
    switch_rule->trigger.type = TRIGGER_INTERACT;
    ActionNode switch_node = {.type = ACTION_FIRE_EVENT};
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &switch_node.argument, "switch_pulled"));
    TEST_ASSERT_TRUE(vec_action_node_push(&switch_rule->action_tree.nodes, switch_node, &rule_alloc));
    TEST_ASSERT_TRUE(vec_rule_push(&bp_switch.rules, *switch_rule, &rule_alloc));

    Blueprint bp_door = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &bp_door.attrs, (AttrStringPair){.name = "name", .value = "door"}));

    Rule *door_rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    memset(door_rule, 0, sizeof(*door_rule));
    door_rule->trigger.type = TRIGGER_EVENT;
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &door_rule->trigger.argument, "switch_pulled"));
    ActionNode door_node = {.type = ACTION_SET_FLAG};
    TEST_ASSERT_TRUE(str_from_cstr(&rule_alloc, &door_node.argument, "door_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&door_rule->action_tree.nodes, door_node, &rule_alloc));
    TEST_ASSERT_TRUE(vec_rule_push(&bp_door.rules, *door_rule, &rule_alloc));

    Entity entities[2] = {0};
    entities[0].id = 0;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entities[0].blueprint_name, "switch"));
    entities[1].id = 1;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entities[1].blueprint_name, "door"));

    map_entity_ruleset rule_table = {0};
    Allocator heap_alloc = allocator_heap();
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entities[0].id, bp_switch.rules, &heap_alloc));
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entities[1].id, bp_door.rules, &heap_alloc));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    const AttrSet *defaults_array[] = {&bp_switch.attrs, &bp_door.attrs};

    rules_evaluate_batch(&ctx, NULL, entities, 2, &event, 1, &flags, &global_vars, &rule_table, NULL, NULL,
                         defaults_array);
    TEST_ASSERT_TRUE(flag_get(&flags, "door_opened"));

    arena_free(&arena);
    attr_set_free(NULL, &bp_switch.attrs);
    attr_set_free(NULL, &bp_door.attrs);
    str_free(NULL, &entities[0].blueprint_name);
    str_free(NULL, &entities[1].blueprint_name);
    map_entity_ruleset_free(&rule_table, &heap_alloc);
    test_flag_set_free(&flags);
    test_attr_set_free(&global_vars);
}

/* ---- Variable system tests ---- */

void test_var_set_local(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "damage"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "42"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&local_vars, "damage", 0));
    TEST_ASSERT_NULL(attr_get(&global_vars, "damage"));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &local_vars);
    test_flag_set_free(&flags);
    test_attr_set_free(&local_vars);
}

void test_var_set_global(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "global.score"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "100"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(100, attr_get_int(&global_vars, "score", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars, "score"));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &global_vars);
    test_flag_set_free(&flags);
    test_attr_set_free(&local_vars);
}

void test_var_condition_truthy(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(NULL, &local_vars, "active", 1);

    Condition cond = {.type = COND_VAR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &cond.argument, "active"));

    const AttrSet *var_defs[] = {NULL};
    ConditionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .entity_defaults = var_defs,
    };
    TEST_ASSERT_TRUE(conditions_evaluate(&cond, 1, context));
    str_free(NULL, &cond.argument);
    attr_set_free(NULL, &local_vars);
    test_flag_set_free(&flags);
    test_attr_set_free(&local_vars);
}

void test_var_condition_falsy_when_unset(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    Condition cond = {.type = COND_VAR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &cond.argument, "missing"));

    const AttrSet *var_defs2[] = {NULL};
    ConditionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .entity_defaults = var_defs2,
    };
    TEST_ASSERT_FALSE(conditions_evaluate(&cond, 1, context));
    str_free(NULL, &cond.argument);
    test_flag_set_free(&flags);
    test_attr_set_free(&local_vars);
}

void test_var_substitution_in_set_attr(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(NULL, &local_vars, "amount", 5);

    ActionNode action = {.type = ACTION_SET_ATTR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "health"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "$amount"));

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .entity_defaults = (const AttrSet *[]){NULL},
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context));
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&entity.attrs, "health", 0));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &local_vars);
    attr_set_free(NULL, &entity.attrs);
    test_flag_set_free(&flags);
    test_attr_set_free(&local_vars);
}

void test_local_var_scoped_per_rule(void)
{
    FlagSet flags = {0};
    TriggerEventQueue queue = {0};
    Entity entity = {0};
    AttrSet local_vars_a = {0};
    AttrSet local_vars_b = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.argument, "temp"));
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &action.second_argument, "99"));

    ActionContext context_a = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars_a,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, NULL, &action, context_a));
    TEST_ASSERT_EQUAL_INT(99, attr_get_int(&local_vars_a, "temp", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars_b, "temp"));
    str_free(NULL, &action.argument);
    str_free(NULL, &action.second_argument);
    attr_set_free(NULL, &local_vars_a);
    test_flag_set_free(&flags);
    test_attr_set_free(&global_vars);
}

/* ---- Integration: blueprint with rules parsed from gamedata ---- */

static Texture2D rule_test_dummy_texture;

static Texture2D *rule_test_dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &rule_test_dummy_texture;
}

static const char *rule_test_gamedata = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [-5, 6]\n"
                                        "collision_size = [10, 10]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[blueprint]]\n"
                                        "name = \"chest\"\n"
                                        "texture = \"chest.png\"\n"
                                        "src = [0, 0, 16, 16]\n"
                                        "collision_offset = [0, 0]\n"
                                        "collision_size = [16, 16]\n"
                                        "\n"
                                        "[[blueprint.rule]]\n"
                                        "trigger = \"interact\"\n"
                                        "actions = [\"set_flag:chest_opened\"]\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"test\"\n"
                                        "size = [320, 240]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [160, 120]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"chest\"\n"
                                        "pos = [165, 120]\n";

void test_integration_interact_rule(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = rule_test_gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(state.gamedata_loaded);
    TEST_ASSERT_EQUAL_INT(2, state.current_level.entities.count);

    const Blueprint *chest_bp = blueprint_find(&state.blueprints, "chest");
    TEST_ASSERT_NOT_NULL(chest_bp);
    TEST_ASSERT_EQUAL_INT(1, chest_bp->rules.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, chest_bp->rules.data[0].trigger.type);

    TEST_ASSERT_FALSE(flag_get(&state.flags, "chest_opened"));

    InputState input = {0};
    input.buttons[0] = true;
    game_update(&ctx, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(flag_get(&state.flags, "chest_opened"));

    game_free(&ctx, &state);
}

void test_integration_condition_blocks_interact(void)
{
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"player\"\n"
                                  "texture = \"player.png\"\n"
                                  "src = [0, 0, 32, 32]\n"
                                  "collision_offset = [-5, 6]\n"
                                  "collision_size = [10, 10]\n"
                                  "behavior = \"player\"\n"
                                  "speed = 80\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"locked_chest\"\n"
                                  "texture = \"chest.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "conditions = [\"flag:has_key\"]\n"
                                  "actions = [\"set_flag:chest_opened\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"player\"\n"
                                  "pos = [160, 120]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"locked_chest\"\n"
                                  "pos = [165, 120]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    InputState input = {0};
    input.buttons[0] = true;
    game_update(&ctx, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_FALSE(flag_get(&state.flags, "chest_opened"));

    input.buttons[0] = false;
    game_update(&ctx, &state, input, 1.0F / 60.0F);

    Allocator arena_alloc = allocator_arena(&ctx, &state.gamedata_arena);
    flag_set(&arena_alloc, &state.flags, "has_key");
    input.buttons[0] = true;
    game_update(&ctx, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(flag_get(&state.flags, "chest_opened"));

    game_free(&ctx, &state);
}

/* ---- Integration: for_each control flow ---- */

void test_integration_for_each_no_bind_iterates_all_entities(void)
{
    /* counter blueprint: on_spawn → for_each entities (no bind) → add_attr:self.count,1
     * Three entities in the level: counter, target_a, target_b (all count = 0).
     * After load, every entity should have count = 1 (one iteration each). */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"counter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [{ for_each = \"entities\", do = [\"add_attr:self.count,1\"] }]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"target\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"counter\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"target\"\n"
                                  "pos = [50, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"target\"\n"
                                  "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* All three entities must have count = 1 */
    for (int entity_index = 0; entity_index < state.current_level.entities.count; entity_index++) {
        const Entity *entity = &state.current_level.entities.data[entity_index];
        TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&entity->attrs, NULL, "count", 0.0F));
    }

    game_free(&ctx, &state);
}

void test_integration_for_each_condition_filter(void)
{
    /* Only entities with is_enemy attr get hit_count incremented.
     * Fixture: one watcher (has the rule), one enemy, one bystander.
     * After load: enemy.hit_count = 1, bystander.hit_count = 0. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"watcher\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [{ for_each = \"entities\", conditions = [\"attr:is_enemy\"], "
                                  "do = [\"add_attr:self.hit_count,1\"] }]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"enemy\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "is_enemy = 1\n"
                                  "hit_count = 0\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"bystander\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "hit_count = 0\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"watcher\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"enemy\"\n"
                                  "pos = [50, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"bystander\"\n"
                                  "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_EQUAL_INT(3, state.current_level.entities.count);
    /* entity 1 = enemy, entity 2 = bystander */
    const Entity *enemy = &state.current_level.entities.data[1];
    const Entity *bystander = &state.current_level.entities.data[2];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&enemy->attrs, NULL, "hit_count", 0.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&bystander->attrs, NULL, "hit_count", 0.0F));

    game_free(&ctx, &state);
}

void test_integration_for_each_bind_mode(void)
{
    /* Bind mode: self stays as rule owner; bound variable names the iterated entity.
     * Fixture: "marker" entity has on_spawn rule, for_each bind="item", add_attr:item.tagged,1.
     * All entities (including marker itself — no self-exclusion) get tagged = 1. */
    static const char *gamedata =
        "[[blueprint]]\n"
        "name = \"marker\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "tagged = 0\n"
        "\n"
        "[[blueprint.rule]]\n"
        "trigger = \"on_spawn\"\n"
        "actions = [{ for_each = \"entities\", bind = \"item\", do = [\"add_attr:item.tagged,1\"] }]\n"
        "\n"
        "[[blueprint]]\n"
        "name = \"thing\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "tagged = 0\n"
        "\n"
        "[[level]]\n"
        "name = \"test\"\n"
        "size = [320, 240]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"marker\"\n"
        "pos = [10, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"thing\"\n"
        "pos = [50, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"thing\"\n"
        "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* All three entities must have tagged = 1 (bind mode still includes self) */
    for (int entity_index = 0; entity_index < state.current_level.entities.count; entity_index++) {
        const Entity *entity = &state.current_level.entities.data[entity_index];
        TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&entity->attrs, NULL, "tagged", 0.0F));
    }

    game_free(&ctx, &state);
}

/* ---- Integration: subroutines ---- */

void test_integration_subroutine_call(void)
{
    /* A subroutine sets a flag. A blueprint rule calls it.
     * After load (on_spawn), the flag must be set. */
    static const char *gamedata = "[[subroutine]]\n"
                                  "name = \"mark_visited\"\n"
                                  "actions = [\"set_flag:visited\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"beacon\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:mark_visited\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"beacon\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(flag_get(&state.flags, "visited"));

    game_free(&ctx, &state);
}

void test_integration_subroutine_inherits_self(void)
{
    /* Subroutine uses self.attr — must operate on the calling entity. */
    static const char *gamedata = "[[subroutine]]\n"
                                  "name = \"increment\"\n"
                                  "actions = [\"add_attr:self.count,1\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"counter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:increment\", \"call:increment\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"counter\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Called twice — count must be 2 */
    const Entity *counter = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(2, (int)attr_get_scoped_float(&counter->attrs, NULL, "count", 0.0F));

    game_free(&ctx, &state);
}

/* ---- Integration: timers ---- */

void test_integration_timer_oneshot_fires_once(void)
{
    /* Entity creates a one-shot timer on_spawn; after one tick past duration it fires and
     * sets a flag. A second tick must NOT fire again. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer:tick,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer:tick\"\n"
                                  "actions = [\"add_attr:self.fired_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "fired_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* 1 timer created, none fired yet */
    TEST_ASSERT_EQUAL_INT(1, state.timers.count);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&thing->attrs, NULL, "fired_count", 0.0F));

    /* Advance past duration — timer fires once */
    game_update(&ctx, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(0, state.timers.count);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, NULL, "fired_count", 0.0F));

    /* Second tick — no timer left, count stays at 1 */
    game_update(&ctx, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, NULL, "fired_count", 0.0F));

    game_free(&ctx, &state);
}

void test_integration_timer_periodic_fires_repeatedly(void)
{
    /* Periodic timer fires every 0.5 s — advance 1.1 s and expect 2 fires. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer_periodic:pulse,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer_periodic:pulse\"\n"
                                  "actions = [\"add_attr:self.pulse_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "pulse_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Advance 0.6 s — one fire */
    game_update(&ctx, &state, (InputState){0}, 0.6F);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, NULL, "pulse_count", 0.0F));

    /* Advance another 0.6 s — second fire; timer still alive */
    game_update(&ctx, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(2, (int)attr_get_scoped_float(&thing->attrs, NULL, "pulse_count", 0.0F));
    TEST_ASSERT_EQUAL_INT(1, state.timers.count);

    game_free(&ctx, &state);
}

void test_integration_timer_destroy_cancels(void)
{
    /* Entity creates a timer on_spawn and destroys it on a separate event.
     * After the destroy event no fire should occur even past the duration. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer:tick,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"event:cancel\"\n"
                                  "actions = [\"destroy_timer:tick\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer:tick\"\n"
                                  "actions = [\"add_attr:self.fired_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "fired_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_EQUAL_INT(1, state.timers.count);

    /* Fire cancel event — timer removed */
    TriggerEvent cancel = {.type = TRIGGER_EVENT, .entity_index = -1};
    Allocator heap_alloc = allocator_heap();
    (void)str_from_cstr(&heap_alloc, &cancel.argument, "cancel");
    int cancel_count = state.current_level.entities.count;
    const AttrSet *cancel_defaults[64];
    for (int index = 0; index < cancel_count; index++) {
        cancel_defaults[index] = entity_resolve_defaults(&state, state.current_level.entities.data[index].id);
    }
    Allocator rule_alloc = allocator_arena(&ctx, &state.gamedata_arena);
    rules_evaluate_batch(&ctx, &rule_alloc, state.current_level.entities.data, cancel_count, &cancel, 1, &state.flags,
                         &state.vars, &state.rule_table, &state.subroutines, &state.timers, cancel_defaults);
    str_free(&heap_alloc, &cancel.argument);

    TEST_ASSERT_EQUAL_INT(0, state.timers.count);

    /* Advance past duration — no fire */
    game_update(&ctx, &state, (InputState){0}, 0.6F);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&thing->attrs, NULL, "fired_count", 0.0F));

    game_free(&ctx, &state);
}

/* ---- Integration: on_destroy trigger ---- */

void test_integration_on_destroy_fires(void)
{
    /* Entity destroys itself on_spawn; on_destroy rule must run while it is inactive. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"destroy\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_destroy\"\n"
                                  "actions = [\"set_flag:thing_destroyed\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Entity must be inactive and the on_destroy flag must be set */
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_FALSE(attr_get_bool(&thing->attrs, "active", true));
    TEST_ASSERT_TRUE(flag_get(&state.flags, "thing_destroyed"));

    game_free(&ctx, &state);
}

/* ---- Integration: defeat trigger ---- */

void test_integration_defeat_fires_when_health_drops_to_zero(void)
{
    /* Entity starts with health=5; on_spawn subtracts 10 → health=-5 (crosses 0 → defeat). */
    /* health = [current, max] is the blueprint format; parse_health stores it as health=5 */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"enemy\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "health = [5, 10]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"add_attr:self.health,-10\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"defeat\"\n"
                                  "actions = [\"set_flag:enemy_defeated\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"enemy\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(flag_get(&state.flags, "enemy_defeated"));

    game_free(&ctx, &state);
}

/* ---- Integration: collide trigger ---- */

void test_integration_collide_fires_on_overlap(void)
{
    /* Two solid entities placed at the same position — they overlap immediately.
     * After the first game_update both should receive TRIGGER_COLLIDE.
     * The "rock" blueprint has a collide rule that sets a flag. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"rock\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"collide\"\n"
                                  "actions = [\"set_flag:rock_hit\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"barrel\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"rock\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"barrel\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* No collide event yet — prev_solid_collisions initialised to false */
    TEST_ASSERT_FALSE(flag_get(&state.flags, "rock_hit"));

    /* First update — overlap detected for the first time → fire */
    game_update(&ctx, &state, (InputState){0}, 0.016F);
    TEST_ASSERT_TRUE(flag_get(&state.flags, "rock_hit"));

    game_free(&ctx, &state);
}

void test_integration_subroutine_missing_is_soft_fail(void)
{
    /* Calling a non-existent subroutine must not crash — subsequent actions still run. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:nonexistent\", \"add_attr:self.count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* count must be 1 — add_attr ran after the failed call: */
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, NULL, "count", 0.0F));

    game_free(&ctx, &state);
}

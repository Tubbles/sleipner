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

    flag_set(&ctx, &flags, "chest_opened");
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    test_flag_set_free(&ctx, &flags);
}

void test_flag_clear(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "door_locked");
    TEST_ASSERT_TRUE(flag_get(&flags, "door_locked"));

    flag_clear(&ctx, &flags, "door_locked");
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    test_flag_set_free(&ctx, &flags);
}

void test_flag_unset_returns_false(void)
{
    FlagSet flags = {0};
    TEST_ASSERT_FALSE(flag_get(&flags, "never_set"));
    test_flag_set_free(&ctx, &flags);
}

void test_flag_set_idempotent(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "test_flag");
    flag_set(&ctx, &flags, "test_flag");
    TEST_ASSERT_EQUAL_INT(1, flags.names.count);
    test_flag_set_free(&ctx, &flags);
}

void test_flag_clear_nonexistent(void)
{
    FlagSet flags = {0};
    flag_clear(&ctx, &flags, "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, flags.names.count);
    test_flag_set_free(&ctx, &flags);
}

/* ---- Trigger parsing tests ---- */

void test_trigger_parse_interact(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, &trigger, "interact"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, trigger.type);
}

void test_trigger_parse_enter(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, &trigger, "enter"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ENTER, trigger.type);
}

void test_trigger_parse_on_spawn(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, &trigger, "on_spawn"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ON_SPAWN, trigger.type);
}

void test_trigger_parse_event(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, &trigger, "event:boss_defeated"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, trigger.type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", trigger.argument);
}

void test_trigger_parse_attr_changed(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&ctx, &trigger, "attr_changed:health"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ATTR_CHANGED, trigger.type);
    TEST_ASSERT_EQUAL_STRING("health", trigger.argument);
}

void test_trigger_parse_unknown(void)
{
    Trigger trigger;
    TEST_ASSERT_FALSE(trigger_parse(&ctx, &trigger, "nonexistent"));
}

/* ---- Condition parsing tests ---- */

void test_condition_parse_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(COND_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", condition.argument);
}

void test_condition_parse_not_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "not_flag:boss_alive"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("boss_alive", condition.argument);
}

void test_condition_parse_attr_truthy(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "self.attr:is_locked"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("is_locked", condition.argument);
}

void test_condition_parse_attr_short_form(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "attr:visible"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("visible", condition.argument);
}

void test_condition_parse_not_attr(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "not_attr:dead"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("dead", condition.argument);
}

void test_condition_parse_attr_less_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "attr:health<10"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_LT, condition.type);
    TEST_ASSERT_EQUAL_STRING("health", condition.argument);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, condition.compare_value);
}

void test_condition_parse_attr_greater_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "attr:speed>5"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_GT, condition.type);
    TEST_ASSERT_EQUAL_STRING("speed", condition.argument);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, condition.compare_value);
}

void test_condition_parse_attr_equals(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "attr:level==3"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_EQ, condition.type);
    TEST_ASSERT_EQUAL_STRING("level", condition.argument);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.0F, condition.compare_value);
}

void test_condition_parse_has_item(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&ctx, &condition, "has_item:key"));
    TEST_ASSERT_EQUAL_INT(COND_HAS_ITEM, condition.type);
    TEST_ASSERT_EQUAL_STRING("key", condition.argument);
}

void test_condition_parse_unknown(void)
{
    Condition condition;
    TEST_ASSERT_FALSE(condition_parse(&ctx, &condition, "garbage_condition"));
}

/* ---- Action parsing tests ---- */

static bool parse_action_str(ActionNode *node, const char *str)
{
    char buf[MAX_ARG * 4];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    toml_datum_t value = {.ok = 1};
    value.u.s = buf;
    return action_node_parse(&ctx, node, value, NULL);
}

void test_action_parse_set_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", action.argument);
}

void test_action_parse_clear_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "clear_flag:door_locked"));
    TEST_ASSERT_EQUAL_INT(ACTION_CLEAR_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("door_locked", action.argument);
}

void test_action_parse_set_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_attr:self.is_locked,false"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.is_locked", action.argument);
    TEST_ASSERT_EQUAL_STRING("false", action.second_argument);
}

void test_action_parse_add_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "add_attr:root.health,-2"));
    TEST_ASSERT_EQUAL_INT(ACTION_ADD_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("root.health", action.argument);
    TEST_ASSERT_EQUAL_STRING("-2", action.second_argument);
}

void test_action_parse_toggle_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "toggle_attr:self.visible"));
    TEST_ASSERT_EQUAL_INT(ACTION_TOGGLE_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.visible", action.argument);
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
    TEST_ASSERT_EQUAL_STRING("boss_defeated", action.argument);
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
    strncpy(trigger.argument, "boss_defeated", MAX_ARG - 1);

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    strncpy(event.argument, "boss_defeated", MAX_ARG - 1);

    TEST_ASSERT_TRUE(trigger_matches(&trigger, &event));
}

void test_trigger_no_match_event_wrong_argument(void)
{
    Trigger trigger = {.type = TRIGGER_EVENT};
    strncpy(trigger.argument, "boss_defeated", MAX_ARG - 1);

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    strncpy(event.argument, "door_opened", MAX_ARG - 1);

    TEST_ASSERT_FALSE(trigger_matches(&trigger, &event));
}

/* ---- Condition evaluation tests ---- */

void test_condition_flag_true(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "test_flag");

    Condition condition = {.type = COND_FLAG};
    strncpy(condition.argument, "test_flag", MAX_ARG - 1);

    Entity entity = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    test_flag_set_free(&ctx, &flags);
}

void test_condition_flag_false(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_FLAG};
    strncpy(condition.argument, "test_flag", MAX_ARG - 1);

    Entity entity = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    test_flag_set_free(&ctx, &flags);
}

void test_condition_not_flag(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_NOT_FLAG};
    strncpy(condition.argument, "test_flag", MAX_ARG - 1);

    Entity entity = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    test_flag_set_free(&ctx, &flags);
}

void test_condition_attr_truthy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(&ctx, &entity.attrs, "is_locked", true);

    Condition condition = {.type = COND_ATTR};
    strncpy(condition.argument, "is_locked", MAX_ARG - 1);

    FlagSet flags = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
}

void test_condition_attr_falsy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(&ctx, &entity.attrs, "is_locked", false);

    Condition condition = {.type = COND_ATTR};
    strncpy(condition.argument, "is_locked", MAX_ARG - 1);

    FlagSet flags = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
}

void test_condition_attr_missing(void)
{
    Entity entity = {0};

    Condition condition = {.type = COND_ATTR};
    strncpy(condition.argument, "nonexistent", MAX_ARG - 1);

    FlagSet flags = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    test_flag_set_free(&ctx, &flags);
}

void test_condition_attr_less_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(&ctx, &entity.attrs, "health", 5);

    Condition condition = {.type = COND_ATTR_LT, .compare_value = 10.0F};
    strncpy(condition.argument, "health", MAX_ARG - 1);

    FlagSet flags = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
}

void test_condition_attr_greater_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(&ctx, &entity.attrs, "speed", 15);

    Condition condition = {.type = COND_ATTR_GT, .compare_value = 10.0F};
    strncpy(condition.argument, "speed", MAX_ARG - 1);

    FlagSet flags = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
}

void test_condition_and_logic_all_pass(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "flag_a");
    flag_set(&ctx, &flags, "flag_b");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    strncpy(conditions[0].argument, "flag_a", MAX_ARG - 1);
    strncpy(conditions[1].argument, "flag_b", MAX_ARG - 1);

    Entity entity = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_TRUE(conditions_evaluate(conditions, 2, context));
    test_flag_set_free(&ctx, &flags);
}

void test_condition_and_logic_one_fails(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "flag_a");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    strncpy(conditions[0].argument, "flag_a", MAX_ARG - 1);
    strncpy(conditions[1].argument, "flag_b", MAX_ARG - 1);

    Entity entity = {0};
    ConditionContext context = {.entity = &entity, .flags = &flags};
    TEST_ASSERT_FALSE(conditions_evaluate(conditions, 2, context));
    test_flag_set_free(&ctx, &flags);
}

/* ---- Action execution tests ---- */

void test_action_set_flag_executes(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    entity.active = true;

    ActionNode action = {.type = ACTION_SET_FLAG};
    strncpy(action.argument, "chest_opened", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_clear_flag_executes(void)
{
    FlagSet flags = {0};
    flag_set(&ctx, &flags, "door_locked");
    vec_trigger_event queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_CLEAR_FLAG};
    strncpy(action.argument, "door_locked", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_set_attr_bool(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    (void)attr_set_bool(&ctx, &entity.attrs, "is_locked", true);

    ActionNode action = {.type = ACTION_SET_ATTR};
    strncpy(action.argument, "is_locked", MAX_ARG - 1);
    strncpy(action.second_argument, "false", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));

    const Attribute *attr = attr_get(&entity.attrs, "is_locked");
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr->type);
    TEST_ASSERT_FALSE(attr->value.b);
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_set_attr_int(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_SET_ATTR};
    strncpy(action.argument, "health", MAX_ARG - 1);
    strncpy(action.second_argument, "42", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(42, entity_get_int(&entity, "health", 0));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_add_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    (void)attr_set_int(&ctx, &entity.attrs, "health", 10);

    ActionNode action = {.type = ACTION_ADD_ATTR};
    strncpy(action.argument, "health", MAX_ARG - 1);
    strncpy(action.second_argument, "-3", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(7, entity_get_int(&entity, "health", 0));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_toggle_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    (void)attr_set_bool(&ctx, &entity.attrs, "visible", true);

    ActionNode action = {.type = ACTION_TOGGLE_ATTR};
    strncpy(action.argument, "visible", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_FALSE(entity_get_bool(&entity, "visible", true));
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_destroy(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    entity.active = true;

    ActionNode action = {.type = ACTION_DESTROY};

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_FALSE(entity.active);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
}

void test_action_fire_event_queues(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};

    ActionNode action = {.type = ACTION_FIRE_EVENT};
    strncpy(action.argument, "boss_defeated", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(1, queue.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, queue.data[0].type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", queue.data[0].argument);
    vec_trigger_event_free(&queue, NULL);
    test_flag_set_free(&ctx, &flags);
}

void test_action_execution_order(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};

    ActionNode actions[2] = {
        {.type = ACTION_SET_FLAG},
        {.type = ACTION_SET_FLAG},
    };
    strncpy(actions[0].argument, "first", MAX_ARG - 1);
    strncpy(actions[1].argument, "second", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
    };
    (void)action_node_execute(&ctx, &actions[0], context);
    (void)action_node_execute(&ctx, &actions[1], context);

    TEST_ASSERT_TRUE(flag_get(&flags, "first"));
    TEST_ASSERT_TRUE(flag_get(&flags, "second"));
    TEST_ASSERT_EQUAL_INT(2, flags.names.count);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
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
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    RuleSet rules;
    TEST_ASSERT_TRUE(rules_parse(&ctx, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(1, rules.count);

    const Rule *rule = &rules.entries[0];
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rule->trigger.type);
    TEST_ASSERT_EQUAL_INT(1, rule->condition_count);
    TEST_ASSERT_EQUAL_INT(COND_FLAG, rule->conditions[0].type);
    TEST_ASSERT_EQUAL_STRING("has_key", rule->conditions[0].argument);
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.count);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, rule->action_tree.nodes[0].type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", rule->action_tree.nodes[0].argument);
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, rule->action_tree.nodes[1].type);

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
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    RuleSet rules;
    TEST_ASSERT_TRUE(rules_parse(&ctx, &rules, root, &arena));
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
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    RuleSet rules;
    TEST_ASSERT_TRUE(rules_parse(&ctx, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(2, rules.count);

    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rules.entries[0].trigger.type);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, rules.entries[1].trigger.type);
    TEST_ASSERT_EQUAL_STRING("reset", rules.entries[1].trigger.argument);

    toml_free(root);
    arena_free(&arena);
}

/* ---- Evaluation loop tests ---- */

void test_evaluate_interact_sets_flag(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &blueprint.name, "chest"));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    Rule *rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    ActionNode *nodes_interact =
        arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    TEST_ASSERT_NOT_NULL(nodes_interact);
    memset(nodes_interact, 0, sizeof(ActionNode));
    nodes_interact[0].type = ACTION_SET_FLAG;
    strncpy(nodes_interact[0].argument, "chest_opened", MAX_ARG - 1);
    rule->action_tree.nodes = nodes_interact;
    rule->action_tree.count = 1;

    blueprint.rules.entries = rule;
    blueprint.rules.count = 1;

    Entity entity = {0};
    entity.active = true;
    entity.blueprint = &blueprint;

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};

    rules_evaluate_batch(&ctx, &entity, 1, &event, 1, &flags, &global_vars);
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    str_free(NULL, &blueprint.name);
    test_flag_set_free(&ctx, &flags);
    test_attr_set_free(&ctx, &global_vars);
}

void test_evaluate_condition_blocks_action(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &blueprint.name, "chest"));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    Rule *rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    rule->condition_count = 1;
    rule->conditions[0].type = COND_FLAG;
    strncpy(rule->conditions[0].argument, "has_key", MAX_ARG - 1);
    ActionNode *nodes_blocked =
        arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    TEST_ASSERT_NOT_NULL(nodes_blocked);
    memset(nodes_blocked, 0, sizeof(ActionNode));
    nodes_blocked[0].type = ACTION_SET_FLAG;
    strncpy(nodes_blocked[0].argument, "chest_opened", MAX_ARG - 1);
    rule->action_tree.nodes = nodes_blocked;
    rule->action_tree.count = 1;

    blueprint.rules.entries = rule;
    blueprint.rules.count = 1;

    Entity entity = {0};
    entity.active = true;
    entity.blueprint = &blueprint;

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};

    rules_evaluate_batch(&ctx, &entity, 1, &event, 1, &flags, &global_vars);
    TEST_ASSERT_FALSE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    str_free(NULL, &blueprint.name);
    test_flag_set_free(&ctx, &flags);
    test_attr_set_free(&ctx, &global_vars);
}

void test_evaluate_fire_event_cascading(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 65536));

    Blueprint bp_switch = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &bp_switch.name, "switch"));

    Rule *switch_rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    memset(switch_rule, 0, sizeof(*switch_rule));
    switch_rule->trigger.type = TRIGGER_INTERACT;
    ActionNode *switch_nodes =
        arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    TEST_ASSERT_NOT_NULL(switch_nodes);
    memset(switch_nodes, 0, sizeof(ActionNode));
    switch_nodes[0].type = ACTION_FIRE_EVENT;
    strncpy(switch_nodes[0].argument, "switch_pulled", MAX_ARG - 1);
    switch_rule->action_tree.nodes = switch_nodes;
    switch_rule->action_tree.count = 1;
    bp_switch.rules.entries = switch_rule;
    bp_switch.rules.count = 1;

    Blueprint bp_door = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &bp_door.name, "door"));

    Rule *door_rule = arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(Rule), .alignment = _Alignof(Rule)});
    memset(door_rule, 0, sizeof(*door_rule));
    door_rule->trigger.type = TRIGGER_EVENT;
    strncpy(door_rule->trigger.argument, "switch_pulled", MAX_ARG - 1);
    ActionNode *door_nodes =
        arena_alloc(&ctx, &arena, (AllocRequest){.size = sizeof(ActionNode), .alignment = _Alignof(ActionNode)});
    TEST_ASSERT_NOT_NULL(door_nodes);
    memset(door_nodes, 0, sizeof(ActionNode));
    door_nodes[0].type = ACTION_SET_FLAG;
    strncpy(door_nodes[0].argument, "door_opened", MAX_ARG - 1);
    door_rule->action_tree.nodes = door_nodes;
    door_rule->action_tree.count = 1;
    bp_door.rules.entries = door_rule;
    bp_door.rules.count = 1;

    Entity entities[2] = {0};
    entities[0].active = true;
    entities[0].blueprint = &bp_switch;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entities[0].blueprint_name, "switch"));
    entities[1].active = true;
    entities[1].blueprint = &bp_door;
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &entities[1].blueprint_name, "door"));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};

    rules_evaluate_batch(&ctx, entities, 2, &event, 1, &flags, &global_vars);
    TEST_ASSERT_TRUE(flag_get(&flags, "door_opened"));

    arena_free(&arena);
    str_free(NULL, &bp_switch.name);
    str_free(NULL, &bp_door.name);
    str_free(NULL, &entities[0].blueprint_name);
    str_free(NULL, &entities[1].blueprint_name);
    test_flag_set_free(&ctx, &flags);
    test_attr_set_free(&ctx, &global_vars);
}

/* ---- Variable system tests ---- */

void test_var_set_local(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    strncpy(action.argument, "damage", MAX_ARG - 1);
    strncpy(action.second_argument, "42", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&local_vars, "damage", 0));
    TEST_ASSERT_NULL(attr_get(&global_vars, "damage"));
    attr_set_free(&ctx, &local_vars);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
    test_attr_set_free(&ctx, &local_vars);
}

void test_var_set_global(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    strncpy(action.argument, "global.score", MAX_ARG - 1);
    strncpy(action.second_argument, "100", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(100, attr_get_int(&global_vars, "score", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars, "score"));
    attr_set_free(&ctx, &global_vars);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
    test_attr_set_free(&ctx, &local_vars);
}

void test_var_condition_truthy(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(&ctx, &local_vars, "active", 1);

    Condition cond = {.type = COND_VAR};
    strncpy(cond.argument, "active", MAX_ARG - 1);

    ConditionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(conditions_evaluate(&cond, 1, context));
    attr_set_free(&ctx, &local_vars);
    test_flag_set_free(&ctx, &flags);
    test_attr_set_free(&ctx, &local_vars);
}

void test_var_condition_falsy_when_unset(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    Condition cond = {.type = COND_VAR};
    strncpy(cond.argument, "missing", MAX_ARG - 1);

    ConditionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_FALSE(conditions_evaluate(&cond, 1, context));
    test_flag_set_free(&ctx, &flags);
    test_attr_set_free(&ctx, &local_vars);
}

void test_var_substitution_in_set_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(&ctx, &local_vars, "amount", 5);

    ActionNode action = {.type = ACTION_SET_ATTR};
    strncpy(action.argument, "health", MAX_ARG - 1);
    strncpy(action.second_argument, "$amount", MAX_ARG - 1);

    ActionContext context = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context));
    TEST_ASSERT_EQUAL_INT(5, entity_get_int(&entity, "health", 0));
    attr_set_free(&ctx, &local_vars);
    attr_set_free(&ctx, &entity.attrs);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
    test_attr_set_free(&ctx, &local_vars);
}

void test_local_var_scoped_per_rule(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = {0};
    Entity entity = {0};
    AttrSet local_vars_a = {0};
    AttrSet local_vars_b = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    strncpy(action.argument, "temp", MAX_ARG - 1);
    strncpy(action.second_argument, "99", MAX_ARG - 1);

    ActionContext context_a = {
        .entity = &entity,
        .entities = &entity,
        .entity_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars_a,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&ctx, &action, context_a));
    TEST_ASSERT_EQUAL_INT(99, attr_get_int(&local_vars_a, "temp", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars_b, "temp"));
    attr_set_free(&ctx, &local_vars_a);
    test_flag_set_free(&ctx, &flags);
    vec_trigger_event_free(&queue, NULL);
    test_attr_set_free(&ctx, &global_vars);
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
    TEST_ASSERT_EQUAL_INT(2, state.current_level.entity_count);

    const Blueprint *chest_bp = blueprint_find(&state.blueprints, "chest");
    TEST_ASSERT_NOT_NULL(chest_bp);
    TEST_ASSERT_EQUAL_INT(1, chest_bp->rules.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, chest_bp->rules.entries[0].trigger.type);

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

    flag_set(&ctx, &state.flags, "has_key");
    input.buttons[0] = true;
    game_update(&ctx, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(flag_get(&state.flags, "chest_opened"));

    game_free(&ctx, &state);
}

#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/rule.c"        // NOLINT(bugprone-suspicious-include)
#include "blueprint.h"

DEFINE_FFF_GLOBALS;

/* debug_log stub — cannot use FAKE_VOID_FUNC_VARARG due to __attribute__((format)) conflict */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

/* TOML function fakes — rule.c references these but the pure tests never trigger them */
FAKE_VALUE_FUNC(toml_array_t *, toml_array_in, const toml_table_t *, const char *);
FAKE_VALUE_FUNC(int, toml_array_nelem, const toml_array_t *);
FAKE_VALUE_FUNC(toml_datum_t, toml_string_at, const toml_array_t *, int);
FAKE_VALUE_FUNC(toml_datum_t, toml_string_in, const toml_table_t *, const char *);
FAKE_VALUE_FUNC(toml_table_t *, toml_table_at, const toml_array_t *, int);

#include "test_heap_alloc.h"

#include <string.h>

static ErrorState test_err;
static DebugState test_dbg;
static Diag test_diag = {&test_err, &test_dbg};

void setUp(void) {}
void tearDown(void) {}

/* Helper wrappers matching what test_helpers.h provides in engine_tests */
static void test_flag_set_free_local(FlagSet *flags)
{
    flag_set_free(&test_heap_alloc, flags);
}

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

/* ---- FlagSet tests ---- */

void test_flag_set_and_get(void)
{
    FlagSet flags = {0};
    TEST_ASSERT_FALSE(flag_get(&flags, "chest_opened"));

    flag_set(&test_diag, &test_heap_alloc, &flags, "chest_opened");
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    test_flag_set_free_local(&flags);
}

void test_flag_clear(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "door_locked");
    TEST_ASSERT_TRUE(flag_get(&flags, "door_locked"));

    flag_clear(&test_heap_alloc, &flags, "door_locked");
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    test_flag_set_free_local(&flags);
}

void test_flag_unset_returns_false(void)
{
    FlagSet flags = {0};
    TEST_ASSERT_FALSE(flag_get(&flags, "never_set"));
    test_flag_set_free_local(&flags);
}

void test_flag_set_idempotent(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "test_flag");
    flag_set(&test_diag, &test_heap_alloc, &flags, "test_flag");
    TEST_ASSERT_EQUAL_INT(1, flags.names.count);
    test_flag_set_free_local(&flags);
}

void test_flag_clear_nonexistent(void)
{
    FlagSet flags = {0};
    flag_clear(&test_heap_alloc, &flags, "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, flags.names.count);
    test_flag_set_free_local(&flags);
}

/* ---- Trigger parsing tests ---- */

void test_trigger_parse_interact(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "interact"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, trigger.type);
}

void test_trigger_parse_enter(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "enter"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ENTER, trigger.type);
}

void test_trigger_parse_on_spawn(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "on_spawn"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ON_SPAWN, trigger.type);
}

void test_trigger_parse_event(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "event:boss_defeated"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, trigger.type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", trigger.argument.ptr);
    str_free(&trigger.argument);
}

void test_trigger_parse_attr_changed(void)
{
    Trigger trigger;
    TEST_ASSERT_TRUE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "attr_changed:health"));
    TEST_ASSERT_EQUAL_INT(TRIGGER_ATTR_CHANGED, trigger.type);
    TEST_ASSERT_EQUAL_STRING("health", trigger.argument.ptr);
    str_free(&trigger.argument);
}

void test_trigger_parse_unknown(void)
{
    Trigger trigger;
    TEST_ASSERT_FALSE(trigger_parse(&test_diag, &test_heap_alloc, &trigger, "nonexistent"));
}

/* ---- Condition parsing tests ---- */

void test_condition_parse_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(COND_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_not_flag(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "not_flag:boss_alive"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_FLAG, condition.type);
    TEST_ASSERT_EQUAL_STRING("boss_alive", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_attr_truthy(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "self.attr:is_locked"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("is_locked", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_attr_short_form(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "attr:visible"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("visible", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_not_attr(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "not_attr:dead"));
    TEST_ASSERT_EQUAL_INT(COND_NOT_ATTR, condition.type);
    TEST_ASSERT_EQUAL_STRING("dead", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_attr_less_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "attr:health<10"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_LT, condition.type);
    TEST_ASSERT_EQUAL_STRING("health", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, condition.compare_value);
    str_free(&condition.argument);
}

void test_condition_parse_attr_greater_than(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "attr:speed>5"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_GT, condition.type);
    TEST_ASSERT_EQUAL_STRING("speed", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, condition.compare_value);
    str_free(&condition.argument);
}

void test_condition_parse_attr_equals(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "attr:level==3"));
    TEST_ASSERT_EQUAL_INT(COND_ATTR_EQ, condition.type);
    TEST_ASSERT_EQUAL_STRING("level", condition.argument.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.0F, condition.compare_value);
    str_free(&condition.argument);
}

void test_condition_parse_has_item(void)
{
    Condition condition;
    TEST_ASSERT_TRUE(condition_parse(&test_diag, &test_heap_alloc, &condition, "has_item:key"));
    TEST_ASSERT_EQUAL_INT(COND_HAS_ITEM, condition.type);
    TEST_ASSERT_EQUAL_STRING("key", condition.argument.ptr);
    str_free(&condition.argument);
}

void test_condition_parse_unknown(void)
{
    Condition condition;
    TEST_ASSERT_FALSE(condition_parse(&test_diag, &test_heap_alloc, &condition, "garbage_condition"));
}

/* ---- Action parsing tests ---- */

static bool parse_action_str(ActionNode *node, const char *str)
{
    char buf[MAX_ARG * 4];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    toml_datum_t value = {.ok = 1};
    value.u.s = buf;
    return action_node_parse(&test_diag, &test_heap_alloc, node, value);
}

void test_action_parse_set_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_flag:chest_opened"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", action.argument.ptr);
    str_free(&action.argument);
}

void test_action_parse_clear_flag(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "clear_flag:door_locked"));
    TEST_ASSERT_EQUAL_INT(ACTION_CLEAR_FLAG, action.type);
    TEST_ASSERT_EQUAL_STRING("door_locked", action.argument.ptr);
    str_free(&action.argument);
}

void test_action_parse_set_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "set_attr:self.is_locked,false"));
    TEST_ASSERT_EQUAL_INT(ACTION_SET_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.is_locked", action.argument.ptr);
    TEST_ASSERT_EQUAL_STRING("false", action.second_argument.ptr);
    str_free(&action.argument);
    str_free(&action.second_argument);
}

void test_action_parse_add_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "add_attr:root.health,-2"));
    TEST_ASSERT_EQUAL_INT(ACTION_ADD_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("root.health", action.argument.ptr);
    TEST_ASSERT_EQUAL_STRING("-2", action.second_argument.ptr);
    str_free(&action.argument);
    str_free(&action.second_argument);
}

void test_action_parse_toggle_attr(void)
{
    ActionNode action;
    TEST_ASSERT_TRUE(parse_action_str(&action, "toggle_attr:self.visible"));
    TEST_ASSERT_EQUAL_INT(ACTION_TOGGLE_ATTR, action.type);
    TEST_ASSERT_EQUAL_STRING("self.visible", action.argument.ptr);
    str_free(&action.argument);
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
    str_free(&action.argument);
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
    trigger.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&trigger.argument, "boss_defeated"));

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    event.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&event.argument, "boss_defeated"));

    TEST_ASSERT_TRUE(trigger_matches(&trigger, &event));
    str_free(&trigger.argument);
    str_free(&event.argument);
}

void test_trigger_no_match_event_wrong_argument(void)
{
    Trigger trigger = {.type = TRIGGER_EVENT};
    trigger.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&trigger.argument, "boss_defeated"));

    TriggerEvent event = {.type = TRIGGER_EVENT, .entity_index = -1};
    event.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&event.argument, "door_opened"));

    TEST_ASSERT_FALSE(trigger_matches(&trigger, &event));
    str_free(&trigger.argument);
    str_free(&event.argument);
}

/* ---- Condition evaluation tests ---- */

void test_condition_flag_true(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "test_flag");

    Condition condition = {.type = COND_FLAG};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "test_flag"));

    Entity entity = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    test_flag_set_free_local(&flags);
}

void test_condition_flag_false(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_FLAG};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "test_flag"));

    Entity entity = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    test_flag_set_free_local(&flags);
}

void test_condition_not_flag(void)
{
    FlagSet flags = {0};

    Condition condition = {.type = COND_NOT_FLAG};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "test_flag"));

    Entity entity = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    test_flag_set_free_local(&flags);
}

void test_condition_attr_truthy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(&test_heap_alloc, &entity.attrs, "is_locked", true);

    Condition condition = {.type = COND_ATTR};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "is_locked"));

    FlagSet flags = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    test_flag_set_free_local(&flags);
}

void test_condition_attr_falsy(void)
{
    Entity entity = {0};
    (void)attr_set_bool(&test_heap_alloc, &entity.attrs, "is_locked", false);

    Condition condition = {.type = COND_ATTR};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "is_locked"));

    FlagSet flags = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    test_flag_set_free_local(&flags);
}

void test_condition_attr_missing(void)
{
    Entity entity = {0};

    Condition condition = {.type = COND_ATTR};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "nonexistent"));

    FlagSet flags = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_FALSE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    test_flag_set_free_local(&flags);
}

void test_condition_attr_less_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5);

    Condition condition = {.type = COND_ATTR_LT, .compare_value = 10.0F};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "health"));

    FlagSet flags = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    test_flag_set_free_local(&flags);
}

void test_condition_attr_greater_than(void)
{
    Entity entity = {0};
    (void)attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 15);

    Condition condition = {.type = COND_ATTR_GT, .compare_value = 10.0F};
    condition.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&condition.argument, "speed"));

    FlagSet flags = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(&condition, 1, context));
    str_free(&condition.argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    test_flag_set_free_local(&flags);
}

void test_condition_and_logic_all_pass(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "flag_a");
    flag_set(&test_diag, &test_heap_alloc, &flags, "flag_b");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    conditions[0].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&conditions[0].argument, "flag_a"));
    conditions[1].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&conditions[1].argument, "flag_b"));

    Entity entity = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_TRUE(conditions_evaluate(conditions, 2, context));
    str_free(&conditions[0].argument);
    str_free(&conditions[1].argument);
    test_flag_set_free_local(&flags);
}

void test_condition_and_logic_one_fails(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "flag_a");

    Condition conditions[2] = {
        {.type = COND_FLAG},
        {.type = COND_FLAG},
    };
    conditions[0].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&conditions[0].argument, "flag_a"));
    conditions[1].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&conditions[1].argument, "flag_b"));

    Entity entity = {0};
    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {.entity = &entity, .flags = &flags, .views = entity_views};
    TEST_ASSERT_FALSE(conditions_evaluate(conditions, 2, context));
    str_free(&conditions[0].argument);
    str_free(&conditions[1].argument);
    test_flag_set_free_local(&flags);
}

/* ---- Action execution tests ---- */

void test_action_set_flag_executes(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode action = {.type = ACTION_SET_FLAG};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "chest_opened"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .views = entity_views,
        .progression_alloc = &test_heap_alloc,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));
    str_free(&action.argument);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_clear_flag_executes(void)
{
    FlagSet flags = {0};
    flag_set(&test_diag, &test_heap_alloc, &flags, "door_locked");
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode action = {.type = ACTION_CLEAR_FLAG};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "door_locked"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .views = entity_views,
        .progression_alloc = &test_heap_alloc,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_FALSE(flag_get(&flags, "door_locked"));
    str_free(&action.argument);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_set_attr_bool(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    (void)attr_set_bool(&test_heap_alloc, &entity.attrs, "is_locked", true);

    ActionNode action = {.type = ACTION_SET_ATTR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "is_locked"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "false"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));

    const Attribute *attr = attr_get(&entity.attrs, "is_locked");
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr->type);
    TEST_ASSERT_FALSE(attr->value.b);
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_set_attr_int(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode action = {.type = ACTION_SET_ATTR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "health"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "42"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&entity.attrs, "health", 0));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_add_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    (void)attr_set_int(&test_heap_alloc, &entity.attrs, "health", 10);

    ActionNode action = {.type = ACTION_ADD_ATTR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "health"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "-3"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(7, attr_get_int(&entity.attrs, "health", 0));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_toggle_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    (void)attr_set_bool(&test_heap_alloc, &entity.attrs, "visible", true);

    ActionNode action = {.type = ACTION_TOGGLE_ATTR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "visible"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_FALSE(attr_get_bool(&entity.attrs, "visible", true));
    str_free(&action.argument);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_destroy(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode action = {.type = ACTION_DESTROY};

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .views = entity_views,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_FALSE(attr_get_bool(&entity.attrs, "active", true));
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&entity.attrs);
}

void test_action_fire_event_queues(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode action = {.type = ACTION_FIRE_EVENT};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "boss_defeated"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .views = entity_views,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(1, queue.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, queue.data[0].type);
    TEST_ASSERT_EQUAL_STRING("boss_defeated", queue.data[0].argument.ptr);
    str_free(&action.argument);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

void test_action_execution_order(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};

    ActionNode actions[2] = {
        {.type = ACTION_SET_FLAG},
        {.type = ACTION_SET_FLAG},
    };
    actions[0].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&actions[0].argument, "first"));
    actions[1].argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&actions[1].argument, "second"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .flags = &flags,
        .event_queue = &queue,
        .views = entity_views,
        .progression_alloc = &test_heap_alloc,
    };
    (void)action_node_execute(&test_diag, &test_heap_alloc, &actions[0], context);
    (void)action_node_execute(&test_diag, &test_heap_alloc, &actions[1], context);

    TEST_ASSERT_TRUE(flag_get(&flags, "first"));
    TEST_ASSERT_TRUE(flag_get(&flags, "second"));
    TEST_ASSERT_EQUAL_INT(2, flags.names.count);
    str_free(&actions[0].argument);
    str_free(&actions[1].argument);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
}

/* ---- Evaluation loop tests ---- */

void test_evaluate_interact_sets_flag(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){.name = "name", .value = "chest"}));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator rule_alloc = allocator_arena(&arena);

    Rule *rule = arena_alloc(&arena, sizeof(Rule));
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    rule->action_tree.nodes.alloc = rule_alloc;
    rule->action_tree.roots.alloc = rule_alloc;
    ActionNode node_interact = {.type = ACTION_SET_FLAG};
    node_interact.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&node_interact.argument, "chest_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&rule->action_tree.nodes, node_interact));
    TEST_ASSERT_TRUE(vec_int_push(&rule->action_tree.roots, 0));

    blueprint.rules.alloc = rule_alloc;
    TEST_ASSERT_TRUE(vec_rule_push(&blueprint.rules, *rule));

    Entity entity = {0};
    entity.id = 0;
    entity.blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entity.blueprint_name, "chest"));

    Allocator heap_alloc = allocator_heap();
    map_entity_ruleset rule_table = map_entity_ruleset_new(heap_alloc);
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entity.id, blueprint.rules));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    EntityView views[] = {{.entity = &entity, .defaults = &blueprint.attrs}};

    rules_evaluate_batch(&test_diag, &test_heap_alloc, views, 1, &event, 1, &flags, &global_vars, &test_heap_alloc,
                         &rule_table, nullptr, nullptr, &rule_alloc, nullptr);
    TEST_ASSERT_TRUE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    attr_set_free(&test_heap_alloc, &blueprint.attrs);
    str_free(&entity.blueprint_name);
    map_entity_ruleset_free(&rule_table);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&global_vars);
}

void test_evaluate_condition_blocks_action(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){.name = "name", .value = "chest"}));

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator rule_alloc = allocator_arena(&arena);

    Rule *rule = arena_alloc(&arena, sizeof(Rule));
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    rule->conditions.alloc = rule_alloc;
    Condition cond_blocked = {.type = COND_FLAG};
    cond_blocked.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&cond_blocked.argument, "has_key"));
    TEST_ASSERT_TRUE(vec_condition_push(&rule->conditions, cond_blocked));
    rule->action_tree.nodes.alloc = rule_alloc;
    rule->action_tree.roots.alloc = rule_alloc;
    ActionNode node_blocked = {.type = ACTION_SET_FLAG};
    node_blocked.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&node_blocked.argument, "chest_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&rule->action_tree.nodes, node_blocked));
    TEST_ASSERT_TRUE(vec_int_push(&rule->action_tree.roots, 0));

    blueprint.rules.alloc = rule_alloc;
    TEST_ASSERT_TRUE(vec_rule_push(&blueprint.rules, *rule));

    Entity entity = {0};
    entity.id = 0;
    entity.blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entity.blueprint_name, "chest"));

    Allocator heap_alloc = allocator_heap();
    map_entity_ruleset rule_table = map_entity_ruleset_new(heap_alloc);
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entity.id, blueprint.rules));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    EntityView views[] = {{.entity = &entity, .defaults = &blueprint.attrs}};

    rules_evaluate_batch(&test_diag, &test_heap_alloc, views, 1, &event, 1, &flags, &global_vars, &test_heap_alloc,
                         &rule_table, nullptr, nullptr, &rule_alloc, nullptr);
    TEST_ASSERT_FALSE(flag_get(&flags, "chest_opened"));

    arena_free(&arena);
    attr_set_free(&test_heap_alloc, &blueprint.attrs);
    str_free(&entity.blueprint_name);
    map_entity_ruleset_free(&rule_table);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&global_vars);
}

void test_evaluate_fire_event_cascading(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator rule_alloc = allocator_arena(&arena);

    Blueprint bp_switch = {0};
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &bp_switch.attrs, (AttrStringPair){.name = "name", .value = "switch"}));

    Rule *switch_rule = arena_alloc(&arena, sizeof(Rule));
    memset(switch_rule, 0, sizeof(*switch_rule));
    switch_rule->trigger.type = TRIGGER_INTERACT;
    switch_rule->action_tree.nodes.alloc = rule_alloc;
    switch_rule->action_tree.roots.alloc = rule_alloc;
    ActionNode switch_node = {.type = ACTION_FIRE_EVENT};
    switch_node.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&switch_node.argument, "switch_pulled"));
    TEST_ASSERT_TRUE(vec_action_node_push(&switch_rule->action_tree.nodes, switch_node));
    TEST_ASSERT_TRUE(vec_int_push(&switch_rule->action_tree.roots, 0));
    bp_switch.rules.alloc = rule_alloc;
    TEST_ASSERT_TRUE(vec_rule_push(&bp_switch.rules, *switch_rule));

    Blueprint bp_door = {0};
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &bp_door.attrs, (AttrStringPair){.name = "name", .value = "door"}));

    Rule *door_rule = arena_alloc(&arena, sizeof(Rule));
    memset(door_rule, 0, sizeof(*door_rule));
    door_rule->trigger.type = TRIGGER_EVENT;
    door_rule->trigger.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&door_rule->trigger.argument, "switch_pulled"));
    door_rule->action_tree.nodes.alloc = rule_alloc;
    door_rule->action_tree.roots.alloc = rule_alloc;
    ActionNode door_node = {.type = ACTION_SET_FLAG};
    door_node.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&door_node.argument, "door_opened"));
    TEST_ASSERT_TRUE(vec_action_node_push(&door_rule->action_tree.nodes, door_node));
    TEST_ASSERT_TRUE(vec_int_push(&door_rule->action_tree.roots, 0));
    bp_door.rules.alloc = rule_alloc;
    TEST_ASSERT_TRUE(vec_rule_push(&bp_door.rules, *door_rule));

    Entity entities[2] = {0};
    entities[0].id = 0;
    entities[0].blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entities[0].blueprint_name, "switch"));
    entities[1].id = 1;
    entities[1].blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entities[1].blueprint_name, "door"));

    Allocator heap_alloc = allocator_heap();
    map_entity_ruleset rule_table = map_entity_ruleset_new(heap_alloc);
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entities[0].id, bp_switch.rules));
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entities[1].id, bp_door.rules));

    FlagSet flags = {0};
    AttrSet global_vars = {0};
    TriggerEvent event = {.type = TRIGGER_INTERACT, .entity_index = 0};
    EntityView views[] = {
        {.entity = &entities[0], .defaults = &bp_switch.attrs},
        {.entity = &entities[1], .defaults = &bp_door.attrs},
    };

    rules_evaluate_batch(&test_diag, &test_heap_alloc, views, 2, &event, 1, &flags, &global_vars, &test_heap_alloc,
                         &rule_table, nullptr, nullptr, &rule_alloc, nullptr);
    TEST_ASSERT_TRUE(flag_get(&flags, "door_opened"));

    arena_free(&arena);
    attr_set_free(&test_heap_alloc, &bp_switch.attrs);
    attr_set_free(&test_heap_alloc, &bp_door.attrs);
    str_free(&entities[0].blueprint_name);
    str_free(&entities[1].blueprint_name);
    map_entity_ruleset_free(&rule_table);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&global_vars);
}

void test_evaluate_batch_handles_over_64_seeded_events(void)
{
    /* Regression for the fixed 64-slot TriggerEventQueue: it silently
     * dropped events past index 64, so only the first 64 of these 100
     * entities would ever see their matching event and set hit_count.
     * The vec_trigger_event-backed queue must process all 100. */
    enum { ENTITY_COUNT = 100 };

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator rule_alloc = allocator_arena(&arena);

    Rule *rule = arena_alloc(&arena, sizeof(Rule));
    TEST_ASSERT_NOT_NULL(rule);
    memset(rule, 0, sizeof(*rule));
    rule->trigger.type = TRIGGER_INTERACT;
    rule->action_tree.nodes.alloc = rule_alloc;
    rule->action_tree.roots.alloc = rule_alloc;
    ActionNode set_hit_count = {.type = ACTION_SET_ATTR};
    set_hit_count.argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&set_hit_count.argument, "hit_count"));
    set_hit_count.second_argument = str_new(rule_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&set_hit_count.second_argument, "1"));
    TEST_ASSERT_TRUE(vec_action_node_push(&rule->action_tree.nodes, set_hit_count));
    TEST_ASSERT_TRUE(vec_int_push(&rule->action_tree.roots, 0));

    vec_rule shared_rules = {0};
    shared_rules.alloc = rule_alloc;
    TEST_ASSERT_TRUE(vec_rule_push(&shared_rules, *rule));

    Allocator heap_alloc = allocator_heap();
    map_entity_ruleset rule_table = map_entity_ruleset_new(heap_alloc);

    Entity entities[ENTITY_COUNT] = {0};
    EntityView views[ENTITY_COUNT];
    TriggerEvent events[ENTITY_COUNT];
    for (int index = 0; index < ENTITY_COUNT; index++) {
        entities[index].id = index;
        views[index] = (EntityView){.entity = &entities[index], .defaults = nullptr};
        events[index] = (TriggerEvent){.type = TRIGGER_INTERACT, .entity_index = index};
        TEST_ASSERT_TRUE(map_entity_ruleset_set(&rule_table, entities[index].id, shared_rules));
    }

    FlagSet flags = {0};
    AttrSet global_vars = {0};

    rules_evaluate_batch(&test_diag, &test_heap_alloc, views, ENTITY_COUNT, events, ENTITY_COUNT, &flags, &global_vars,
                         &test_heap_alloc, &rule_table, nullptr, nullptr, &rule_alloc, nullptr);

    for (int index = 0; index < ENTITY_COUNT; index++) {
        TEST_ASSERT_EQUAL_INT(1, attr_get_int(&entities[index].attrs, "hit_count", 0));
    }

    arena_free(&arena);
    for (int index = 0; index < ENTITY_COUNT; index++) {
        attr_set_free(&test_heap_alloc, &entities[index].attrs);
    }
    map_entity_ruleset_free(&rule_table);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&global_vars);
}

/* ---- Variable system tests ---- */

void test_var_set_local(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "damage"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "42"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&local_vars, "damage", 0));
    TEST_ASSERT_NULL(attr_get(&global_vars, "damage"));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &local_vars);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&local_vars);
}

void test_var_set_global(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "global.score"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "100"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
        .progression_alloc = &test_heap_alloc,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(100, attr_get_int(&global_vars, "score", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars, "score"));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &global_vars);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&local_vars);
}

void test_var_condition_truthy(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(&test_heap_alloc, &local_vars, "active", 1);

    Condition cond = {.type = COND_VAR};
    cond.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&cond.argument, "active"));

    EntityView var_views[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {
        .entity = &entity,
        .views = var_views,
        .view_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(conditions_evaluate(&cond, 1, context));
    str_free(&cond.argument);
    attr_set_free(&test_heap_alloc, &local_vars);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&local_vars);
}

void test_var_condition_falsy_when_unset(void)
{
    FlagSet flags = {0};
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};

    Condition cond = {.type = COND_VAR};
    cond.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&cond.argument, "missing"));

    EntityView var_views2[] = {{.entity = &entity, .defaults = nullptr}};
    ConditionContext context = {
        .entity = &entity,
        .views = var_views2,
        .view_count = 1,
        .flags = &flags,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_FALSE(conditions_evaluate(&cond, 1, context));
    str_free(&cond.argument);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&local_vars);
}

void test_var_substitution_in_set_attr(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    AttrSet local_vars = {0};
    AttrSet global_vars = {0};
    (void)attr_set_int(&test_heap_alloc, &local_vars, "amount", 5);

    ActionNode action = {.type = ACTION_SET_ATTR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "health"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "$amount"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context));
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&entity.attrs, "health", 0));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &local_vars);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&local_vars);
}

void test_local_var_scoped_per_rule(void)
{
    FlagSet flags = {0};
    vec_trigger_event queue = vec_trigger_event_new(test_heap_alloc);
    Entity entity = {0};
    AttrSet local_vars_a = {0};
    AttrSet local_vars_b = {0};
    AttrSet global_vars = {0};

    ActionNode action = {.type = ACTION_SET_VAR};
    action.argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.argument, "temp"));
    action.second_argument = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&action.second_argument, "99"));

    EntityView entity_views[] = {{.entity = &entity, .defaults = nullptr}};
    ActionContext context_a = {
        .entity = &entity,
        .views = entity_views,
        .view_count = 1,
        .flags = &flags,
        .event_queue = &queue,
        .local_vars = &local_vars_a,
        .global_vars = &global_vars,
    };
    TEST_ASSERT_TRUE(action_node_execute(&test_diag, &test_heap_alloc, &action, context_a));
    TEST_ASSERT_EQUAL_INT(99, attr_get_int(&local_vars_a, "temp", 0));
    TEST_ASSERT_NULL(attr_get(&local_vars_b, "temp"));
    str_free(&action.argument);
    str_free(&action.second_argument);
    attr_set_free(&test_heap_alloc, &local_vars_a);
    vec_trigger_event_free(&queue);
    test_flag_set_free_local(&flags);
    test_attr_set_free_local(&global_vars);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_flag_set_and_get);
    RUN_TEST(test_flag_clear);
    RUN_TEST(test_flag_unset_returns_false);
    RUN_TEST(test_flag_set_idempotent);
    RUN_TEST(test_flag_clear_nonexistent);
    RUN_TEST(test_trigger_parse_interact);
    RUN_TEST(test_trigger_parse_enter);
    RUN_TEST(test_trigger_parse_on_spawn);
    RUN_TEST(test_trigger_parse_event);
    RUN_TEST(test_trigger_parse_attr_changed);
    RUN_TEST(test_trigger_parse_unknown);
    RUN_TEST(test_condition_parse_flag);
    RUN_TEST(test_condition_parse_not_flag);
    RUN_TEST(test_condition_parse_attr_truthy);
    RUN_TEST(test_condition_parse_attr_short_form);
    RUN_TEST(test_condition_parse_not_attr);
    RUN_TEST(test_condition_parse_attr_less_than);
    RUN_TEST(test_condition_parse_attr_greater_than);
    RUN_TEST(test_condition_parse_attr_equals);
    RUN_TEST(test_condition_parse_has_item);
    RUN_TEST(test_condition_parse_unknown);
    RUN_TEST(test_action_parse_set_flag);
    RUN_TEST(test_action_parse_clear_flag);
    RUN_TEST(test_action_parse_set_attr);
    RUN_TEST(test_action_parse_add_attr);
    RUN_TEST(test_action_parse_toggle_attr);
    RUN_TEST(test_action_parse_destroy);
    RUN_TEST(test_action_parse_fire_event);
    RUN_TEST(test_action_parse_unknown);
    RUN_TEST(test_trigger_matches_simple);
    RUN_TEST(test_trigger_no_match_different_type);
    RUN_TEST(test_trigger_matches_event_with_argument);
    RUN_TEST(test_trigger_no_match_event_wrong_argument);
    RUN_TEST(test_condition_flag_true);
    RUN_TEST(test_condition_flag_false);
    RUN_TEST(test_condition_not_flag);
    RUN_TEST(test_condition_attr_truthy);
    RUN_TEST(test_condition_attr_falsy);
    RUN_TEST(test_condition_attr_missing);
    RUN_TEST(test_condition_attr_less_than);
    RUN_TEST(test_condition_attr_greater_than);
    RUN_TEST(test_condition_and_logic_all_pass);
    RUN_TEST(test_condition_and_logic_one_fails);
    RUN_TEST(test_action_set_flag_executes);
    RUN_TEST(test_action_clear_flag_executes);
    RUN_TEST(test_action_set_attr_bool);
    RUN_TEST(test_action_set_attr_int);
    RUN_TEST(test_action_add_attr);
    RUN_TEST(test_action_toggle_attr);
    RUN_TEST(test_action_destroy);
    RUN_TEST(test_action_fire_event_queues);
    RUN_TEST(test_action_execution_order);
    RUN_TEST(test_evaluate_interact_sets_flag);
    RUN_TEST(test_evaluate_condition_blocks_action);
    RUN_TEST(test_evaluate_fire_event_cascading);
    RUN_TEST(test_evaluate_batch_handles_over_64_seeded_events);
    RUN_TEST(test_var_set_local);
    RUN_TEST(test_var_set_global);
    RUN_TEST(test_var_condition_truthy);
    RUN_TEST(test_var_condition_falsy_when_unset);
    RUN_TEST(test_var_substitution_in_set_attr);
    RUN_TEST(test_local_var_scoped_per_rule);

    return UNITY_END();
}

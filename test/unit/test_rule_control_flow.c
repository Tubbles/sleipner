#include "rule.h"
#include "arena.h"
#include "entity.h"
#include "blueprint.h"
#include "attribute.h"
#include "unity.h"

#include <string.h>

static Arena arena;
static FlagSet flags;
static Entity entities[4];
static Blueprint blueprints[2];
static AttributeSet attr_storage[4];

void setUp(void)
{
    arena_init(&arena, 1024 * 1024);
    memset(&flags, 0, sizeof(flags));
    
    // Set up test blueprints
    for (int i = 0; i < 2; i++) {
        memset(&blueprints[i], 0, sizeof(Blueprint));
        blueprints[i].rules.entries = NULL;
        blueprints[i].rules.count = 0;
    }
    
    // Set up test entities
    for (int i = 0; i < 4; i++) {
        memset(&entities[i], 0, sizeof(Entity));
        entities[i].blueprint = &blueprints[i % 2];
        entities[i].active = true;
        attr_init(&attr_storage[i]);
        entities[i].attrs = attr_storage[i];
    }
}

void tearDown(void)
{
    arena_free(&arena);
}

void test_if_else_control_flow(void)
{
    // Create a rule with if/else control flow
    Rule rule;
    memset(&rule, 0, sizeof(rule));
    
    // Set up trigger
    rule.trigger.type = TRIGGER_INTERACT;
    
    // Set up action tree with if/else
    ActionNode *nodes = arena_alloc(&arena, (AllocRequest){
        .size = 1 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(nodes);
    
    // Create if/else node
    ActionNode *if_node = &nodes[0];
    if_node->type = ACTION_IF_ELSE;
    strncpy(if_node->argument, "flag:test_flag", MAX_ARG - 1);
    
    // Then branch: set_flag:then_executed
    if_node->children = arena_alloc(&arena, (AllocRequest){
        .size = 1 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(if_node->children);
    if_node->child_count = 1;
    
    ActionNode *then_action = &if_node->children[0];
    then_action->type = ACTION_SET_FLAG;
    strncpy(then_action->argument, "then_executed", MAX_ARG - 1);
    
    // Else branch: set_flag:else_executed
    if_node->else_children = arena_alloc(&arena, (AllocRequest){
        .size = 1 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(if_node->else_children);
    if_node->else_child_count = 1;
    
    ActionNode *else_action = &if_node->else_children[0];
    else_action->type = ACTION_SET_FLAG;
    strncpy(else_action->argument, "else_executed", MAX_ARG - 1);
    
    rule.action_tree.nodes = nodes;
    rule.action_tree.count = 1;
    
    // Test execution with condition true
    flag_set(&flags, "test_flag");
    
    ActionContext ctx = {
        .entity = &entities[0],
        .entity_index = 0,
        .entities = entities,
        .entity_count = 4,
        .flags = &flags,
        .event_queue = NULL
    };
    
    TEST_ASSERT_TRUE(action_node_execute(if_node, ctx));
    TEST_ASSERT_TRUE(flag_get(&flags, "then_executed"));
    TEST_ASSERT_FALSE(flag_get(&flags, "else_executed"));
    
    // Reset and test with condition false
    flag_clear(&flags, "test_flag");
    flag_clear(&flags, "then_executed");
    flag_clear(&flags, "else_executed");
    
    TEST_ASSERT_TRUE(action_node_execute(if_node, ctx));
    TEST_ASSERT_FALSE(flag_get(&flags, "then_executed"));
    TEST_ASSERT_TRUE(flag_get(&flags, "else_executed"));
}

void test_repeat_control_flow(void)
{
    // Create a rule with repeat control flow
    Rule rule;
    memset(&rule, 0, sizeof(rule));
    
    // Set up trigger
    rule.trigger.type = TRIGGER_INTERACT;
    
    // Set up action tree with repeat
    ActionNode *nodes = arena_alloc(&arena, (AllocRequest){
        .size = 1 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(nodes);
    
    // Create repeat node
    ActionNode *repeat_node = &nodes[0];
    repeat_node->type = ACTION_REPEAT;
    strncpy(repeat_node->argument, "3", MAX_ARG - 1);
    
    // Repeat body: set_flag:repeat_executed
    repeat_node->children = arena_alloc(&arena, (AllocRequest){
        .size = 1 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(repeat_node->children);
    repeat_node->child_count = 1;
    
    ActionNode *body_action = &repeat_node->children[0];
    body_action->type = ACTION_SET_FLAG;
    strncpy(body_action->argument, "repeat_executed", MAX_ARG - 1);
    
    rule.action_tree.nodes = nodes;
    rule.action_tree.count = 1;
    
    ActionContext ctx = {
        .entity = &entities[0],
        .entity_index = 0,
        .entities = entities,
        .entity_count = 4,
        .flags = &flags,
        .event_queue = NULL
    };
    
    // Execute repeat
    TEST_ASSERT_TRUE(action_node_execute(repeat_node, ctx));
    
    // The flag should be set (though it's set 3 times, it's still just set once)
    TEST_ASSERT_TRUE(flag_get(&flags, "repeat_executed"));
}

void test_simple_actions_still_work(void)
{
    // Test that regular string actions still work
    Rule rule;
    memset(&rule, 0, sizeof(rule));
    
    rule.trigger.type = TRIGGER_INTERACT;
    
    ActionNode *nodes = arena_alloc(&arena, (AllocRequest){
        .size = 2 * sizeof(ActionNode),
        .alignment = _Alignof(ActionNode)
    });
    TEST_ASSERT_NOT_NULL(nodes);
    
    // set_flag:test_flag
    nodes[0].type = ACTION_SET_FLAG;
    strncpy(nodes[0].argument, "test_flag", MAX_ARG - 1);
    
    // set_attr:self.health,10
    nodes[1].type = ACTION_SET_ATTR;
    strncpy(nodes[1].argument, "self.health", MAX_ARG - 1);
    strncpy(nodes[1].second_argument, "10", MAX_ARG - 1);
    
    rule.action_tree.nodes = nodes;
    rule.action_tree.count = 2;
    
    ActionContext ctx = {
        .entity = &entities[0],
        .entity_index = 0,
        .entities = entities,
        .entity_count = 4,
        .flags = &flags,
        .event_queue = NULL
    };
    
    TEST_ASSERT_TRUE(action_node_execute(&nodes[0], ctx));
    TEST_ASSERT_TRUE(flag_get(&flags, "test_flag"));
    
    TEST_ASSERT_TRUE(action_node_execute(&nodes[1], ctx));
    const Attribute *health = entity_get_attr(&entities[0], "health");
    TEST_ASSERT_NOT_NULL(health);
    TEST_ASSERT_EQUAL(10, health->value.i);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_if_else_control_flow);
    RUN_TEST(test_repeat_control_flow);
    RUN_TEST(test_simple_actions_still_work);
    return UNITY_END();
}
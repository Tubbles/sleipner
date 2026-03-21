#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "entity.h"

#include <string.h>

static Blueprint make_test_blueprint(void)
{
    Blueprint blueprint = {0};
    strncpy(blueprint.name, "chest", MAX_BLUEPRINT_NAME);
    strncpy(blueprint.texture_name, "chest.png", MAX_TEXTURE_NAME);
    blueprint.source = (Rectangle){0, 0, 16, 16};
    blueprint.collision_offset = (Vector2){2, 4};
    blueprint.collision_size = (Vector2){12, 8};

    TEST_ASSERT_TRUE(attr_set_string(&ctx, &blueprint.attrs, (AttrStringPair){.name = "behavior", .value = "static"}));
    TEST_ASSERT_TRUE(attr_set_bool(&ctx, &blueprint.attrs, "is_locked", true));
    TEST_ASSERT_TRUE(attr_set_int(&ctx, &blueprint.attrs, "health", 3));
    TEST_ASSERT_TRUE(attr_set_int(&ctx, &blueprint.attrs, "max_health", 5));
    TEST_ASSERT_TRUE(attr_set_float(&ctx, &blueprint.attrs, "speed", 0.0F));

    return blueprint;
}

void test_entity_init_from_blueprint(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){100, 200}, &dummy);

    TEST_ASSERT_EQUAL_STRING("chest", entity.blueprint_name);
    TEST_ASSERT_TRUE(entity.blueprint == &blueprint);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, entity.position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 200.0F, entity.position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 102.0F, entity.collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 204.0F, entity.collision.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 12.0F, entity.collision.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.0F, entity.collision.height);
    TEST_ASSERT_EQUAL_INT(3, entity.health);
    TEST_ASSERT_EQUAL_INT(5, entity.max_health);
    TEST_ASSERT_TRUE(entity.visible);
    TEST_ASSERT_TRUE(entity.active);
    TEST_ASSERT_TRUE(entity.solid);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.0F, entity.opacity);
    TEST_ASSERT_EQUAL_INT(-1, entity.parent_index);
    vec_attribute_free(&blueprint.attrs.entries);
}

void test_entity_get_attr_from_blueprint(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* No instance overrides — falls back to blueprint */
    TEST_ASSERT_EQUAL_STRING("static", entity_get_string(&entity, "behavior"));
    TEST_ASSERT_TRUE(entity_get_bool(&entity, "is_locked", false));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, entity_get_float(&entity, "speed", -1.0F));
    vec_attribute_free(&blueprint.attrs.entries);
}

void test_entity_instance_overrides_blueprint(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* Override at instance level */
    TEST_ASSERT_TRUE(attr_set_bool(&ctx, &entity.attrs, "is_locked", false));
    TEST_ASSERT_TRUE(attr_set_float(&ctx, &entity.attrs, "speed", 50.0F));

    /* Instance wins */
    TEST_ASSERT_FALSE(entity_get_bool(&entity, "is_locked", true));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, entity_get_float(&entity, "speed", 0));

    /* Blueprint still provides unoverridden attrs */
    TEST_ASSERT_EQUAL_STRING("static", entity_get_string(&entity, "behavior"));
    vec_attribute_free(&blueprint.attrs.entries);
    vec_attribute_free(&entity.attrs.entries);
}

void test_entity_get_missing_attr(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    TEST_ASSERT_EQUAL_INT(42, entity_get_int(&entity, "nonexistent", 42));
    TEST_ASSERT_NULL(entity_get_string(&entity, "nope"));
    vec_attribute_free(&blueprint.attrs.entries);
}

void test_entity_int_float_coercion(void)
{
    Blueprint blueprint = {0};
    strncpy(blueprint.name, "test", MAX_BLUEPRINT_NAME);
    TEST_ASSERT_TRUE(attr_set_int(&ctx, &blueprint.attrs, "speed", 80));

    Texture2D dummy = {0};
    Entity entity;
    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* Int attr retrieved as float via coercion */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, entity_get_float(&entity, "speed", 0));
    vec_attribute_free(&blueprint.attrs.entries);
}

void test_entity_no_blueprint(void)
{
    Entity entity = {0};
    entity.parent_index = -1;

    TEST_ASSERT_TRUE(attr_set_float(&ctx, &entity.attrs, "speed", 100.0F));

    /* Works without a blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, entity_get_float(&entity, "speed", 0));
    TEST_ASSERT_EQUAL_INT(0, entity_get_int(&entity, "missing", 0));
    vec_attribute_free(&entity.attrs.entries);
}

void test_entity_solid_from_collision(void)
{
    Blueprint no_collision = {0};
    strncpy(no_collision.name, "ghost", MAX_BLUEPRINT_NAME);
    /* collision_size is (0, 0) */

    Texture2D dummy = {0};
    Entity entity;
    entity_init_from_blueprint(&entity, &no_collision, (Vector2){0, 0}, &dummy);

    TEST_ASSERT_FALSE(entity.solid);
}

/* Helper: set up a 3-entity tree (parent -> child -> grandchild) */
static void make_entity_tree(Entity *entities)
{
    memset(entities, 0, 3 * sizeof(Entity));

    strncpy(entities[0].blueprint_name, "wagon", MAX_BLUEPRINT_NAME);
    entities[0].parent_index = -1;
    entities[0].visible = true;
    entities[0].active = true;

    strncpy(entities[1].blueprint_name, "lantern", MAX_BLUEPRINT_NAME);
    strncpy(entities[1].tag, "light", MAX_TAG);
    entities[1].parent_index = 0;
    entities[1].visible = true;
    entities[1].active = true;

    strncpy(entities[2].blueprint_name, "wheel", MAX_BLUEPRINT_NAME);
    strncpy(entities[2].tag, "front_wheel", MAX_TAG);
    entities[2].parent_index = 0;
    entities[2].visible = true;
    entities[2].active = true;
}

void test_entity_find_by_tag_self(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "self", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[1]);
}

void test_entity_find_by_tag_parent(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "parent", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[0]);
}

void test_entity_find_by_tag_parent_of_root(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[0], "parent", entities, 3);
    TEST_ASSERT_NULL(result);
}

void test_entity_find_by_tag_root(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "root", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[0]);
}

void test_entity_find_by_tag_custom(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "front_wheel", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[2]);
}

void test_entity_find_by_tag_not_found(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "nonexistent", entities, 3);
    TEST_ASSERT_NULL(result);
}

void test_entity_is_visible_standalone(void)
{
    Entity entity = {0};
    entity.parent_index = -1;
    entity.visible = true;

    TEST_ASSERT_TRUE(entity_is_visible(0, &entity));
}

void test_entity_is_visible_parent_hidden(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    /* Child is visible, but parent is hidden */
    entities[0].visible = false;

    TEST_ASSERT_FALSE(entity_is_visible(1, entities));
}

void test_entity_is_visible_both_visible(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    TEST_ASSERT_TRUE(entity_is_visible(1, entities));
}

void test_entity_is_active_parent_inactive(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    entities[0].active = false;

    TEST_ASSERT_FALSE(entity_is_active(1, entities));
}

void test_entity_is_active_both_active(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    TEST_ASSERT_TRUE(entity_is_active(1, entities));
}

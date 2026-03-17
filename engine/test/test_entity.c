#include "unity.h"
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

    attr_set_string(&blueprint.attrs, "behavior", "static");
    attr_set_bool(&blueprint.attrs, "is_locked", true);
    attr_set_int(&blueprint.attrs, "health", 3);
    attr_set_int(&blueprint.attrs, "max_health", 5);
    attr_set_float(&blueprint.attrs, "speed", 0.0F);

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
}

void test_entity_get_attr_from_blueprint(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* No instance overrides — falls back to blueprint */
    TEST_ASSERT_EQUAL_STRING("static", entity_get_string(&entity, "behavior", ""));
    TEST_ASSERT_TRUE(entity_get_bool(&entity, "is_locked", false));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, entity_get_float(&entity, "speed", -1.0F));
}

void test_entity_instance_overrides_blueprint(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* Override at instance level */
    attr_set_bool(&entity.attrs, "is_locked", false);
    attr_set_float(&entity.attrs, "speed", 50.0F);

    /* Instance wins */
    TEST_ASSERT_FALSE(entity_get_bool(&entity, "is_locked", true));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, entity_get_float(&entity, "speed", 0));

    /* Blueprint still provides unoverridden attrs */
    TEST_ASSERT_EQUAL_STRING("static", entity_get_string(&entity, "behavior", ""));
}

void test_entity_get_missing_attr(void)
{
    Blueprint blueprint = make_test_blueprint();
    Texture2D dummy = {0};
    Entity entity;

    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    TEST_ASSERT_EQUAL_INT(42, entity_get_int(&entity, "nonexistent", 42));
    TEST_ASSERT_EQUAL_STRING("default", entity_get_string(&entity, "nope", "default"));
}

void test_entity_int_float_coercion(void)
{
    Blueprint blueprint = {0};
    strncpy(blueprint.name, "test", MAX_BLUEPRINT_NAME);
    attr_set_int(&blueprint.attrs, "speed", 80);

    Texture2D dummy = {0};
    Entity entity;
    entity_init_from_blueprint(&entity, &blueprint, (Vector2){0, 0}, &dummy);

    /* Int attr retrieved as float via coercion */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, entity_get_float(&entity, "speed", 0));
}

void test_entity_no_blueprint(void)
{
    Entity entity = {0};
    entity.parent_index = -1;

    attr_set_float(&entity.attrs, "speed", 100.0F);

    /* Works without a blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, entity_get_float(&entity, "speed", 0));
    TEST_ASSERT_EQUAL_INT(0, entity_get_int(&entity, "missing", 0));
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

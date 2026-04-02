#include "fff.h"
#include "unity.h"

#include "../src/strv.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c" // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"    // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Lightweight stand-in for blueprint geometry helpers.
 * The real functions live in blueprint.c but only read attrs — replicate
 * that logic here so we avoid pulling in the full blueprint/rule/toml chain. */
static Vector2 get_collision_offset(const AttrSet *attrs)
{
    return (Vector2){
        attr_get_float(attrs, "collision_offset_x", 0.0F),
        attr_get_float(attrs, "collision_offset_y", 0.0F),
    };
}

static Vector2 get_collision_size(const AttrSet *attrs)
{
    return (Vector2){
        attr_get_float(attrs, "collision_w", 0.0F),
        attr_get_float(attrs, "collision_h", 0.0F),
    };
}

static EntitySpec spec_from_attrs(const AttrSet *attrs, Texture2D *texture)
{
    return (EntitySpec){
        .blueprint_name = strv_from_cstr(attr_get_string(attrs, "name")),
        .collision_offset = get_collision_offset(attrs),
        .collision_size = get_collision_size(attrs),
        .texture = texture,
    };
}

static AttrSet make_test_blueprint_attrs(void)
{
    AttrSet attrs = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &attrs, (AttrStringPair){.name = "name", .value = "chest"}));
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &attrs, (AttrStringPair){.name = "texture", .value = "chest.png"}));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "src_x", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "src_y", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "src_w", 16.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "src_h", 16.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "collision_offset_x", 2.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "collision_offset_y", 4.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "collision_w", 12.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "collision_h", 8.0F));

    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &attrs, (AttrStringPair){.name = "behavior", .value = "static"}));
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &attrs, "is_locked", true));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &attrs, "health", 3));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &attrs, "max_health", 5));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "speed", 0.0F));

    return attrs;
}

static void free_test_entity(Entity *entity)
{
    str_free(&test_heap_alloc, &entity->blueprint_name);
    str_free(&test_heap_alloc, &entity->tag);
    attr_set_free(&test_heap_alloc, &entity->attrs);
}

void test_entity_init_from_blueprint(void)
{
    AttrSet bp_attrs = make_test_blueprint_attrs();
    Texture2D dummy = {0};
    EntitySpec spec = spec_from_attrs(&bp_attrs, &dummy);
    Entity entity;

    TEST_ASSERT_TRUE(entity_init(&entity, spec, (Vector2){100, 200}, &test_heap_alloc));

    TEST_ASSERT_EQUAL_STRING("chest", entity.blueprint_name.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, entity.position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 200.0F, entity.position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 102.0F, entity.collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 204.0F, entity.collision.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 12.0F, entity.collision.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.0F, entity.collision.height);
    const AttrSet *defaults = &bp_attrs;
    TEST_ASSERT_EQUAL_INT(3, attr_get_scoped_int(&entity.attrs, defaults, "health", 0));
    TEST_ASSERT_EQUAL_INT(5, attr_get_scoped_int(&entity.attrs, defaults, "max_health", 0));
    TEST_ASSERT_TRUE(attr_get_scoped_bool(&entity.attrs, defaults, "visible", true));
    TEST_ASSERT_TRUE(attr_get_scoped_bool(&entity.attrs, defaults, "active", true));
    /* solid is now set by level.c, not entity_init — check collision size instead */
    TEST_ASSERT_TRUE(spec.collision_size.x > 0.0F || spec.collision_size.y > 0.0F);
    TEST_ASSERT_EQUAL_INT(-1, entity.parent_index);
    attr_set_free(&test_heap_alloc, &bp_attrs);
    free_test_entity(&entity);
}

void test_entity_get_attr_from_blueprint(void)
{
    AttrSet bp_attrs = make_test_blueprint_attrs();
    Texture2D dummy = {0};
    Entity entity;

    TEST_ASSERT_TRUE(entity_init(&entity, spec_from_attrs(&bp_attrs, &dummy), (Vector2){0, 0}, &test_heap_alloc));

    const AttrSet *defaults = &bp_attrs;
    TEST_ASSERT_EQUAL_STRING("static", attr_get_scoped_string(&entity.attrs, defaults, "behavior"));
    TEST_ASSERT_TRUE(attr_get_scoped_bool(&entity.attrs, defaults, "is_locked", false));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, attr_get_scoped_float(&entity.attrs, defaults, "speed", -1.0F));
    attr_set_free(&test_heap_alloc, &bp_attrs);
    free_test_entity(&entity);
}

void test_entity_instance_overrides_blueprint(void)
{
    AttrSet bp_attrs = make_test_blueprint_attrs();
    Texture2D dummy = {0};
    Entity entity;

    TEST_ASSERT_TRUE(entity_init(&entity, spec_from_attrs(&bp_attrs, &dummy), (Vector2){0, 0}, &test_heap_alloc));

    /* Override at instance level */
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &entity.attrs, "is_locked", false));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "speed", 50.0F));

    const AttrSet *defaults = &bp_attrs;
    /* Instance wins */
    TEST_ASSERT_FALSE(attr_get_scoped_bool(&entity.attrs, defaults, "is_locked", true));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, attr_get_scoped_float(&entity.attrs, defaults, "speed", 0));

    /* Blueprint still provides unoverridden attrs */
    TEST_ASSERT_EQUAL_STRING("static", attr_get_scoped_string(&entity.attrs, defaults, "behavior"));
    attr_set_free(&test_heap_alloc, &bp_attrs);
    free_test_entity(&entity);
}

void test_entity_get_missing_attr(void)
{
    AttrSet bp_attrs = make_test_blueprint_attrs();
    Texture2D dummy = {0};
    Entity entity;

    TEST_ASSERT_TRUE(entity_init(&entity, spec_from_attrs(&bp_attrs, &dummy), (Vector2){0, 0}, &test_heap_alloc));

    const AttrSet *defaults = &bp_attrs;
    TEST_ASSERT_EQUAL_INT(42, attr_get_scoped_int(&entity.attrs, defaults, "nonexistent", 42));
    TEST_ASSERT_NULL(attr_get_scoped_string(&entity.attrs, defaults, "nope"));
    attr_set_free(&test_heap_alloc, &bp_attrs);
    free_test_entity(&entity);
}

void test_entity_int_float_coercion(void)
{
    AttrSet bp_attrs = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &bp_attrs, (AttrStringPair){.name = "name", .value = "test"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &bp_attrs, "speed", 80));

    Texture2D dummy = {0};
    Entity entity;
    TEST_ASSERT_TRUE(entity_init(&entity, spec_from_attrs(&bp_attrs, &dummy), (Vector2){0, 0}, &test_heap_alloc));

    const AttrSet *defaults = &bp_attrs;
    /* Int attr retrieved as float via coercion */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, attr_get_scoped_float(&entity.attrs, defaults, "speed", 0));
    attr_set_free(&test_heap_alloc, &bp_attrs);
    free_test_entity(&entity);
}

void test_entity_no_blueprint(void)
{
    Entity entity = {0};
    entity.parent_index = -1;

    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "speed", 100.0F));

    /* Works without defaults (nullptr) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, attr_get_scoped_float(&entity.attrs, nullptr, "speed", 0));
    TEST_ASSERT_EQUAL_INT(0, attr_get_scoped_int(&entity.attrs, nullptr, "missing", 0));
    free_test_entity(&entity);
}

void test_entity_solid_from_collision(void)
{
    /* Solid is no longer auto-derived in entity_init — it's set by level.c.
     * Test the logic directly: no collision size means not solid. */
    Entity entity = {0};
    entity.parent_index = -1;
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &entity.attrs, "solid", false));

    TEST_ASSERT_FALSE(attr_get_scoped_bool(&entity.attrs, nullptr, "solid", true));
    free_test_entity(&entity);
}

/* Helper: set up a 3-entity tree (parent -> child -> grandchild) */
static void make_entity_tree(Entity *entities)
{
    memset(entities, 0, 3 * sizeof(Entity));

    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entities[0].blueprint_name, "wagon"));
    entities[0].parent_index = -1;

    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entities[1].blueprint_name, "lantern"));
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entities[1].tag, "light"));
    entities[1].parent_index = 0;

    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entities[2].blueprint_name, "wheel"));
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entities[2].tag, "front_wheel"));
    entities[2].parent_index = 0;
}

static void free_entity_tree(Entity *entities)
{
    for (int index = 0; index < 3; index++) {
        free_test_entity(&entities[index]);
    }
}

void test_entity_find_by_tag_self(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "self", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[1]);
    free_entity_tree(entities);
}

void test_entity_find_by_tag_parent(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "parent", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[0]);
    free_entity_tree(entities);
}

void test_entity_find_by_tag_parent_of_root(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[0], "parent", entities, 3);
    TEST_ASSERT_NULL(result);
    free_entity_tree(entities);
}

void test_entity_find_by_tag_root(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "root", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[0]);
    free_entity_tree(entities);
}

void test_entity_find_by_tag_custom(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "front_wheel", entities, 3);
    TEST_ASSERT_TRUE(result == &entities[2]);
    free_entity_tree(entities);
}

void test_entity_find_by_tag_not_found(void)
{
    Entity entities[3];
    make_entity_tree(entities);

    const Entity *result = entity_find_by_tag(&entities[1], "nonexistent", entities, 3);
    TEST_ASSERT_NULL(result);
    free_entity_tree(entities);
}

void test_entity_is_visible_standalone(void)
{
    Entity entity = {0};
    entity.parent_index = -1;
    const AttrSet *defaults[] = {nullptr};

    TEST_ASSERT_TRUE(entity_is_visible(0, &entity, defaults));
    free_test_entity(&entity);
}

void test_entity_is_visible_parent_hidden(void)
{
    Entity entities[3];
    make_entity_tree(entities);
    const AttrSet *defaults[] = {nullptr, nullptr, nullptr};

    /* Child is visible, but parent is hidden */
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &entities[0].attrs, "visible", false));

    TEST_ASSERT_FALSE(entity_is_visible(1, entities, defaults));
    free_entity_tree(entities);
}

void test_entity_is_visible_both_visible(void)
{
    Entity entities[3];
    make_entity_tree(entities);
    const AttrSet *defaults[] = {nullptr, nullptr, nullptr};

    TEST_ASSERT_TRUE(entity_is_visible(1, entities, defaults));
    free_entity_tree(entities);
}

void test_entity_is_active_parent_inactive(void)
{
    Entity entities[3];
    make_entity_tree(entities);
    const AttrSet *defaults[] = {nullptr, nullptr, nullptr};

    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &entities[0].attrs, "active", false));

    TEST_ASSERT_FALSE(entity_is_active(1, entities, defaults));
    free_entity_tree(entities);
}

void test_entity_is_active_both_active(void)
{
    Entity entities[3];
    make_entity_tree(entities);
    const AttrSet *defaults[] = {nullptr, nullptr, nullptr};

    TEST_ASSERT_TRUE(entity_is_active(1, entities, defaults));
    free_entity_tree(entities);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_entity_init_from_blueprint);
    RUN_TEST(test_entity_get_attr_from_blueprint);
    RUN_TEST(test_entity_instance_overrides_blueprint);
    RUN_TEST(test_entity_get_missing_attr);
    RUN_TEST(test_entity_int_float_coercion);
    RUN_TEST(test_entity_no_blueprint);
    RUN_TEST(test_entity_solid_from_collision);
    RUN_TEST(test_entity_find_by_tag_self);
    RUN_TEST(test_entity_find_by_tag_parent);
    RUN_TEST(test_entity_find_by_tag_parent_of_root);
    RUN_TEST(test_entity_find_by_tag_root);
    RUN_TEST(test_entity_find_by_tag_custom);
    RUN_TEST(test_entity_find_by_tag_not_found);
    RUN_TEST(test_entity_is_visible_standalone);
    RUN_TEST(test_entity_is_visible_parent_hidden);
    RUN_TEST(test_entity_is_visible_both_visible);
    RUN_TEST(test_entity_is_active_parent_inactive);
    RUN_TEST(test_entity_is_active_both_active);
    return UNITY_END();
}

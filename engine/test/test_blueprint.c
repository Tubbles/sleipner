#include "unity.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"

#include "toml.h"

#include <string.h>

static void with_arena(Arena *arena)
{
    arena_init(arena, 4096);
}

static toml_table_t *parse_toml(const char *input)
{
    /* toml_parse needs a mutable buffer */
    char buffer[4096];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    char errbuf[200];
    return toml_parse(buffer, errbuf, (int)sizeof(errbuf));
}

void test_blueprint_load_single(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"tree\"\n"
                                    "texture = \"tree.png\"\n"
                                    "src = [0, 0, 64, 80]\n"
                                    "collision_offset = [20, 60]\n"
                                    "collision_size = [24, 16]\n");
    TEST_ASSERT_NOT_NULL(root);

    int count = blueprints_load(&table, root, &test_arena);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_INT(1, table.count);

    TEST_ASSERT_EQUAL_STRING("tree", table.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("tree.png", table.entries[0].texture_name);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, table.entries[0].source.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, table.entries[0].source.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 64.0f, table.entries[0].source.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, table.entries[0].source.height);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, table.entries[0].collision_offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, table.entries[0].collision_offset.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, table.entries[0].collision_size.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, table.entries[0].collision_size.y);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_load_multiple(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"tree\"\n"
                                    "texture = \"tree.png\"\n"
                                    "src = [0, 0, 64, 80]\n"
                                    "collision_offset = [20, 60]\n"
                                    "collision_size = [24, 16]\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"chest\"\n"
                                    "texture = \"chest.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n");
    TEST_ASSERT_NOT_NULL(root);

    int count = blueprints_load(&table, root, &test_arena);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("tree", table.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("chest", table.entries[1].name);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_find(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"tree\"\n"
                                    "texture = \"tree.png\"\n"
                                    "src = [0, 0, 64, 80]\n"
                                    "collision_offset = [20, 60]\n"
                                    "collision_size = [24, 16]\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"chest\"\n"
                                    "texture = \"chest.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n");
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&table, root, &test_arena);

    const Blueprint *found = blueprint_find(&table, "chest");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("chest", found->name);
    TEST_ASSERT_EQUAL_STRING("chest.png", found->texture_name);

    const Blueprint *not_found = blueprint_find(&table, "nonexistent");
    TEST_ASSERT_NULL(not_found);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_skip_nameless(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "texture = \"tree.png\"\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"chest\"\n"
                                    "texture = \"chest.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n");
    TEST_ASSERT_NOT_NULL(root);

    int count = blueprints_load(&table, root, &test_arena);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("chest", table.entries[0].name);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_no_blueprints_section(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[level]]\n"
                                    "name = \"overworld\"\n");
    TEST_ASSERT_NOT_NULL(root);

    int count = blueprints_load(&table, root, &test_arena);
    TEST_ASSERT_EQUAL_INT(0, count);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_custom_attrs(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"chest\"\n"
                                    "texture = \"chest.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n"
                                    "behavior = \"static\"\n"
                                    "is_locked = true\n"
                                    "loot_table = \"common\"\n"
                                    "speed = 80.0\n"
                                    "weight = 5\n");
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&table, root, &test_arena);
    const Blueprint *chest = blueprint_find(&table, "chest");
    TEST_ASSERT_NOT_NULL(chest);

    TEST_ASSERT_EQUAL_STRING("static", attr_get_string(&chest->attrs, "behavior", ""));
    TEST_ASSERT_TRUE(attr_get_bool(&chest->attrs, "is_locked", false));
    TEST_ASSERT_EQUAL_STRING("common", attr_get_string(&chest->attrs, "loot_table", ""));
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, attr_get_float(&chest->attrs, "speed", 0));
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&chest->attrs, "weight", 0));

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_health_parsed(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"enemy\"\n"
                                    "texture = \"enemy.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n"
                                    "health = [3, 5]\n");
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&table, root, &test_arena);
    const Blueprint *enemy = blueprint_find(&table, "enemy");
    TEST_ASSERT_NOT_NULL(enemy);

    TEST_ASSERT_EQUAL_INT(3, attr_get_int(&enemy->attrs, "health", 0));
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&enemy->attrs, "max_health", 0));

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_extends(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"chest\"\n"
                                    "texture = \"chest.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n"
                                    "behavior = \"static\"\n"
                                    "is_locked = false\n"
                                    "loot_table = \"common\"\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"locked_chest\"\n"
                                    "extends = \"chest\"\n"
                                    "is_locked = true\n"
                                    "loot_table = \"rare\"\n");
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&table, root, &test_arena);
    const Blueprint *locked = blueprint_find(&table, "locked_chest");
    TEST_ASSERT_NOT_NULL(locked);

    /* Overridden attrs */
    TEST_ASSERT_TRUE(attr_get_bool(&locked->attrs, "is_locked", false));
    TEST_ASSERT_EQUAL_STRING("rare", attr_get_string(&locked->attrs, "loot_table", ""));

    /* Inherited from parent */
    TEST_ASSERT_EQUAL_STRING("static", attr_get_string(&locked->attrs, "behavior", ""));
    TEST_ASSERT_EQUAL_STRING("chest.png", locked->texture_name);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, locked->source.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, locked->collision_size.x);

    toml_free(root);
    arena_free(&test_arena);
}

void test_blueprint_extends_chain(void)
{
    Arena test_arena;
    with_arena(&test_arena);
    BlueprintTable table;

    toml_table_t *root = parse_toml("[[blueprint]]\n"
                                    "name = \"base\"\n"
                                    "texture = \"base.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n"
                                    "behavior = \"static\"\n"
                                    "tier = 1\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"mid\"\n"
                                    "extends = \"base\"\n"
                                    "tier = 2\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"top\"\n"
                                    "extends = \"mid\"\n"
                                    "tier = 3\n");
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&table, root, &test_arena);
    const Blueprint *top = blueprint_find(&table, "top");
    TEST_ASSERT_NOT_NULL(top);

    /* Own attr */
    TEST_ASSERT_EQUAL_INT(3, attr_get_int(&top->attrs, "tier", 0));

    /* Inherited through chain */
    TEST_ASSERT_EQUAL_STRING("static", attr_get_string(&top->attrs, "behavior", ""));
    TEST_ASSERT_EQUAL_STRING("base.png", top->texture_name);

    toml_free(root);
    arena_free(&test_arena);
}

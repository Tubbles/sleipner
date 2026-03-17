#include "unity.h"
#include "arena.h"
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

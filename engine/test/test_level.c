#include "unity.h"
#include "arena.h"
#include "blueprint.h"
#include "level.h"

#include "toml.h"

#include <string.h>

static const char *test_gamedata = "[[blueprint]]\n"
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
                                   "collision_size = [16, 16]\n"
                                   "\n"
                                   "[[level]]\n"
                                   "name = \"overworld\"\n"
                                   "size = [640, 360]\n"
                                   "music = \"bgm.mp3\"\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"tree\"\n"
                                   "pos = [200, 60]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"chest\"\n"
                                   "pos = [300, 100]\n"
                                   "\n"
                                   "[[level]]\n"
                                   "name = \"dungeon\"\n"
                                   "size = [320, 240]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"chest\"\n"
                                   "pos = [50, 50]\n";

/* Dummy textures for testing */
static Texture2D dummy_tree_texture;
static Texture2D dummy_chest_texture;

static Texture2D *test_texture_lookup(const char *texture_name, void *user_data)
{
    (void)user_data;
    if (strcmp(texture_name, "tree.png") == 0) {
        return &dummy_tree_texture;
    }
    if (strcmp(texture_name, "chest.png") == 0) {
        return &dummy_chest_texture;
    }
    return NULL;
}

static toml_table_t *parse_toml(const char *input)
{
    char buffer[4096];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    char errbuf[200];
    return toml_parse(buffer, errbuf, (int)sizeof(errbuf));
}

void test_level_load_first(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 4096));
    BlueprintTable blueprints;
    Level level;

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&blueprints, root, &arena);

    bool loaded = level_load(&level, root, NULL, &blueprints, test_texture_lookup, NULL);
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("overworld", level.name);
    TEST_ASSERT_EQUAL_STRING("bgm.mp3", level.music_name);
    TEST_ASSERT_EQUAL_INT(640, level.width);
    TEST_ASSERT_EQUAL_INT(360, level.height);
    TEST_ASSERT_EQUAL_INT(2, level.entity_count);

    toml_free(root);
    arena_free(&arena);
}

void test_level_load_by_name(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 4096));
    BlueprintTable blueprints;
    Level level;

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&blueprints, root, &arena);

    bool loaded = level_load(&level, root, "dungeon", &blueprints, test_texture_lookup, NULL);
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("dungeon", level.name);
    TEST_ASSERT_EQUAL_INT(320, level.width);
    TEST_ASSERT_EQUAL_INT(240, level.height);
    TEST_ASSERT_EQUAL_INT(1, level.entity_count);

    toml_free(root);
    arena_free(&arena);
}

void test_level_load_nonexistent(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 4096));
    BlueprintTable blueprints;
    Level level;

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&blueprints, root, &arena);

    bool loaded = level_load(&level, root, "nonexistent", &blueprints, test_texture_lookup, NULL);
    TEST_ASSERT_FALSE(loaded);

    toml_free(root);
    arena_free(&arena);
}

void test_level_entity_positions(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 4096));
    BlueprintTable blueprints;
    Level level;

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&level, root, "overworld", &blueprints, test_texture_lookup, NULL));

    /* Tree at (200, 60) with collision_offset (20, 60) and collision_size (24, 16) */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f, level.entities[0].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, level.entities[0].position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 220.0f, level.entities[0].collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, level.entities[0].collision.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, level.entities[0].collision.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, level.entities[0].collision.height);
    TEST_ASSERT_TRUE(level.entities[0].texture == &dummy_tree_texture);

    /* Chest at (300, 100) with collision_offset (0, 0) and collision_size (16, 16) */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, level.entities[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, level.entities[1].position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, level.entities[1].collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, level.entities[1].collision.y);
    TEST_ASSERT_TRUE(level.entities[1].texture == &dummy_chest_texture);

    toml_free(root);
    arena_free(&arena);
}

void test_level_entity_source_rects(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&arena, 4096));
    BlueprintTable blueprints;
    Level level;

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&level, root, "overworld", &blueprints, test_texture_lookup, NULL));

    /* Tree source rect from blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, level.entities[0].source.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, level.entities[0].source.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 64.0f, level.entities[0].source.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, level.entities[0].source.height);

    /* Chest source rect from blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, level.entities[1].source.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, level.entities[1].source.height);

    toml_free(root);
    arena_free(&arena);
}

#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "arena.h"
#include "blueprint.h"
#include "entity.h"
#include "level.h"
#include "helpers_test.h"

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
static Texture2D dummy_lantern_texture;
static Texture2D dummy_wheel_texture;
static Texture2D dummy_wagon_texture;
static Texture2D dummy_part_texture;

static Texture2D *test_texture_lookup(const char *texture_name, void *user_data)
{
    (void)user_data;
    if (strcmp(texture_name, "tree.png") == 0) {
        return &dummy_tree_texture;
    }
    if (strcmp(texture_name, "chest.png") == 0) {
        return &dummy_chest_texture;
    }
    if (strcmp(texture_name, "lantern.png") == 0) {
        return &dummy_lantern_texture;
    }
    if (strcmp(texture_name, "wheel.png") == 0) {
        return &dummy_wheel_texture;
    }
    if (strcmp(texture_name, "wagon.png") == 0) {
        return &dummy_wagon_texture;
    }
    if (strcmp(texture_name, "part.png") == 0) {
        return &dummy_part_texture;
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
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);

    bool loaded = level_load(&ctx, &level, root, NULL, &blueprints, test_texture_lookup, NULL, NULL);
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("overworld", level.name.ptr);
    TEST_ASSERT_EQUAL_STRING("bgm.mp3", level.music_name.ptr);
    TEST_ASSERT_EQUAL_INT(640, level.width);
    TEST_ASSERT_EQUAL_INT(360, level.height);
    TEST_ASSERT_EQUAL_INT(2, level.entities.count);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_load_by_name(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);

    bool loaded = level_load(&ctx, &level, root, "dungeon", &blueprints, test_texture_lookup, NULL, NULL);
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("dungeon", level.name.ptr);
    TEST_ASSERT_EQUAL_INT(320, level.width);
    TEST_ASSERT_EQUAL_INT(240, level.height);
    TEST_ASSERT_EQUAL_INT(1, level.entities.count);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_load_nonexistent(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);

    bool loaded = level_load(&ctx, &level, root, "nonexistent", &blueprints, test_texture_lookup, NULL, NULL);
    TEST_ASSERT_FALSE(loaded);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_entity_positions(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "overworld", &blueprints, test_texture_lookup, NULL, NULL));

    /* Tree at (200, 60) with collision_offset (20, 60) and collision_size (24, 16) */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f, level.entities.data[0].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, level.entities.data[0].position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 220.0f, level.entities.data[0].collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, level.entities.data[0].collision.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, level.entities.data[0].collision.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, level.entities.data[0].collision.height);
    TEST_ASSERT_TRUE(level.entities.data[0].texture == &dummy_tree_texture);

    /* Chest at (300, 100) with collision_offset (0, 0) and collision_size (16, 16) */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, level.entities.data[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, level.entities.data[1].position.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 300.0f, level.entities.data[1].collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, level.entities.data[1].collision.y);
    TEST_ASSERT_TRUE(level.entities.data[1].texture == &dummy_chest_texture);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_entity_source_rects(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(test_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "overworld", &blueprints, test_texture_lookup, NULL, NULL));

    /* Tree source rect from blueprint */
    Rectangle src0 = entity_get_source(&level.entities.data[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, src0.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, src0.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 64.0f, src0.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, src0.height);

    /* Chest source rect from blueprint */
    Rectangle src1 = entity_get_source(&level.entities.data[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, src1.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, src1.height);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

static const char *child_gamedata = "[[blueprint]]\n"
                                    "name = \"lantern\"\n"
                                    "texture = \"lantern.png\"\n"
                                    "src = [0, 0, 8, 8]\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"wheel\"\n"
                                    "texture = \"wheel.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "\n"
                                    "[[blueprint]]\n"
                                    "name = \"wagon\"\n"
                                    "texture = \"wagon.png\"\n"
                                    "src = [0, 0, 64, 32]\n"
                                    "\n"
                                    "[[blueprint.child]]\n"
                                    "blueprint = \"lantern\"\n"
                                    "tag = \"light\"\n"
                                    "offset = [56, -8]\n"
                                    "\n"
                                    "[[blueprint.child]]\n"
                                    "blueprint = \"wheel\"\n"
                                    "tag = \"front_wheel\"\n"
                                    "offset = [8, 28]\n"
                                    "\n"
                                    "[[level]]\n"
                                    "name = \"test\"\n"
                                    "size = [320, 240]\n"
                                    "\n"
                                    "[[level.entity]]\n"
                                    "blueprint = \"wagon\"\n"
                                    "pos = [100, 50]\n";

void test_level_child_entities_instantiated(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(child_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "test", &blueprints, test_texture_lookup, NULL, NULL));

    /* 1 parent (wagon) + 2 children (lantern, wheel) */
    TEST_ASSERT_EQUAL_INT(3, level.entities.count);

    /* Parent has no parent */
    TEST_ASSERT_EQUAL_INT(-1, level.entities.data[0].parent_index);

    /* Children point back to parent */
    TEST_ASSERT_EQUAL_INT(0, level.entities.data[1].parent_index);
    TEST_ASSERT_EQUAL_INT(0, level.entities.data[2].parent_index);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_child_entity_positions(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(child_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "test", &blueprints, test_texture_lookup, NULL, NULL));

    /* Lantern at wagon(100,50) + offset(56,-8) = (156, 42) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 156.0F, level.entities.data[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 42.0F, level.entities.data[1].position.y);

    /* Wheel at wagon(100,50) + offset(8,28) = (108, 78) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 108.0F, level.entities.data[2].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 78.0F, level.entities.data[2].position.y);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_child_entity_tags(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    toml_table_t *root = parse_toml(child_gamedata);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "test", &blueprints, test_texture_lookup, NULL, NULL));

    TEST_ASSERT_EQUAL_STRING("light", level.entities.data[1].tag.ptr);
    TEST_ASSERT_EQUAL_STRING("front_wheel", level.entities.data[2].tag.ptr);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

void test_level_nested_children(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena));
    BlueprintTable blueprints;
    Level level = {0};

    const char *nested_data = "[[blueprint]]\n"
                              "name = \"part\"\n"
                              "texture = \"part.png\"\n"
                              "src = [0, 0, 4, 4]\n"
                              "\n"
                              "[[blueprint]]\n"
                              "name = \"mid\"\n"
                              "texture = \"part.png\"\n"
                              "src = [0, 0, 8, 8]\n"
                              "\n"
                              "[[blueprint.child]]\n"
                              "blueprint = \"part\"\n"
                              "tag = \"inner\"\n"
                              "offset = [2, 2]\n"
                              "\n"
                              "[[blueprint]]\n"
                              "name = \"outer\"\n"
                              "texture = \"part.png\"\n"
                              "src = [0, 0, 16, 16]\n"
                              "\n"
                              "[[blueprint.child]]\n"
                              "blueprint = \"mid\"\n"
                              "tag = \"middle\"\n"
                              "offset = [4, 4]\n"
                              "\n"
                              "[[level]]\n"
                              "name = \"test\"\n"
                              "size = [100, 100]\n"
                              "\n"
                              "[[level.entity]]\n"
                              "blueprint = \"outer\"\n"
                              "pos = [10, 10]\n";

    toml_table_t *root = parse_toml(nested_data);
    TEST_ASSERT_NOT_NULL(root);

    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "test", &blueprints, test_texture_lookup, NULL, NULL));

    /* outer(0) -> mid(1) -> part(2) */
    TEST_ASSERT_EQUAL_INT(3, level.entities.count);

    TEST_ASSERT_EQUAL_INT(-1, level.entities.data[0].parent_index);
    TEST_ASSERT_EQUAL_INT(0, level.entities.data[1].parent_index);
    TEST_ASSERT_EQUAL_INT(1, level.entities.data[2].parent_index);

    /* outer at (10,10), mid at (10+4, 10+4) = (14, 14) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 14.0F, level.entities.data[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 14.0F, level.entities.data[1].position.y);

    /* part at (14+2, 14+2) = (16, 16) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, level.entities.data[2].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, level.entities.data[2].position.y);

    TEST_ASSERT_EQUAL_STRING("middle", level.entities.data[1].tag.ptr);
    TEST_ASSERT_EQUAL_STRING("inner", level.entities.data[2].tag.ptr);

    test_level_free(&level);
    (void)blueprints;
    toml_free(root);
    arena_free(&arena);
}

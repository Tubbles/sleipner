#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "toml_emitter.h"
#include "test_helpers.h"
#include "arena.h"
#include "toml.h"

#include <string.h>

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

/* Helper: parse TOML, load blueprints and level, emit, re-parse, verify round-trip */
static const char *fixture_gamedata = "[[blueprint]]\n"
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
                                      "pos = [300, 100]\n";

static toml_table_t *parse_toml(const char *input)
{
    char buffer[8192];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    char errbuf[200];
    return toml_parse(buffer, errbuf, (int)sizeof(errbuf));
}

void test_toml_emit_blueprints(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 4096));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&ctx, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &blueprints, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    /* Verify the output contains the blueprint data */
    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"tree\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "texture = \"chest.png\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "src = [0, 0, 64, 80]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "collision_offset = [20, 60]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "collision_size = [24, 16]"));

    test_blueprint_table_free(&ctx, &blueprints);
    arena_free(&arena);
}

void test_toml_emit_level_with_entities(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 4096));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, NULL, &blueprints, dummy_lookup, NULL));
    toml_free(root);

    char output[4096];
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &blueprints, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[level]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"overworld\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "size = [640, 360]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "music = \"bgm.mp3\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "[[level.entity]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "blueprint = \"tree\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "pos = [200, 60]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "blueprint = \"chest\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "pos = [300, 100]"));

    test_level_free(&ctx, &level);
    test_blueprint_table_free(&ctx, &blueprints);
    arena_free(&arena);
}

void test_toml_emit_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 4096));
    BlueprintTable blueprints = {0};
    Level level = {0};

    /* Parse original */
    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, NULL, &blueprints, dummy_lookup, NULL));
    toml_free(root);

    /* Emit */
    char output[8192];
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &blueprints, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Re-parse the emitted output */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena2, 4096));
    BlueprintTable blueprints2 = {0};
    Level level2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&ctx, &blueprints2, root2, &arena2);
    TEST_ASSERT_TRUE(level_load(&ctx, &level2, root2, NULL, &blueprints2, dummy_lookup, NULL));
    toml_free(root2);

    /* Verify round-trip preserves data */
    TEST_ASSERT_EQUAL_INT(blueprints.entries.count, blueprints2.entries.count);
    for (int index = 0; index < blueprints.entries.count; index++) {
        TEST_ASSERT_EQUAL_STRING(blueprints.entries.data[index].name.ptr, blueprints2.entries.data[index].name.ptr);
        TEST_ASSERT_EQUAL_STRING(blueprints.entries.data[index].texture_name.ptr,
                                 blueprints2.entries.data[index].texture_name.ptr);
        TEST_ASSERT_FLOAT_WITHIN(0.1F, blueprints.entries.data[index].source.width,
                                 blueprints2.entries.data[index].source.width);
    }

    TEST_ASSERT_EQUAL_STRING(level.name.ptr, level2.name.ptr);
    TEST_ASSERT_EQUAL_STRING(level.music_name.ptr, level2.music_name.ptr);
    TEST_ASSERT_EQUAL_INT(level.width, level2.width);
    TEST_ASSERT_EQUAL_INT(level.height, level2.height);
    TEST_ASSERT_EQUAL_INT(level.entity_count, level2.entity_count);

    for (int index = 0; index < level.entity_count; index++) {
        TEST_ASSERT_FLOAT_WITHIN(0.1F, level.entities[index].position.x, level2.entities[index].position.x);
        TEST_ASSERT_FLOAT_WITHIN(0.1F, level.entities[index].position.y, level2.entities[index].position.y);
        TEST_ASSERT_EQUAL_STRING(level.entities[index].blueprint_name.ptr, level2.entities[index].blueprint_name.ptr);
    }

    test_level_free(&ctx, &level);
    test_level_free(&ctx, &level2);
    test_blueprint_table_free(&ctx, &blueprints);
    test_blueprint_table_free(&ctx, &blueprints2);
    arena_free(&arena);
    arena_free(&arena2);
}

void test_toml_emit_buffer_too_small(void)
{
    BlueprintTable blueprints = {0};
    TEST_ASSERT_TRUE(vec_blueprint_push(&blueprints.entries, (Blueprint){0}, NULL));
    Blueprint *entry = &blueprints.entries.data[0];
    TEST_ASSERT_TRUE(str_from_cstr(&ctx, &entry->name, "test"));
    TEST_ASSERT_TRUE(str_from_cstr(&ctx, &entry->texture_name, "test.png"));

    char tiny[10];
    int written = toml_emit_gamedata(&ctx, tiny, (int)sizeof(tiny), &blueprints, NULL, 0);
    TEST_ASSERT_EQUAL_INT(-1, written);
    test_blueprint_table_free(&ctx, &blueprints);
}

static const char *child_fixture = "[[blueprint]]\n"
                                   "name = \"lantern\"\n"
                                   "texture = \"lantern.png\"\n"
                                   "src = [0, 0, 8, 8]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [0, 0]\n"
                                   "\n"
                                   "[[blueprint]]\n"
                                   "name = \"wagon\"\n"
                                   "texture = \"wagon.png\"\n"
                                   "src = [0, 0, 64, 32]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [0, 0]\n"
                                   "\n"
                                   "[[blueprint.child]]\n"
                                   "blueprint = \"lantern\"\n"
                                   "tag = \"light\"\n"
                                   "offset = [56, -8]\n"
                                   "\n"
                                   "[[level]]\n"
                                   "name = \"test\"\n"
                                   "size = [320, 240]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"wagon\"\n"
                                   "pos = [100, 50]\n";

void test_toml_emit_blueprint_children(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 4096));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(child_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&ctx, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &blueprints, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint.child]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "tag = \"light\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "offset = [56, -8]"));

    test_blueprint_table_free(&ctx, &blueprints);
    arena_free(&arena);
}

void test_toml_emit_skips_child_entities(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&ctx, &arena, 4096));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(child_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&ctx, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(level_load(&ctx, &level, root, "test", &blueprints, dummy_lookup, NULL));
    toml_free(root);

    /* Level has 2 entities (wagon + lantern child) */
    TEST_ASSERT_EQUAL_INT(2, level.entity_count);

    char output[4096];
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &blueprints, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Only the parent wagon should appear as a level.entity */
    TEST_ASSERT_NOT_NULL(strstr(output, "blueprint = \"wagon\""));

    /* Count occurrences of [[level.entity]] — should be 1 */
    int count = 0;
    const char *search = output;
    while ((search = strstr(search, "[[level.entity]]")) != NULL) {
        count++;
        search += 16;
    }
    TEST_ASSERT_EQUAL_INT(1, count);

    test_level_free(&ctx, &level);
    test_blueprint_table_free(&ctx, &blueprints);
    arena_free(&arena);
}

void test_toml_emit_no_music(void)
{
    Level level = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&ctx, &level.name, "silent"));
    level.width = 100;
    level.height = 100;

    BlueprintTable empty = {0};
    char output[1024];
    int written = toml_emit_gamedata(&ctx, output, (int)sizeof(output), &empty, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Should not contain music line */
    TEST_ASSERT_NULL(strstr(output, "music"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"silent\""));
    test_level_free(&ctx, &level);
}

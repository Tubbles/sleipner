#include "unity.h"
#include "debug.h"
#include "diag.h"
#include "error.h"

static ErrorState test_err;
static DebugState test_dbg;
static Diag test_diag = {&test_err, &test_dbg};

#include "toml_emitter.h"
#include "test_helpers.h"
#include "arena.h"
#include "atlas.h"
#include "collision.h"
#include "input_func.h"
#include "rule.h"
#include "tileset.h"
#include "toml.h"

#include <stdio.h>
#include <string.h>

static Texture2D dummy_texture;
static const vec_subroutine empty_subroutines = {0};
static const vec_tileset_entry empty_tileset = {0};
static const vec_atlas_region empty_atlas_regions = {0};

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
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    /* Verify the output contains the blueprint data */
    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"tree\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "texture = \"chest.png\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "src = [0, 0, 64, 80]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "collision_offset = [20, 60]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "collision_size = [24, 16]"));

    arena_free(&arena);
}

void test_toml_emit_level_with_entities(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, nullptr, &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
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

    test_level_free(&level);
    arena_free(&arena);
}

void test_toml_emit_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    /* Parse original */
    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, nullptr, &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    /* Emit */
    char output[8192];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Re-parse the emitted output */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};
    Level level2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level2, root2, nullptr, &blueprints2, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root2);

    /* Verify round-trip preserves data */
    TEST_ASSERT_EQUAL_INT(blueprints.entries.count, blueprints2.entries.count);
    for (int index = 0; index < blueprints.entries.count; index++) {
        const Blueprint *bp1 = &blueprints.entries.data[index];
        const Blueprint *bp2 = &blueprints2.entries.data[index];
        TEST_ASSERT_EQUAL_STRING(attr_get_string(&bp1->attrs, "name"), attr_get_string(&bp2->attrs, "name"));
        TEST_ASSERT_EQUAL_STRING(attr_get_string(&bp1->attrs, "texture"), attr_get_string(&bp2->attrs, "texture"));
        TEST_ASSERT_FLOAT_WITHIN(0.1F, attr_get_float(&bp1->attrs, "src_w", -1.0F),
                                 attr_get_float(&bp2->attrs, "src_w", -1.0F));
    }

    TEST_ASSERT_EQUAL_STRING(level.name.ptr, level2.name.ptr);
    TEST_ASSERT_EQUAL_STRING(level.music_name.ptr, level2.music_name.ptr);
    TEST_ASSERT_EQUAL_INT(level.width, level2.width);
    TEST_ASSERT_EQUAL_INT(level.height, level2.height);
    TEST_ASSERT_EQUAL_INT(level.entities.count, level2.entities.count);

    for (int index = 0; index < level.entities.count; index++) {
        TEST_ASSERT_FLOAT_WITHIN(0.1F, level.entities.data[index].position.x, level2.entities.data[index].position.x);
        TEST_ASSERT_FLOAT_WITHIN(0.1F, level.entities.data[index].position.y, level2.entities.data[index].position.y);
        TEST_ASSERT_EQUAL_STRING(level.entities.data[index].blueprint_name.ptr,
                                 level2.entities.data[index].blueprint_name.ptr);
    }

    test_level_free(&level);
    test_level_free(&level2);
    arena_free(&arena);
    arena_free(&arena2);
}

void test_toml_emit_buffer_too_small(void)
{
    BlueprintTable blueprints = {0};
    blueprints.entries.alloc = test_heap_alloc;
    TEST_ASSERT_TRUE(vec_blueprint_push(&blueprints.entries, (Blueprint){0}));
    Blueprint *entry = &blueprints.entries.data[0];
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &entry->attrs, (AttrStringPair){.name = "name", .value = "test"}));
    TEST_ASSERT_TRUE(
        attr_set_string(&test_heap_alloc, &entry->attrs, (AttrStringPair){.name = "texture", .value = "test.png"}));

    char tiny[10];
    int written = toml_emit_gamedata(&test_err, tiny, (int)sizeof(tiny), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(-1, written);
    test_blueprint_table_free(&blueprints);
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
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(child_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint.child]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "tag = \"light\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "offset = [56, -8]"));

    arena_free(&arena);
}

void test_toml_emit_skips_child_entities(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(child_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, "test", &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    /* Level has 2 entities (wagon + lantern child) */
    TEST_ASSERT_EQUAL_INT(2, level.entities.count);

    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Only the parent wagon should appear as a level.entity */
    TEST_ASSERT_NOT_NULL(strstr(output, "blueprint = \"wagon\""));

    /* Count occurrences of [[level.entity]] — should be 1 */
    int count = 0;
    const char *search = output;
    while ((search = strstr(search, "[[level.entity]]")) != nullptr) {
        count++;
        search += 16;
    }
    TEST_ASSERT_EQUAL_INT(1, count);

    test_level_free(&level);
    arena_free(&arena);
}

void test_toml_emit_no_music(void)
{
    Level level = {0};
    level.name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&level.name, "silent"));
    level.width = 100;
    level.height = 100;

    BlueprintTable empty = {0};
    char output[1024];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &empty, &empty_subroutines, &empty_tileset,
                                     &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Should not contain music line */
    TEST_ASSERT_NULL(strstr(output, "music"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"silent\""));
    test_level_free(&level);
}

static const char *custom_attr_fixture = "[[blueprint]]\n"
                                         "name = \"gate\"\n"
                                         "texture = \"gate.png\"\n"
                                         "src = [0, 0, 16, 32]\n"
                                         "collision_offset = [0, 0]\n"
                                         "collision_size = [16, 32]\n"
                                         "behavior = \"static\"\n"
                                         "score = 7\n"
                                         "\n"
                                         "[[level]]\n"
                                         "name = \"test\"\n"
                                         "size = [320, 240]\n";

void test_toml_emit_custom_attrs(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(custom_attr_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "behavior = \"static\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "score = 7"));

    /* Round-trip: re-parse and verify custom attrs survive */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    const Blueprint *blueprint = &blueprints2.entries.data[0];
    TEST_ASSERT_EQUAL_STRING("static", attr_get_string(&blueprint->attrs, "behavior"));
    TEST_ASSERT_EQUAL_INT(7, attr_get_int(&blueprint->attrs, "score", -1));

    arena_free(&arena);
    arena_free(&arena2);
}

static const char *health_fixture = "[[blueprint]]\n"
                                    "name = \"knight\"\n"
                                    "texture = \"knight.png\"\n"
                                    "src = [0, 0, 16, 16]\n"
                                    "collision_offset = [0, 0]\n"
                                    "collision_size = [16, 16]\n"
                                    "health = [10, 50]\n"
                                    "\n"
                                    "[[level]]\n"
                                    "name = \"test\"\n"
                                    "size = [320, 240]\n";

void test_toml_emit_health(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(health_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "health = [10, 50]"));

    /* Round-trip: re-parse and verify health values survive */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    const Blueprint *blueprint = &blueprints2.entries.data[0];
    TEST_ASSERT_EQUAL_INT(10, attr_get_int(&blueprint->attrs, "health", -1));
    TEST_ASSERT_EQUAL_INT(50, attr_get_int(&blueprint->attrs, "max_health", -1));

    arena_free(&arena);
    arena_free(&arena2);
}

static const char *animation_fixture = "[[blueprint]]\n"
                                       "name = \"player\"\n"
                                       "texture = \"player.png\"\n"
                                       "src = [0, 0, 32, 32]\n"
                                       "collision_offset = [0, 0]\n"
                                       "collision_size = [16, 16]\n"
                                       "animation = { frames = 6, size = 32, speed = 10, row = 2 }\n"
                                       "\n"
                                       "[[level]]\n"
                                       "name = \"test\"\n"
                                       "size = [320, 240]\n";

void test_toml_emit_animation_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(animation_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(1, blueprints.entries.count);
    const Blueprint *original = &blueprints.entries.data[0];
    TEST_ASSERT_EQUAL_INT(6, attr_get_int(&original->attrs, "anim_frames", -1));
    TEST_ASSERT_EQUAL_INT(32, attr_get_int(&original->attrs, "anim_size", -1));
    TEST_ASSERT_EQUAL_INT(10, attr_get_int(&original->attrs, "anim_speed", -1));
    TEST_ASSERT_EQUAL_INT(2, attr_get_int(&original->attrs, "anim_row", -1));

    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "animation = { frames = 6, size = 32, speed = 10, row = 2 }"));

    /* Round-trip: re-parse and verify anim_* attrs survive */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    const Blueprint *blueprint = &blueprints2.entries.data[0];
    TEST_ASSERT_EQUAL_INT(6, attr_get_int(&blueprint->attrs, "anim_frames", -1));
    TEST_ASSERT_EQUAL_INT(32, attr_get_int(&blueprint->attrs, "anim_size", -1));
    TEST_ASSERT_EQUAL_INT(10, attr_get_int(&blueprint->attrs, "anim_speed", -1));
    TEST_ASSERT_EQUAL_INT(2, attr_get_int(&blueprint->attrs, "anim_row", -1));

    arena_free(&arena);
    arena_free(&arena2);
}

static const char *persisted_attr_fixture = "[[blueprint]]\n"
                                            "name = \"chest\"\n"
                                            "texture = \"chest.png\"\n"
                                            "src = [0, 0, 16, 16]\n"
                                            "collision_offset = [0, 0]\n"
                                            "collision_size = [16, 16]\n"
                                            "\n"
                                            "[[level]]\n"
                                            "name = \"test\"\n"
                                            "size = [320, 240]\n"
                                            "\n"
                                            "[[level.entity]]\n"
                                            "blueprint = \"chest\"\n"
                                            "pos = [40, 60]\n"
                                            "opened = true\n"
                                            "coins = 25\n"
                                            "owner = \"hero\"\n";

void test_toml_emit_persisted_attrs(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(persisted_attr_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, "test", &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    /* Confirm loader placed overrides in persisted_attrs, and runtime mirrors them */
    TEST_ASSERT_EQUAL_INT(1, level.entities.count);
    const Entity *entity = &level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&entity->persisted_attrs, "opened", 0));
    TEST_ASSERT_EQUAL_INT(25, attr_get_int(&entity->persisted_attrs, "coins", -1));
    TEST_ASSERT_EQUAL_STRING("hero", attr_get_string(&entity->persisted_attrs, "owner"));
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&entity->attrs, "opened", 0));
    TEST_ASSERT_EQUAL_INT(25, attr_get_int(&entity->attrs, "coins", -1));

    /* Emit and verify the persisted values appear on the [[level.entity]] line */
    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "blueprint = \"chest\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "pos = [40, 60]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "opened = true"));
    TEST_ASSERT_NOT_NULL(strstr(output, "coins = 25"));
    TEST_ASSERT_NOT_NULL(strstr(output, "owner = \"hero\""));

    /* Runtime mutation to attrs must NOT affect persisted emission */
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, (AttrSet *)&entity->attrs, "coins", 999));
    char output2[4096];
    int written2 = toml_emit_gamedata(&test_err, output2, (int)sizeof(output2), &blueprints, &empty_subroutines,
                                      &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written2 > 0);
    TEST_ASSERT_NOT_NULL(strstr(output2, "coins = 25"));
    TEST_ASSERT_NULL(strstr(output2, "coins = 999"));

    /* Round-trip: re-parse the emitted output and confirm persisted_attrs match */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};
    Level level2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level2, root2, "test", &blueprints2, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, level2.entities.count);
    const Entity *entity2 = &level2.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&entity2->persisted_attrs, "opened", 0));
    TEST_ASSERT_EQUAL_INT(25, attr_get_int(&entity2->persisted_attrs, "coins", -1));
    TEST_ASSERT_EQUAL_STRING("hero", attr_get_string(&entity2->persisted_attrs, "owner"));

    test_level_free(&level);
    test_level_free(&level2);
    arena_free(&arena);
    arena_free(&arena2);
}

void test_toml_emit_no_persisted_attrs(void)
{
    /* Entity without instance overrides — output should have no extra keys */
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    toml_table_t *root = parse_toml(fixture_gamedata);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, nullptr, &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    /* Persisted sets must be empty when no instance overrides were declared */
    for (int index = 0; index < level.entities.count; index++) {
        TEST_ASSERT_EQUAL_INT(0, level.entities.data[index].persisted_attrs.entries.count);
    }

    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* Fixture has no solid attr, so nothing should be emitted for it */
    TEST_ASSERT_NULL(strstr(output, "solid ="));

    test_level_free(&level);
    arena_free(&arena);
}

/* Regression fixture for D19/S3.3a: child-instance persisted attrs (declared
 * via `[level.entity.children.<tag>]`) used to be silently dropped on save.
 * `chest` has a tagged child `door`, which itself has a tagged grandchild
 * `hinge`, so the fixture exercises one level of nesting in addition to the
 * direct-child case. */
static const char *child_persisted_attr_fixture = "[[blueprint]]\n"
                                                  "name = \"hinge\"\n"
                                                  "texture = \"hinge.png\"\n"
                                                  "src = [0, 0, 8, 8]\n"
                                                  "collision_offset = [0, 0]\n"
                                                  "collision_size = [8, 8]\n"
                                                  "\n"
                                                  "[[blueprint]]\n"
                                                  "name = \"door\"\n"
                                                  "texture = \"door.png\"\n"
                                                  "src = [0, 0, 16, 16]\n"
                                                  "collision_offset = [0, 0]\n"
                                                  "collision_size = [16, 16]\n"
                                                  "\n"
                                                  "[[blueprint.child]]\n"
                                                  "blueprint = \"hinge\"\n"
                                                  "tag = \"hinge\"\n"
                                                  "offset = [8, 0]\n"
                                                  "\n"
                                                  "[[blueprint]]\n"
                                                  "name = \"chest\"\n"
                                                  "texture = \"chest.png\"\n"
                                                  "src = [0, 0, 16, 16]\n"
                                                  "collision_offset = [0, 0]\n"
                                                  "collision_size = [16, 16]\n"
                                                  "\n"
                                                  "[[blueprint.child]]\n"
                                                  "blueprint = \"door\"\n"
                                                  "tag = \"door\"\n"
                                                  "offset = [0, -8]\n"
                                                  "\n"
                                                  "[[level]]\n"
                                                  "name = \"test\"\n"
                                                  "size = [320, 240]\n"
                                                  "\n"
                                                  "[[level.entity]]\n"
                                                  "blueprint = \"chest\"\n"
                                                  "pos = [40, 60]\n"
                                                  "opened = true\n"
                                                  "\n"
                                                  "[level.entity.children.door]\n"
                                                  "locked = true\n"
                                                  "\n"
                                                  "[level.entity.children.door.children.hinge]\n"
                                                  "rusty = true\n";

static const Entity *find_entity_by_tag(const Level *level, const char *tag)
{
    for (int index = 0; index < level->entities.count; index++) {
        const Entity *candidate = &level->entities.data[index];
        if (candidate->tag.len > 0 && strcmp(candidate->tag.ptr, tag) == 0) {
            return candidate;
        }
    }
    return nullptr;
}

void test_toml_emit_child_persisted_attrs_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    Level level = {0};

    /* Parse original: child overrides must land on the instantiated door/hinge,
     * not just be silently ignored. */
    toml_table_t *root = parse_toml(child_persisted_attr_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, "test", &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(3, level.entities.count); /* chest + door + hinge */

    const Entity *door = find_entity_by_tag(&level, "door");
    TEST_ASSERT_NOT_NULL(door);
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&door->persisted_attrs, "locked", 0));
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&door->attrs, "locked", 0));

    const Entity *hinge = find_entity_by_tag(&level, "hinge");
    TEST_ASSERT_NOT_NULL(hinge);
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&hinge->persisted_attrs, "rusty", 0));
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&hinge->attrs, "rusty", 0));

    /* Emit and confirm the overrides made it into the TOML text. */
    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[level.entity.children.door]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "locked = true"));
    TEST_ASSERT_NOT_NULL(strstr(output, "[level.entity.children.door.children.hinge]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "rusty = true"));

    /* Re-parse the emitted output and confirm the overrides survive onto the
     * freshly instantiated door/hinge — the actual regression this guards. */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};
    Level level2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level2, root2, "test", &blueprints2, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(3, level2.entities.count);

    const Entity *door2 = find_entity_by_tag(&level2, "door");
    TEST_ASSERT_NOT_NULL(door2);
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&door2->persisted_attrs, "locked", 0));
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&door2->attrs, "locked", 0));

    const Entity *hinge2 = find_entity_by_tag(&level2, "hinge");
    TEST_ASSERT_NOT_NULL(hinge2);
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&hinge2->persisted_attrs, "rusty", 0));
    TEST_ASSERT_EQUAL_INT(1, attr_get_bool(&hinge2->attrs, "rusty", 0));

    test_level_free(&level);
    test_level_free(&level2);
    arena_free(&arena);
    arena_free(&arena2);
}

static const char *rule_fixture = "[[blueprint]]\n"
                                  "name = \"chest\"\n"
                                  "texture = \"chest.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "conditions = [\"not_flag:chest_opened\"]\n"
                                  "actions = [\"set_flag:chest_opened\", \"destroy\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n";

void test_toml_emit_rules(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(rule_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[8192];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint.rule]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "trigger = \"interact\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "\"not_flag:chest_opened\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "\"set_flag:chest_opened\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "\"destroy\""));

    /* Round-trip: re-parse and verify rule structure survives */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    const Blueprint *blueprint = &blueprints2.entries.data[0];
    TEST_ASSERT_EQUAL_INT(1, blueprint->rules.count);

    const Rule *rule = &blueprint->rules.data[0];
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rule->trigger.type);
    TEST_ASSERT_EQUAL_INT(1, rule->conditions.count);
    TEST_ASSERT_EQUAL_INT(COND_NOT_FLAG, rule->conditions.data[0].type);
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, rule->action_tree.nodes.data[0].type);
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, rule->action_tree.nodes.data[1].type);

    arena_free(&arena);
    arena_free(&arena2);
}

/* S6.5's camera_pan/camera_shake actions carry three and two comma-separated
 * values respectively (parse_action_two_args splits at the first comma
 * only, so the rest lands in second_argument). The emit table must mark
 * both ACTION_EMIT_TWO_ARGS (like change_sprite/transition) so re-saving a
 * rule via the editor doesn't silently drop the trailing value(s). */
static const char *camera_action_fixture = "[[blueprint]]\n"
                                           "name = \"trigger_zone\"\n"
                                           "texture = \"trigger.png\"\n"
                                           "src = [0, 0, 16, 16]\n"
                                           "collision_offset = [0, 0]\n"
                                           "collision_size = [16, 16]\n"
                                           "\n"
                                           "[[blueprint.rule]]\n"
                                           "trigger = \"interact\"\n"
                                           "actions = [\"camera_pan:10,20,1.5\", \"camera_shake:3,0.5\"]\n"
                                           "\n"
                                           "[[level]]\n"
                                           "name = \"test\"\n"
                                           "size = [320, 240]\n";

void test_toml_emit_camera_pan_shake_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    toml_table_t *root = parse_toml(camera_action_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    char output[8192];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    /* Full argument set must survive emit -- ACTION_EMIT_ONE_ARG truncates
     * to "camera_pan:10" / "camera_shake:3", silently dropping the rest. */
    TEST_ASSERT_NOT_NULL(strstr(output, "\"camera_pan:10,20,1.5\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "\"camera_shake:3,0.5\""));

    /* Round-trip: re-parse and verify both actions' full argument sets survive */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    const Blueprint *blueprint = &blueprints2.entries.data[0];
    TEST_ASSERT_EQUAL_INT(1, blueprint->rules.count);

    const Rule *rule = &blueprint->rules.data[0];
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.nodes.count);

    const ActionNode *pan_node = &rule->action_tree.nodes.data[0];
    TEST_ASSERT_EQUAL_INT(ACTION_CAMERA_PAN, pan_node->type);
    TEST_ASSERT_EQUAL_STRING("10", pan_node->argument.ptr);
    TEST_ASSERT_EQUAL_STRING("20,1.5", pan_node->second_argument.ptr);

    const ActionNode *shake_node = &rule->action_tree.nodes.data[1];
    TEST_ASSERT_EQUAL_INT(ACTION_CAMERA_SHAKE, shake_node->type);
    TEST_ASSERT_EQUAL_STRING("3", shake_node->argument.ptr);
    TEST_ASSERT_EQUAL_STRING("0.5", shake_node->second_argument.ptr);

    arena_free(&arena);
    arena_free(&arena2);
}

/* S4.5/D28: [[blueprint.collision]] authors a composite collision shape (a
 * list of primitives) on a blueprint. Two primitives of different kinds
 * (rect + circle) with non-zero offsets exercises both the per-kind field
 * parsing and the offset default handling in one fixture. */
static const char *collision_composite_fixture = "[[blueprint]]\n"
                                                 "name = \"boulder\"\n"
                                                 "texture = \"boulder.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [32, 32]\n"
                                                 "\n"
                                                 "[[blueprint.collision]]\n"
                                                 "kind = \"rect\"\n"
                                                 "offset = [-8, 0]\n"
                                                 "size = [16, 24]\n"
                                                 "angle = 15\n"
                                                 "\n"
                                                 "[[blueprint.collision]]\n"
                                                 "kind = \"circle\"\n"
                                                 "offset = [8, -4]\n"
                                                 "radius = 10\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"test\"\n"
                                                 "size = [320, 240]\n";

static void assert_collision_composite_structure(const Blueprint *blueprint)
{
    TEST_ASSERT_EQUAL_INT(2, blueprint->collision.prims.count);

    const CollisionPrimitive *rect_prim = &blueprint->collision.prims.data[0];
    TEST_ASSERT_EQUAL_INT(COLLIDER_RECT, rect_prim->kind);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -8.0F, rect_prim->offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, rect_prim->offset.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.0F, rect_prim->rect.half_w);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 12.0F, rect_prim->rect.half_h);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 15.0F, rect_prim->angle_offset);

    const CollisionPrimitive *circle_prim = &blueprint->collision.prims.data[1];
    TEST_ASSERT_EQUAL_INT(COLLIDER_CIRCLE, circle_prim->kind);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.0F, circle_prim->offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -4.0F, circle_prim->offset.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, circle_prim->circle.radius);
}

void test_toml_emit_collision_composite_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    /* Parse original and sanity-check the fixture actually parses two
     * distinct primitive kinds with non-zero offsets. */
    toml_table_t *root = parse_toml(collision_composite_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(1, blueprints.entries.count);
    assert_collision_composite_structure(&blueprints.entries.data[0]);

    /* Emit, then re-parse the emitted TOML into a second tree. */
    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[blueprint.collision]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "kind = \"rect\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "kind = \"circle\""));

    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    /* The round trip must preserve prim count, kinds, offsets, and
     * sizes/radius for every primitive, not just the first. */
    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    assert_collision_composite_structure(&blueprints2.entries.data[0]);

    arena_free(&arena);
    arena_free(&arena2);
}

/* Three levels of control-flow nesting: if -> then=[repeat] -> do=[for_each]
 * -> do=["destroy"]. Regression fixture for F25 (nested control flow inside
 * a control-flow child silently dropped by the one-level-deep emitter). */
static const char *nested_control_flow_fixture =
    "[[blueprint]]\n"
    "name = \"spawner\"\n"
    "texture = \"spawner.png\"\n"
    "src = [0, 0, 16, 16]\n"
    "collision_offset = [0, 0]\n"
    "collision_size = [16, 16]\n"
    "\n"
    "[[blueprint.rule]]\n"
    "trigger = \"interact\"\n"
    "actions = [{ if = [\"flag:test_flag\"], then = [{ repeat = \"3\", do = [{ for_each = \"entities\", bind = "
    "\"target\", do = [\"destroy\"] }] }] }]\n"
    "\n"
    "[[level]]\n"
    "name = \"test\"\n"
    "size = [320, 240]\n";

/* Walks the known if -> repeat -> for_each -> destroy chain and asserts every
 * level survived intact, including the deepest (4th) node. */
static void assert_nested_control_flow_structure(const ActionTree *tree)
{
    TEST_ASSERT_EQUAL_INT(4, tree->nodes.count);
    TEST_ASSERT_EQUAL_INT(1, tree->roots.count);

    const ActionNode *if_node = &tree->nodes.data[tree->roots.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_IF_ELSE, if_node->type);
    TEST_ASSERT_EQUAL_INT(1, if_node->conditions.count);
    TEST_ASSERT_EQUAL_INT(1, if_node->children.count);

    const ActionNode *repeat_node = &tree->nodes.data[if_node->children.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_REPEAT, repeat_node->type);
    TEST_ASSERT_EQUAL_STRING("3", repeat_node->argument.ptr);
    TEST_ASSERT_EQUAL_INT(1, repeat_node->children.count);

    const ActionNode *for_each_node = &tree->nodes.data[repeat_node->children.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_FOR_EACH, for_each_node->type);
    TEST_ASSERT_EQUAL_STRING("entities", for_each_node->argument.ptr);
    TEST_ASSERT_EQUAL_STRING("target", for_each_node->second_argument.ptr);
    TEST_ASSERT_EQUAL_INT(1, for_each_node->children.count);

    const ActionNode *destroy_node = &tree->nodes.data[for_each_node->children.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, destroy_node->type);
}

void test_toml_emit_nested_control_flow_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};

    /* Parse original and sanity-check the fixture actually nests 3 levels deep. */
    toml_table_t *root = parse_toml(nested_control_flow_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(1, blueprints.entries.count);
    TEST_ASSERT_EQUAL_INT(1, blueprints.entries.data[0].rules.count);
    assert_nested_control_flow_structure(&blueprints.entries.data[0].rules.data[0].action_tree);

    /* Emit, then re-parse the emitted TOML into a second tree. */
    char output[4096];
    Level empty_level = {0};
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &empty_atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    toml_free(root2);

    /* The round trip must preserve the full 3-level nesting, not just the top level. */
    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.count);
    TEST_ASSERT_EQUAL_INT(1, blueprints2.entries.data[0].rules.count);
    assert_nested_control_flow_structure(&blueprints2.entries.data[0].rules.data[0].action_tree);

    arena_free(&arena);
    arena_free(&arena2);
}

/* Regression fixture for F25/D19: [[subroutine]] blocks were parsed but never
 * emitted, so a save silently dropped every authored subroutine. Includes an
 * if/then/else action so the round trip also exercises S3.1's recursive
 * control-flow emission inside a subroutine body. */
static const char *subroutine_fixture = "[[subroutine]]\n"
                                        "name = \"open_chest\"\n"
                                        "actions = [{ if = [\"flag:test_flag\"], then = [\"destroy\"], else = "
                                        "[\"set_flag:did_not_open\"] }]\n"
                                        "\n";

static void assert_subroutine_structure(const vec_subroutine *subroutines)
{
    TEST_ASSERT_EQUAL_INT(1, subroutines->count);
    const Subroutine *subroutine = &subroutines->data[0];
    TEST_ASSERT_EQUAL_STRING("open_chest", subroutine->name.ptr);

    const ActionTree *tree = &subroutine->action_tree;
    TEST_ASSERT_EQUAL_INT(1, tree->roots.count);

    const ActionNode *if_node = &tree->nodes.data[tree->roots.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_IF_ELSE, if_node->type);
    TEST_ASSERT_EQUAL_INT(1, if_node->conditions.count);
    TEST_ASSERT_EQUAL_INT(COND_FLAG, if_node->conditions.data[0].type);
    TEST_ASSERT_EQUAL_INT(1, if_node->children.count);
    TEST_ASSERT_EQUAL_INT(1, if_node->else_children.count);

    const ActionNode *then_node = &tree->nodes.data[if_node->children.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, then_node->type);

    const ActionNode *else_node = &tree->nodes.data[if_node->else_children.data[0]];
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, else_node->type);
    TEST_ASSERT_EQUAL_STRING("did_not_open", else_node->argument.ptr);
}

void test_toml_emit_subroutines_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator gamedata_alloc = allocator_arena(&arena);
    BlueprintTable blueprints = {0};
    vec_subroutine subroutines = vec_subroutine_new(gamedata_alloc);

    toml_table_t *root = parse_toml(subroutine_fixture);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(subroutines_parse(&test_diag, &gamedata_alloc, &subroutines, root, &arena));
    toml_free(root);

    assert_subroutine_structure(&subroutines);

    char output[4096];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &subroutines, &empty_tileset,
                                     &empty_atlas_regions, nullptr, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[subroutine]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"open_chest\""));

    /* Round-trip: re-parse the emitted TOML and verify the subroutine survived. */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    Allocator gamedata_alloc2 = allocator_arena(&arena2);
    vec_subroutine subroutines2 = vec_subroutine_new(gamedata_alloc2);

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    TEST_ASSERT_TRUE(subroutines_parse(&test_diag, &gamedata_alloc2, &subroutines2, root2, &arena2));
    toml_free(root2);

    assert_subroutine_structure(&subroutines2);

    arena_free(&arena);
    arena_free(&arena2);
}

/* S5.3a/D36: the engine-side tile system. A tileset maps tile ids to a
 * texture name + source rectangle, and a level's ground/overlay layers are
 * row-major flat arrays of those ids. Level size 48x32 -> tiles_wide=3,
 * tiles_high=2 (ceil(48/16), ceil(32/16)), matching the 3x2 row arrays
 * below. */
static const char *tile_fixture = "[[tileset]]\n"
                                  "id = 1\n"
                                  "texture = \"grass.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[tileset]]\n"
                                  "id = 2\n"
                                  "texture = \"grass.png\"\n"
                                  "src = [16, 0, 16, 16]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"tiletest\"\n"
                                  "size = [48, 32]\n"
                                  "tiles_ground = [\n"
                                  "  [1, 1, 1],\n"
                                  "  [2, 2, 2],\n"
                                  "]\n"
                                  "tiles_overlay = [\n"
                                  "  [0, 0, 0],\n"
                                  "  [0, 1, 0],\n"
                                  "]\n";

void test_toml_emit_tiles_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    BlueprintTable blueprints = {0};
    vec_tileset_entry tileset = {0};
    Level level = {0};

    /* Parse original: confirm the fixture itself parses into the expected
     * tileset entries and tile grid before testing emit/round-trip. */
    toml_table_t *root = parse_toml(tile_fixture);
    TEST_ASSERT_NOT_NULL(root);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    TEST_ASSERT_TRUE(tileset_load(&test_diag, &tileset, root, &arena) >= 0);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level, root, "tiletest", &blueprints, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(3, tileset.count); /* placeholder(0) + id 1 + id 2 */
    TEST_ASSERT_EQUAL_STRING("grass.png", tileset.data[1].texture.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, tileset.data[2].src.x);
    TEST_ASSERT_EQUAL_INT(3, level.tiles_wide);
    TEST_ASSERT_EQUAL_INT(2, level.tiles_high);
    TEST_ASSERT_EQUAL_INT(6, level.tiles_ground.count);
    TEST_ASSERT_EQUAL_INT(1, level.tiles_ground.data[level_tile_index(0, 0, level.tiles_wide)]);
    TEST_ASSERT_EQUAL_INT(2, level.tiles_ground.data[level_tile_index(1, 0, level.tiles_wide)]);
    TEST_ASSERT_EQUAL_INT(1, level.tiles_overlay.data[level_tile_index(1, 1, level.tiles_wide)]);

    /* Emit and verify the tileset and both tile layers made it into the TOML text. */
    char output[8192];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines, &tileset,
                                     &empty_atlas_regions, &level, 1);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[tileset]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "id = 1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "id = 2"));
    TEST_ASSERT_NOT_NULL(strstr(output, "tiles_ground = ["));
    TEST_ASSERT_NOT_NULL(strstr(output, "tiles_overlay = ["));

    /* Re-parse the emitted output and confirm everything survives. */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    BlueprintTable blueprints2 = {0};
    vec_tileset_entry tileset2 = {0};
    Level level2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    TEST_ASSERT_TRUE(tileset_load(&test_diag, &tileset2, root2, &arena2) >= 0);
    TEST_ASSERT_TRUE(
        level_load(&test_diag, &level2, root2, "tiletest", &blueprints2, dummy_lookup, nullptr, &test_heap_alloc));
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(tileset.count, tileset2.count);
    TEST_ASSERT_EQUAL_STRING(tileset.data[1].texture.ptr, tileset2.data[1].texture.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, tileset.data[2].src.x, tileset2.data[2].src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, tileset.data[2].src.width, tileset2.data[2].src.width);

    TEST_ASSERT_EQUAL_INT(level.tiles_wide, level2.tiles_wide);
    TEST_ASSERT_EQUAL_INT(level.tiles_high, level2.tiles_high);
    TEST_ASSERT_EQUAL_INT(level.tiles_ground.count, level2.tiles_ground.count);
    for (int index = 0; index < level.tiles_ground.count; index++) {
        TEST_ASSERT_EQUAL_INT(level.tiles_ground.data[index], level2.tiles_ground.data[index]);
    }
    TEST_ASSERT_EQUAL_INT(level.tiles_overlay.count, level2.tiles_overlay.count);
    for (int index = 0; index < level.tiles_overlay.count; index++) {
        TEST_ASSERT_EQUAL_INT(level.tiles_overlay.data[index], level2.tiles_overlay.data[index]);
    }

    test_level_free(&level);
    test_level_free(&level2);
    arena_free(&arena);
    arena_free(&arena2);
}

/* Bounded substring search — used to check whether one blueprint's emitted
 * TOML section (a byte range, not a null-terminated string) contains a
 * given marker. */
static bool block_contains(const char *block, size_t block_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > block_len) {
        return false;
    }
    for (size_t offset = 0; offset <= block_len - needle_len; offset++) {
        if (memcmp(block + offset, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

/* S5.4a/D37: a blueprint may set `sprite = "region_name"` as an alternative
 * to a raw `src`. "player_sprite" has no src/texture of its own -- both are
 * resolved from the "hero" atlas region. "tree_raw" has no sprite attr and
 * must keep behaving exactly like a pre-D37 blueprint (regression guard). */
static const char *atlas_sprite_fixture = "[[atlas.region]]\n"
                                          "name = \"hero\"\n"
                                          "texture = \"hero.png\"\n"
                                          "src = [0, 0, 32, 32]\n"
                                          "\n"
                                          "[[blueprint]]\n"
                                          "name = \"player_sprite\"\n"
                                          "sprite = \"hero\"\n"
                                          "collision_offset = [0, 0]\n"
                                          "collision_size = [16, 16]\n"
                                          "\n"
                                          "[[blueprint]]\n"
                                          "name = \"tree_raw\"\n"
                                          "texture = \"tree.png\"\n"
                                          "src = [0, 0, 64, 80]\n"
                                          "collision_offset = [20, 60]\n"
                                          "collision_size = [24, 16]\n";

void test_toml_emit_atlas_sprite_round_trip(void)
{
    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    vec_atlas_region atlas_regions = {0};
    BlueprintTable blueprints = {0};
    Level empty_level = {0};

    /* Parse original */
    toml_table_t *root = parse_toml(atlas_sprite_fixture);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(atlas_load(&test_diag, &atlas_regions, root, &arena) >= 0);
    blueprints_load(&test_diag, &blueprints, root, &arena);
    Allocator alloc = allocator_arena(&arena);
    blueprint_resolve_sprites(&test_diag, &alloc, &blueprints, &atlas_regions);
    toml_free(root);

    TEST_ASSERT_EQUAL_INT(1, atlas_regions.count);
    TEST_ASSERT_EQUAL_STRING("hero", atlas_regions.data[0].name.ptr);
    TEST_ASSERT_EQUAL_STRING("hero.png", atlas_regions.data[0].texture.ptr);

    /* The sprite reference must resolve to the region's src/texture. */
    const Blueprint *sprite_bp = blueprint_find(&blueprints, "player_sprite");
    TEST_ASSERT_NOT_NULL(sprite_bp);
    TEST_ASSERT_EQUAL_STRING("hero.png", attr_get_string(&sprite_bp->attrs, "texture"));
    Rectangle sprite_src = blueprint_get_source(sprite_bp);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, sprite_src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, sprite_src.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, sprite_src.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, sprite_src.height);

    /* A blueprint with a raw src and no sprite is unaffected. */
    const Blueprint *raw_bp = blueprint_find(&blueprints, "tree_raw");
    TEST_ASSERT_NOT_NULL(raw_bp);
    TEST_ASSERT_NULL(attr_get_string(&raw_bp->attrs, "sprite"));
    TEST_ASSERT_EQUAL_STRING("tree.png", attr_get_string(&raw_bp->attrs, "texture"));

    /* Emit */
    char output[8192];
    int written = toml_emit_gamedata(&test_err, output, (int)sizeof(output), &blueprints, &empty_subroutines,
                                     &empty_tileset, &atlas_regions, &empty_level, 0);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL(strstr(output, "[[atlas.region]]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "name = \"hero\""));
    TEST_ASSERT_NOT_NULL(strstr(output, "sprite = \"hero\""));
    /* tree_raw's raw src/texture must still be emitted verbatim. */
    TEST_ASSERT_NOT_NULL(strstr(output, "src = [0, 0, 64, 80]"));
    TEST_ASSERT_NOT_NULL(strstr(output, "texture = \"tree.png\""));

    /* The sprite-referencing blueprint's own section must NOT re-emit the
     * resolved src/texture as raw values -- only the "sprite" reference. */
    char *player_start = strstr(output, "[[blueprint]]\nname = \"player_sprite\"");
    TEST_ASSERT_NOT_NULL(player_start);
    char *tree_start = strstr(output, "[[blueprint]]\nname = \"tree_raw\"");
    TEST_ASSERT_NOT_NULL(tree_start);
    TEST_ASSERT_TRUE(player_start < tree_start);
    size_t player_block_len = (size_t)(tree_start - player_start);
    TEST_ASSERT_FALSE(block_contains(player_start, player_block_len, "texture = "));
    TEST_ASSERT_FALSE(block_contains(player_start, player_block_len, "src = "));
    TEST_ASSERT_TRUE(block_contains(player_start, player_block_len, "sprite = \"hero\""));

    /* Re-parse the emitted output and confirm the region, the sprite
     * reference, and the raw-src blueprint all survive. */
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena2));
    vec_atlas_region atlas_regions2 = {0};
    BlueprintTable blueprints2 = {0};

    toml_table_t *root2 = parse_toml(output);
    TEST_ASSERT_NOT_NULL(root2);
    TEST_ASSERT_TRUE(atlas_load(&test_diag, &atlas_regions2, root2, &arena2) >= 0);
    blueprints_load(&test_diag, &blueprints2, root2, &arena2);
    Allocator alloc2 = allocator_arena(&arena2);
    blueprint_resolve_sprites(&test_diag, &alloc2, &blueprints2, &atlas_regions2);
    toml_free(root2);

    TEST_ASSERT_EQUAL_INT(1, atlas_regions2.count);
    TEST_ASSERT_EQUAL_STRING("hero", atlas_regions2.data[0].name.ptr);
    TEST_ASSERT_EQUAL_STRING("hero.png", atlas_regions2.data[0].texture.ptr);

    const Blueprint *sprite_bp2 = blueprint_find(&blueprints2, "player_sprite");
    TEST_ASSERT_NOT_NULL(sprite_bp2);
    TEST_ASSERT_EQUAL_STRING("hero", attr_get_string(&sprite_bp2->attrs, "sprite"));
    TEST_ASSERT_EQUAL_STRING("hero.png", attr_get_string(&sprite_bp2->attrs, "texture"));
    Rectangle sprite_src2 = blueprint_get_source(sprite_bp2);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, sprite_src.x, sprite_src2.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, sprite_src.width, sprite_src2.width);

    const Blueprint *raw_bp2 = blueprint_find(&blueprints2, "tree_raw");
    TEST_ASSERT_NOT_NULL(raw_bp2);
    TEST_ASSERT_EQUAL_STRING("tree.png", attr_get_string(&raw_bp2->attrs, "texture"));
    Rectangle raw_src2 = blueprint_get_source(raw_bp2);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 64.0F, raw_src2.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, raw_src2.height);

    arena_free(&arena);
    arena_free(&arena2);
}

/* --- Bindings round-trip ------------------------------------------------- */

#define BINDINGS_BUF_CAP 16384
#define BINDINGS_TMP_PATH "/tmp/sleipner_test_keybindings.toml"

static void free_binding_store(BindingStore *store)
{
    for (int act = 0; act < ACTION_COUNT; act++) {
        for (int alt = 0; alt < store->actions[act].alternatives.count; alt++) {
            vec_atomic_input_free(&store->actions[act].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store->actions[act].alternatives);
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        for (int alt = 0; alt < store->axes[axis].alternatives.count; alt++) {
            vec_atomic_input_free(&store->axes[axis].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store->axes[axis].alternatives);
    }
}

/* Emit `src` to a temp file then load that file back into `dst`. The two
 * stores are bundled into a struct so adjacent-parameter swap can't bite. */
typedef struct {
    BindingStore *src;
    BindingStore *dst;
} StorePair;

static bool emit_then_load(StorePair stores, Allocator alloc)
{
    char buffer[BINDINGS_BUF_CAP];
    int written = toml_emit_bindings(&test_err, buffer, (int)sizeof(buffer), stores.src);
    if (written < 0) {
        return false;
    }
    FILE *file = fopen(BINDINGS_TMP_PATH, "we");
    if (!file) {
        return false;
    }
    bool wrote = fwrite(buffer, 1, (size_t)written, file) == (size_t)written;
    (void)fclose(file);
    if (!wrote) {
        return false;
    }
    input_func_load_defaults(stores.dst, alloc);
    if (!input_func_load_bindings_toml(stores.dst, alloc, &test_err, BINDINGS_TMP_PATH)) {
        return false;
    }
    (void)remove(BINDINGS_TMP_PATH);
    return true;
}

static bool atoms_equal(const AtomicInput *first, const AtomicInput *second)
{
    return first->kind == second->kind && first->int_a == second->int_a && first->int_b == second->int_b;
}

static bool physical_equal(const PhysicalInput *first, const PhysicalInput *second)
{
    if (first->parts.count != second->parts.count) {
        return false;
    }
    for (int index = 0; index < first->parts.count; index++) {
        if (!atoms_equal(&first->parts.data[index], &second->parts.data[index])) {
            return false;
        }
    }
    return true;
}

static bool alternatives_equal(const vec_physical_input *first, const vec_physical_input *second)
{
    if (first->count != second->count) {
        return false;
    }
    for (int index = 0; index < first->count; index++) {
        if (!physical_equal(&first->data[index], &second->data[index])) {
            return false;
        }
    }
    return true;
}

void test_toml_emit_bindings_round_trip_defaults(void)
{
    BindingStore src = {0};
    input_func_load_defaults(&src, test_heap_alloc);

    BindingStore dst = {0};
    TEST_ASSERT_TRUE(emit_then_load((StorePair){.src = &src, .dst = &dst}, test_heap_alloc));

    for (int act = 0; act < ACTION_COUNT; act++) {
        TEST_ASSERT_TRUE(alternatives_equal(&src.actions[act].alternatives, &dst.actions[act].alternatives));
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        TEST_ASSERT_TRUE(alternatives_equal(&src.axes[axis].alternatives, &dst.axes[axis].alternatives));
    }
    free_binding_store(&src);
    free_binding_store(&dst);
}

void test_toml_emit_bindings_round_trip_after_mutation(void)
{
    BindingStore src = {0};
    input_func_load_defaults(&src, test_heap_alloc);

    AtomicInput chord_atoms[3] = {
        {.kind = ATOM_KEY, .int_a = KEY_LEFT_CONTROL, .scale = 1.0F},
        {.kind = ATOM_KEY, .int_a = KEY_LEFT_SHIFT, .scale = 1.0F},
        {.kind = ATOM_KEY, .int_a = KEY_Z, .scale = 1.0F},
    };
    PhysicalInput chord = {0};
    chord.parts.data = chord_atoms;
    chord.parts.count = 3;
    chord.parts.capacity = 3;
    TEST_ASSERT_TRUE(input_func_set_action_alternative(&src, test_heap_alloc, ACTION_INTERACT, 0, &chord));

    BindingStore dst = {0};
    TEST_ASSERT_TRUE(emit_then_load((StorePair){.src = &src, .dst = &dst}, test_heap_alloc));

    const PhysicalInput *out_alt = &dst.actions[ACTION_INTERACT].alternatives.data[0];
    TEST_ASSERT_EQUAL_INT(3, out_alt->parts.count);
    TEST_ASSERT_EQUAL_INT(KEY_LEFT_CONTROL, out_alt->parts.data[0].int_a);
    TEST_ASSERT_EQUAL_INT(KEY_LEFT_SHIFT, out_alt->parts.data[1].int_a);
    TEST_ASSERT_EQUAL_INT(KEY_Z, out_alt->parts.data[2].int_a);

    for (int act = 0; act < ACTION_COUNT; act++) {
        if (act == ACTION_INTERACT) {
            continue;
        }
        TEST_ASSERT_TRUE(alternatives_equal(&src.actions[act].alternatives, &dst.actions[act].alternatives));
    }
    free_binding_store(&src);
    free_binding_store(&dst);
}

void test_toml_load_bindings_missing_file_keeps_defaults(void)
{
    BindingStore store = {0};
    input_func_load_defaults(&store, test_heap_alloc);
    int confirm_count_before = store.actions[ACTION_CONFIRM].alternatives.count;

    TEST_ASSERT_TRUE(
        input_func_load_bindings_toml(&store, test_heap_alloc, &test_err, "/tmp/this_path_should_not_exist_42.toml"));

    TEST_ASSERT_EQUAL_INT(confirm_count_before, store.actions[ACTION_CONFIRM].alternatives.count);
    free_binding_store(&store);
}

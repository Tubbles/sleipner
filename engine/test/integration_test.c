#include "unity.h"
#include "attribute.h"
#include "diag.h"
#include "editor/editor.h"
#include "editor/internal.h"
#include "entity.h"
#include "game.h"
#include "strv.h"
#include "test_input_mock.h"
#include "undo.h"

#include "raylib.h"
#include "toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture_gamedata = "[[blueprint]]\n"
                                      "name = \"player\"\n"
                                      "texture = \"player.png\"\n"
                                      "src = [0, 0, 32, 32]\n"
                                      "collision_offset = [-5, 6]\n"
                                      "collision_size = [10, 10]\n"
                                      "behavior = \"player\"\n"
                                      "speed = 80\n"
                                      "\n"
                                      "[[blueprint]]\n"
                                      "name = \"rock\"\n"
                                      "texture = \"rock.png\"\n"
                                      "src = [0, 0, 16, 16]\n"
                                      "collision_offset = [0, 0]\n"
                                      "collision_size = [16, 16]\n"
                                      "solid = true\n"
                                      "\n"
                                      "[[blueprint]]\n"
                                      "name = \"tall_tree\"\n"
                                      "texture = \"tree.png\"\n"
                                      "src = [0, 0, 32, 48]\n"
                                      "collision_offset = [8, 32]\n"
                                      "collision_size = [16, 12]\n"
                                      "solid = true\n"
                                      "\n"
                                      "[[level]]\n"
                                      "name = \"field\"\n"
                                      "size = [320, 240]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"player\"\n"
                                      "pos = [160, 120]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"rock\"\n"
                                      "pos = [200, 120]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"tall_tree\"\n"
                                      "pos = [50, 50]\n"
                                      "\n"
                                      "[[level]]\n"
                                      "name = \"cave\"\n"
                                      "size = [160, 120]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"player\"\n"
                                      "pos = [80, 60]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"rock\"\n"
                                      "pos = [80, 60]\n";

/* Fixture with enter and on_spawn rules.
 * Player at (100,100), zone at (200,100) with 32x32 collision.
 * Beacon at (50,50) fires set_flag:beacon_spawned on on_spawn. */
static const char *fixture_triggers = "[[blueprint]]\n"
                                      "name = \"player\"\n"
                                      "texture = \"player.png\"\n"
                                      "src = [0, 0, 32, 32]\n"
                                      "collision_offset = [0, 0]\n"
                                      "collision_size = [16, 16]\n"
                                      "behavior = \"player\"\n"
                                      "speed = 80\n"
                                      "\n"
                                      "[[blueprint]]\n"
                                      "name = \"zone\"\n"
                                      "texture = \"rock.png\"\n"
                                      "src = [0, 0, 16, 16]\n"
                                      "collision_offset = [0, 0]\n"
                                      "collision_size = [32, 32]\n"
                                      "solid = false\n"
                                      "\n"
                                      "[[blueprint.rule]]\n"
                                      "trigger = \"enter\"\n"
                                      "actions = [\"set_flag:zone_entered\"]\n"
                                      "\n"
                                      "[[blueprint]]\n"
                                      "name = \"beacon\"\n"
                                      "texture = \"rock.png\"\n"
                                      "src = [0, 0, 16, 16]\n"
                                      "collision_offset = [0, 0]\n"
                                      "collision_size = [16, 16]\n"
                                      "solid = false\n"
                                      "\n"
                                      "[[blueprint.rule]]\n"
                                      "trigger = \"on_spawn\"\n"
                                      "actions = [\"set_flag:beacon_spawned\"]\n"
                                      "\n"
                                      "[[level]]\n"
                                      "name = \"test\"\n"
                                      "size = [320, 240]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"player\"\n"
                                      "pos = [100, 100]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"zone\"\n"
                                      "pos = [200, 100]\n"
                                      "\n"
                                      "[[level.entity]]\n"
                                      "blueprint = \"beacon\"\n"
                                      "pos = [50, 50]\n";

/* Fixture with two levels and a transition trigger.
 * Player at (100,100), door at (200,100) with enter → transition:interior,80,60.
 * Interior level has player at (80,60) and exit_door with enter → transition:field,100,100. */
static const char *fixture_transition = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [0, 0]\n"
                                        "collision_size = [16, 16]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[blueprint]]\n"
                                        "name = \"door\"\n"
                                        "texture = \"rock.png\"\n"
                                        "src = [0, 0, 16, 16]\n"
                                        "collision_offset = [0, 0]\n"
                                        "collision_size = [32, 32]\n"
                                        "solid = false\n"
                                        "\n"
                                        "[[blueprint.rule]]\n"
                                        "trigger = \"enter\"\n"
                                        "actions = [\"transition:interior,80,60\"]\n"
                                        "\n"
                                        "[[blueprint]]\n"
                                        "name = \"exit_door\"\n"
                                        "texture = \"rock.png\"\n"
                                        "src = [0, 0, 16, 16]\n"
                                        "collision_offset = [0, 0]\n"
                                        "collision_size = [32, 32]\n"
                                        "solid = false\n"
                                        "\n"
                                        "[[blueprint.rule]]\n"
                                        "trigger = \"enter\"\n"
                                        "actions = [\"transition:field,100,100\"]\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"field\"\n"
                                        "size = [320, 240]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [100, 100]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"door\"\n"
                                        "pos = [200, 100]\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"interior\"\n"
                                        "size = [160, 120]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [80, 60]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"exit_door\"\n"
                                        "pos = [80, 110]\n";

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

void test_integration_load_gamedata(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    bool loaded = game_load_gamedata(&diag, &state,
                                     (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(state.gamedata_loaded);
    TEST_ASSERT_EQUAL_STRING("field", state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(3, state.gamedata.current_level.entities.count);
    TEST_ASSERT_EQUAL_INT(3, state.gamedata.blueprints.entries.count);
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

    game_free(&diag, &state);
}

void test_integration_load_specific_level(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){160, 120}));

    bool loaded = game_load_gamedata(
        &diag, &state,
        (GamedataParams){.toml_string = fixture_gamedata, .level_name = "cave", .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("cave", state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(2, state.gamedata.current_level.entities.count);
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

    game_free(&diag, &state);
}

void test_integration_walk_and_collide(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Player starts at (160, 120), rock at (200, 120) with 16x16 collision.
     * Walk right into the rock. */
    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 300; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* Player collision must not overlap the rock */
    const Entity *player = game_get_player_const(&state);
    const AttrSet *player_defaults = entity_resolve_defaults(&state, player->id);
    Rectangle player_col = entity_collision_rect(player, player_defaults);
    /* Rock is entity index 1 (player is 0) */
    const Entity *rock_entity = &state.gamedata.current_level.entities.data[1];
    const AttrSet *rock_defaults = entity_resolve_defaults(&state, rock_entity->id);
    Rectangle rock = entity_collision_rect(rock_entity, rock_defaults);
    TEST_ASSERT_TRUE(player_col.x + player_col.width <= rock.x + 0.1F);

    game_free(&diag, &state);
}

void test_integration_walk_freely(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    const Entity *player = game_get_player_const(&state);
    float start_x = player->position.x;
    float start_y = player->position.y;

    /* Walk down-left for 30 frames (away from obstacles) */
    InputState input = {0};
    input.left_stick.x = -0.5F;
    input.left_stick.y = 0.5F;

    for (int iteration = 0; iteration < 30; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    player = game_get_player_const(&state);
    TEST_ASSERT_TRUE(player->position.x < start_x);
    TEST_ASSERT_TRUE(player->position.y > start_y);
    TEST_ASSERT_EQUAL_INT(30, state.frame);

    game_free(&diag, &state);
}

void test_integration_boundary_all_directions(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    float half = FRAME_SIZE / 2.0F;
    InputState input = {0};

    /* Push against each wall */
    input.left_stick.x = -1.0F;
    input.left_stick.y = 0.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&state)->position.x);

    input.left_stick.x = 0.0F;
    input.left_stick.y = -1.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&state)->position.y);

    input.left_stick.x = 1.0F;
    input.left_stick.y = 0.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 320.0F - half, game_get_player_const(&state)->position.x);

    input.left_stick.x = 0.0F;
    input.left_stick.y = 1.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 240.0F - half, game_get_player_const(&state)->position.y);

    game_free(&diag, &state);
}

void test_integration_player_entity_spawns(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){640, 360}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Player entity must exist */
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);

    /* Player must have behavior="player" attribute (via blueprint) */
    const AttrSet *player_defaults = entity_resolve_defaults(&state, player->id);
    const char *behavior = attr_get_scoped_string(&player->attrs, player_defaults, "behavior");
    TEST_ASSERT_NOT_NULL(behavior);
    TEST_ASSERT_EQUAL_STRING("player", behavior);

    /* Player must have valid blueprint and texture */
    TEST_ASSERT_NOT_NULL(player_defaults);
    TEST_ASSERT_NOT_NULL(player->texture);
    TEST_ASSERT_EQUAL_STRING("player", player->blueprint_name.ptr);

    /* Player must be at the spawned position */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Player must be controllable — move right for a few frames */
    float start_x = player->position.x;
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 10; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_TRUE(game_get_player_const(&state)->position.x > start_x);

    game_free(&diag, &state);
}

void test_integration_on_spawn_trigger_fires_on_load(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_triggers, .texture_lookup = dummy_lookup}));

    /* beacon blueprint has on_spawn → set_flag:beacon_spawned.
     * No game_update needed — the flag must be set by game_load_gamedata. */
    TEST_ASSERT_TRUE(flag_get(&state.gamedata.flags, "beacon_spawned"));

    game_free(&diag, &state);
}

void test_integration_enter_trigger_fires_on_overlap(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_triggers, .texture_lookup = dummy_lookup}));

    /* zone_entered must not be set before the player reaches the zone */
    TEST_ASSERT_FALSE(flag_get(&state.gamedata.flags, "zone_entered"));

    /* Player at (100,100), collision [100,100,16,16]. Zone at (200,100), collision [200,100,32,32].
     * Player right edge starts at 116, zone left edge at 200. Gap = 84px.
     * Speed = 80 px/s → need ~63 frames at 1/60s. Run 80 to be safe. */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 80; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    TEST_ASSERT_TRUE(flag_get(&state.gamedata.flags, "zone_entered"));

    game_free(&diag, &state);
}

void test_integration_enter_trigger_fires_only_once(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    /* Zone blueprint uses add_attr:self.enter_count,1 to count firings */
    static const char *fixture_enter_count = "[[blueprint]]\n"
                                             "name = \"player\"\n"
                                             "texture = \"player.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "behavior = \"player\"\n"
                                             "speed = 80\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"zone\"\n"
                                             "texture = \"rock.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [32, 32]\n"
                                             "solid = false\n"
                                             "enter_count = 0\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"enter\"\n"
                                             "actions = [\"add_attr:self.enter_count,1\"]\n"
                                             "\n"
                                             "[[level]]\n"
                                             "name = \"test\"\n"
                                             "size = [320, 240]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"player\"\n"
                                             "pos = [100, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"zone\"\n"
                                             "pos = [200, 100]\n";

    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_enter_count, .texture_lookup = dummy_lookup}));

    /* Walk into zone and keep walking through it for 200 frames total */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 200; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* enter_count must be exactly 1 — edge-triggered, not level-triggered */
    const Entity *zone = &state.gamedata.current_level.entities.data[1];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&zone->attrs, nullptr, "enter_count", 0.0F));

    game_free(&diag, &state);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "re");
    if (!file) {
        return nullptr;
    }
    (void)fseek(file, 0, SEEK_END);
    long length = ftell(file);
    (void)fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        (void)fclose(file);
        return nullptr;
    }
    size_t size = (size_t)length;
    char *buffer = calloc(size + 1, 1);
    if (!buffer) {
        (void)fclose(file);
        return nullptr;
    }
    (void)fread(buffer, 1, size, file);
    (void)fclose(file);
    return buffer;
}

void test_integration_real_gamedata_loads(void)
{
    char *content = read_file(GAMEDATA_PATH);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "could not read " GAMEDATA_PATH);

    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){480, 270}));

    bool loaded =
        game_load_gamedata(&diag, &state, (GamedataParams){.toml_string = content, .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE_MESSAGE(loaded, error_get(&state.error));
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

    /* Run a few frames to exercise update logic (timers, overlap tracking, etc.) */
    InputState input = {0};
    for (int iteration = 0; iteration < 10; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    game_free(&diag, &state);
    free(content);
}

void test_integration_real_gamedata_all_levels_load(void)
{
    char *content = read_file(GAMEDATA_PATH);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "could not read " GAMEDATA_PATH);

    /* Parse once to discover level names */
    char errbuf[200];
    char *parse_buf = strdup(content);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL(root);

    toml_array_t *levels = toml_array_in(root, "level");
    TEST_ASSERT_NOT_NULL(levels);
    int level_count = toml_array_nelem(levels);
    TEST_ASSERT_TRUE(level_count > 0);

    /* Collect level names (freed after the loop) */
    char *level_names[32] = {0};
    TEST_ASSERT_TRUE(level_count <= 32);
    for (int index = 0; index < level_count; index++) {
        toml_table_t *level_table = toml_table_at(levels, index);
        toml_datum_t name = toml_string_in(level_table, "name");
        TEST_ASSERT_TRUE(name.ok);
        level_names[index] = name.u.s;
    }
    toml_free(root);

    /* Load each level through the full engine path */
    for (int index = 0; index < level_count; index++) {
        GameState state = {0};
        Diag diag = {&state.error, &state.debug};
        TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){480, 270}));

        bool loaded = game_load_gamedata(
            &diag, &state,
            (GamedataParams){.toml_string = content, .level_name = level_names[index], .texture_lookup = dummy_lookup});
        TEST_ASSERT_TRUE_MESSAGE(loaded, error_get(&state.error));
        TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

        game_free(&diag, &state);
    }

    for (int index = 0; index < level_count; index++) {
        free(level_names[index]);
    }
    free(content);
}

void test_integration_transition_changes_level(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_transition, .texture_lookup = dummy_lookup}));

    TEST_ASSERT_EQUAL_STRING("field", state.gamedata.current_level.name.ptr);
    TEST_ASSERT_FALSE(state.transition.pending);

    /* Walk right into the door trigger: player at (100,100), door at (200,100).
     * Speed = 80 px/s, gap ~84px → ~63 frames. Run 80 to be safe. */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 80; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* The enter trigger should have set transition.pending */
    TEST_ASSERT_TRUE_MESSAGE(state.transition.pending, error_get(&state.error));
    TEST_ASSERT_EQUAL_STRING("interior", state.transition.level.ptr);

    /* Simulate what handle_transition does: reload with the target level name.
     * This is the exact path that broke — the level name was in gamedata_arena
     * and got wiped by arena_restore before level_load could use it. */
    state.transition.pending = false;
    bool reloaded = game_load_gamedata(&diag, &state,
                                       (GamedataParams){.toml_string = fixture_transition,
                                                        .level_name = state.transition.level.ptr,
                                                        .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE_MESSAGE(reloaded, error_get(&state.error));
    TEST_ASSERT_EQUAL_STRING("interior", state.gamedata.current_level.name.ptr);
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);

    game_free(&diag, &state);
}

/* Bug: "after panning around in editor for a little while, the player's
 * position gets reset". This is the initial failing-test repro — it
 * describes the user's reported scenario at the coarsest integration
 * level. If this passes, the repro is not yet correct and we need more
 * specifics from the user about the exact sequence that triggers the
 * reset. If it fails, we have a stable pin for root-cause work.
 *
 * Scenario (per the user's report):
 *   1. Load gamedata, player at TOML start position (160, 120).
 *   2. Play mode: walk the player visibly away from the start.
 *   3. Toggle editor mode.
 *   4. Pan the editor camera with the left stick for a while.
 *   5. Assert the player position has NOT been reset.
 */
void test_integration_editor_pan_does_not_reset_player_position(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Player starts at TOML position (160, 120). */
    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Play mode: walk the player down-left for 60 frames so the position
     * visibly diverges from the TOML start. (Away from the rock at 200,120
     * and the tree at 50,50 — take a path clear of both.) */
    state.editor_mode = false;
    InputState walk_input = {0};
    walk_input.left_stick.x = 0.0F;
    walk_input.left_stick.y = 1.0F;
    for (int iteration = 0; iteration < 60; iteration++) {
        game_update(&diag, &state, walk_input, 1.0F / 60.0F);
    }

    player = game_get_player_const(&state);
    float walked_x = player->position.x;
    float walked_y = player->position.y;
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, walked_x); /* unchanged in X */
    TEST_ASSERT_TRUE(walked_y > 120.5F);              /* moved down */

    /* Toggle to editor mode. Player position must be preserved across
     * the mode toggle. */
    state.editor_mode = true;
    player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    /* Editor pan: drive the left stick for 600 frames (10 simulated
     * seconds). game_update still runs every frame in editor mode; the
     * bug report says "after panning around for a little while the
     * player's position gets reset". This is the window in which the
     * reset is alleged to happen. */
    InputState pan_input = {0};
    pan_input.left_stick.x = 1.0F;
    pan_input.left_stick.y = 0.0F;
    for (int iteration = 0; iteration < 600; iteration++) {
        game_update(&diag, &state, pan_input, 1.0F / 60.0F);
    }

    /* The player must still be exactly where we left them. */
    player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    game_free(&diag, &state);
}

/* --- Bug regression: editor undo-at-left-edge re-applies "Initial" snapshot
 *
 * User report: in a fresh Linux session, start → walk for a second → enter
 * editor → pan for a couple seconds → player snaps back to TOML start.
 *
 * Mechanism:
 *   1. main.c pushes an "Initial" undo entry at startup, capturing the
 *      freshly loaded level (player at TOML start).
 *   2. The user enters play mode and walks the player.
 *   3. The user toggles to editor mode — no new undo entry is pushed by
 *      play-mode movement (play movement is not an editor edit).
 *   4. In editor BROWSE, KEY_LEFT is bound to undo_history_step_back
 *      (editor/core.c:596). On Linux the user pans with the arrow keys,
 *      so the LEFT arrow fires at the left edge of the pan.
 *   5. undo_history_step_back at the left edge (no prev entry) used to
 *      call restore_entry on the current node unconditionally, memcpying
 *      the "Initial" arena bytes back over live state and resetting the
 *      player to the TOML start.
 *
 * Black-box shape: this test drives the exact observable path — it mocks
 * raylib's IsKeyPressed(KEY_LEFT) via the --wrap shim in test_input_mock.c
 * so handle_browse_input's real toggle_pressed call fires, which then
 * invokes undo_history_step_back via the real editor binding. The only
 * layer skipped versus the real frame loop is main.c's sub_mode dispatch
 * in handle_editor_input (which is static and not reachable from the
 * engine library), but because the editor enters BROWSE by default the
 * dispatch is a straight forward to handle_browse_input anyway.
 *
 * If you need to prove the wrap actually hooks the test, temporarily revert
 * the left-edge early-return in undo.c:undo_history_step_back — this test
 * must go red. */
void test_integration_editor_undo_at_left_edge_preserves_play_state(void)
{
    test_input_reset();

    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Replicate main.c startup: push a baseline "Initial" snapshot
     * immediately after loading gamedata. */
    UndoHistory undo_history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &undo_history));
    undo_history_new_entry(&undo_history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    /* Player starts at TOML position (160, 120). */
    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Play mode: walk the player down for 60 frames so the position
     * visibly diverges from the TOML start. */
    state.editor_mode = false;
    InputState walk_input = {0};
    walk_input.left_stick.y = 1.0F;
    for (int iteration = 0; iteration < 60; iteration++) {
        game_update(&diag, &state, walk_input, 1.0F / 60.0F);
    }

    player = game_get_player_const(&state);
    float walked_x = player->position.x;
    float walked_y = player->position.y;
    TEST_ASSERT_TRUE(walked_y > 120.5F); /* actually moved */

    /* User toggles to editor mode. No editor edits happen here — the
     * only undo entry in history is still the "Initial" one. */
    state.editor_mode = true;

    /* Now simulate the actual user action that triggered the bug: the
     * LEFT arrow keypress while in editor browse. The --wrap shim on
     * IsKeyPressed makes toggle_pressed({KEY_LEFT, ...}) return true,
     * exactly as if the user had pressed the key on a real keyboard.
     *
     * This is the whole point of the wrap infrastructure: the test no
     * longer reaches past handle_browse_input to call
     * undo_history_step_back directly — it goes through the real
     * edge-triggered input path. */
    /* Match main.c's editor initialisation: sentinel -1s for index fields
     * and radial confirmation, so handle_browse_input doesn't early-return
     * into the radial dispatch path (radial_confirmed == 0 is a valid
     * confirmed sector, sentinel is -1). */
    EditorState editor_state = {.top_mode = EDITOR_TOP_SCENE,
                                .selected_entity_index = -1,
                                .sub_mode = EDITOR_SUB_BROWSE,
                                .selected_attr_index = -1,
                                .radial_confirmed = -1,
                                .radial_selected = -1,
                                .selected_blueprint_index = -1,
                                .blueprint_attr_index = -1,
                                .blueprint_tree_index = -1};
    WatchList watches = {0};
    Camera2D editor_camera = {0};
    InputState editor_input = {0};

    test_input_press_key(KEY_LEFT);
    handle_browse_input(&state, &editor_camera, &editor_state, &watches, &undo_history, editor_input, 1.0F / 60.0F);
    test_input_frame_advance();

    /* The user did not perform any editor edits since entering editor
     * mode, so undo-at-the-left-edge must be a no-op with respect to
     * live state. The player must still be at the walked position. */
    player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    undo_history_free(&undo_history);
    game_free(&diag, &state);
}

/* Helper: locate the display index of a named INT attribute on the player
 * entity, mirroring the path the editor walks when the user navigates the
 * attribute panel. Returns -1 if not found. */
static int find_int_attr_display_index(GameState *state, Entity *entity, const char *name)
{
    int probe_limit = 64;
    size_t name_len = strlen(name);
    for (int idx = 0; idx < probe_limit; idx++) {
        Attribute *attr = attr_at_display_index(state, entity, idx);
        if (!attr) {
            continue;
        }
        if (attr->type == ATTR_INT && attr->name.len == name_len && strncmp(attr->name.ptr, name, name_len) == 0) {
            return idx;
        }
    }
    return -1;
}

/* Bug repro: in editor ATTR_EDIT, a single tap of LEFT/RIGHT on either
 * keyboard or D-pad must change a numeric attribute by ±1 immediately.
 *
 * The original implementation routed ±1 through binding_held + a 0.4s
 * ATTR_REPEAT_DELAY wait before the first fire, so a single-frame tap
 * produced no observable change — the user reported "left and right does
 * not work". The fix is to fire the delta on the press edge (the frame
 * the held direction transitions from 0 to ±1) and let the existing
 * timer drive the held repeat after the delay.
 *
 * The test taps each binding for one frame via the input mock — the
 * --wrap shim makes IsKeyDown / IsGamepadButtonDown return true exactly
 * for that frame — and asserts on the entity-side attribute value, the
 * same observable the user is acting on. A regression that no-ops
 * binding_held without changing observable behaviour will still fail
 * this test. */
void test_integration_editor_attr_edit_tap_decrements_by_one(void)
{
    test_input_reset();

    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    UndoHistory undo_history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &undo_history));
    undo_history_new_entry(&undo_history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    Entity *player_entity = &state.gamedata.current_level.entities.data[state.gamedata.player_index];
    int speed_attr_index = find_int_attr_display_index(&state, player_entity, "speed");
    TEST_ASSERT_TRUE_MESSAGE(speed_attr_index >= 0, "could not locate INT attr 'speed' on player");

    Attribute *speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    TEST_ASSERT_NOT_NULL(speed_attr);
    int starting_speed = speed_attr->value.i;
    TEST_ASSERT_EQUAL_INT(80, starting_speed);

    /* Drop directly into ATTR_EDIT on speed, mirroring what
     * handle_browse_select does when the user confirms on an INT row
     * (engine/src/editor/core.c:377-379). The black-box portion is the
     * input simulation and dispatch below, not this setup. */
    EditorState editor_state = {.top_mode = EDITOR_TOP_SCENE,
                                .selected_entity_index = state.gamedata.player_index,
                                .selected_attr_index = speed_attr_index,
                                .sub_mode = EDITOR_SUB_ATTR_EDIT,
                                .saved_attr_int = starting_speed,
                                .radial_confirmed = -1,
                                .radial_selected = -1,
                                .selected_blueprint_index = -1,
                                .blueprint_attr_index = -1,
                                .blueprint_tree_index = -1,
                                .selected_tree_index = -1};

    test_input_tap_key(KEY_LEFT);
    handle_attr_edit_input(&state, &editor_state, &undo_history, 1.0F / 60.0F);
    test_input_frame_advance();
    speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed - 1, speed_attr->value.i,
                                  "tap KEY_LEFT should decrement speed by 1 in ATTR_EDIT");

    test_input_tap_key(KEY_RIGHT);
    handle_attr_edit_input(&state, &editor_state, &undo_history, 1.0F / 60.0F);
    test_input_frame_advance();
    speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed, speed_attr->value.i,
                                  "tap KEY_RIGHT should restore speed to starting value");

    test_input_tap_gamepad_button(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    handle_attr_edit_input(&state, &editor_state, &undo_history, 1.0F / 60.0F);
    test_input_frame_advance();
    speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed - 1, speed_attr->value.i,
                                  "tap D-pad LEFT should decrement speed by 1 in ATTR_EDIT");

    test_input_tap_gamepad_button(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    handle_attr_edit_input(&state, &editor_state, &undo_history, 1.0F / 60.0F);
    test_input_frame_advance();
    speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed, speed_attr->value.i,
                                  "tap D-pad RIGHT should restore speed to starting value");

    undo_history_free(&undo_history);
    game_free(&diag, &state);
}

/* Regression guard: the existing hold-then-repeat behaviour must survive
 * the immediate-fire fix. Hold KEY_LEFT for one simulated second and
 * confirm the attribute drops by enough to cover the initial fire plus
 * several timer-driven repeats after ATTR_REPEAT_DELAY (0.4s). */
void test_integration_editor_attr_edit_hold_repeats_after_delay(void)
{
    test_input_reset();

    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    UndoHistory undo_history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &undo_history));
    undo_history_new_entry(&undo_history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    Entity *player_entity = &state.gamedata.current_level.entities.data[state.gamedata.player_index];
    int speed_attr_index = find_int_attr_display_index(&state, player_entity, "speed");
    TEST_ASSERT_TRUE(speed_attr_index >= 0);

    Attribute *speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    int starting_speed = speed_attr->value.i;

    EditorState editor_state = {.top_mode = EDITOR_TOP_SCENE,
                                .selected_entity_index = state.gamedata.player_index,
                                .selected_attr_index = speed_attr_index,
                                .sub_mode = EDITOR_SUB_ATTR_EDIT,
                                .saved_attr_int = starting_speed,
                                .radial_confirmed = -1,
                                .radial_selected = -1,
                                .selected_blueprint_index = -1,
                                .blueprint_attr_index = -1,
                                .blueprint_tree_index = -1,
                                .selected_tree_index = -1};

    test_input_hold_key(KEY_LEFT);
    for (int iter = 0; iter < 60; iter++) {
        handle_attr_edit_input(&state, &editor_state, &undo_history, 1.0F / 60.0F);
        test_input_frame_advance();
    }
    test_input_release_key(KEY_LEFT);
    speed_attr = attr_at_display_index(&state, player_entity, speed_attr_index);
    int total_drop = starting_speed - speed_attr->value.i;
    TEST_ASSERT_TRUE_MESSAGE(total_drop >= 3, "holding KEY_LEFT for 1s should fire several decrements");

    undo_history_free(&undo_history);
    game_free(&diag, &state);
}

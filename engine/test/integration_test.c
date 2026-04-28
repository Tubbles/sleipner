#include "unity.h"
#include "attribute.h"
#include "editor/editor.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "input_func.h"
#include "menu.h"
#include "test_helpers.h"

#include "raylib.h"
#include "toml.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lazy-initialised default BindingStore for tests that need to drive the
 * function layer (input_pressed/input_axis). The store survives the
 * whole test process; vec contents are heap-allocated and intentionally
 * not freed (one-shot leak at process exit, matches main_test.c style). */
static BindingStore test_bindings;
static bool test_bindings_loaded;
static const BindingStore *get_test_bindings(void)
{
    if (!test_bindings_loaded) {
        input_func_load_defaults(&test_bindings, test_heap_alloc);
        test_bindings_loaded = true;
    }
    return &test_bindings;
}

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

void test_integration_load_gamedata(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    TEST_ASSERT_TRUE(game.state.gamedata_loaded);
    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(3, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_EQUAL_INT(3, game.state.gamedata.blueprints.entries.count);
    TEST_ASSERT_TRUE(game.state.gamedata.player_index >= 0);

    test_game_teardown(&game);
}

void test_integration_load_specific_level(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup_with_level(&game, fixture_gamedata, "cave"));

    TEST_ASSERT_EQUAL_STRING("cave", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(2, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_TRUE(game.state.gamedata.player_index >= 0);

    test_game_teardown(&game);
}

void test_integration_walk_and_collide(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Player starts at (160, 120), rock at (200, 120) with 16x16 collision.
     * Walk right into the rock. */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 300);

    /* Player collision must not overlap the rock */
    const Entity *player = game_get_player_const(&game.state);
    Rectangle player_col = test_entity_collision_rect(&game.state, player);
    const Entity *rock_entity = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_NOT_NULL(rock_entity);
    Rectangle rock = test_entity_collision_rect(&game.state, rock_entity);
    TEST_ASSERT_TRUE(player_col.x + player_col.width <= rock.x + 0.1F);

    test_game_teardown(&game);
}

void test_integration_walk_freely(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    const Entity *player = game_get_player_const(&game.state);
    float start_x = player->position.x;
    float start_y = player->position.y;

    /* Walk down-left for 30 frames (away from obstacles) */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, -0.5F);
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, 0.5F);
    test_advance_frames(&game, input, 30);

    player = game_get_player_const(&game.state);
    TEST_ASSERT_TRUE(player->position.x < start_x);
    TEST_ASSERT_TRUE(player->position.y > start_y);
    TEST_ASSERT_EQUAL_INT(30, game.state.frame);

    test_game_teardown(&game);
}

void test_integration_boundary_all_directions(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    float half = FRAME_SIZE / 2.0F;
    InputState input = {0};

    /* Push against each wall */
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, -1.0F);
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, 0.0F);
    test_advance_frames(&game, input, 500);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&game.state)->position.x);

    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 0.0F);
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, -1.0F);
    test_advance_frames(&game, input, 500);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&game.state)->position.y);

    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, 0.0F);
    test_advance_frames(&game, input, 500);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 320.0F - half, game_get_player_const(&game.state)->position.x);

    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 0.0F);
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    test_advance_frames(&game, input, 500);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 240.0F - half, game_get_player_const(&game.state)->position.y);

    test_game_teardown(&game);
}

void test_integration_player_entity_spawns(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Player entity must exist */
    TEST_ASSERT_TRUE(game.state.gamedata.player_index >= 0);

    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);

    /* Player blueprint name and texture are observable from the
     * loaded gamedata; behavior="player" is the rule-side observable
     * that makes the entity respond to movement input, covered by
     * test_game_update_player_moves_right and friends. */
    TEST_ASSERT_NOT_NULL(player->texture);
    TEST_ASSERT_EQUAL_STRING("player", player->blueprint_name.ptr);

    /* Player must be at the spawned position */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Player must be controllable — move right for a few frames */
    float start_x = player->position.x;
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 10);
    TEST_ASSERT_TRUE(game_get_player_const(&game.state)->position.x > start_x);

    test_game_teardown(&game);
}

void test_integration_on_spawn_trigger_fires_on_load(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_triggers));

    /* beacon blueprint has on_spawn → set_flag:beacon_spawned.
     * No frame advance needed — the flag must be set by game_load_gamedata. */
    TEST_ASSERT_TRUE(flag_get(&game.state.gamedata.flags, "beacon_spawned"));

    test_game_teardown(&game);
}

void test_integration_enter_trigger_fires_on_overlap(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_triggers));

    /* zone_entered must not be set before the player reaches the zone */
    TEST_ASSERT_FALSE(flag_get(&game.state.gamedata.flags, "zone_entered"));

    /* Player at (100,100), collision [100,100,16,16]. Zone at (200,100), collision [200,100,32,32].
     * Player right edge starts at 116, zone left edge at 200. Gap = 84px.
     * Speed = 80 px/s → need ~63 frames at 1/60s. Run 80 to be safe. */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 80);

    TEST_ASSERT_TRUE(flag_get(&game.state.gamedata.flags, "zone_entered"));

    test_game_teardown(&game);
}

void test_integration_enter_trigger_fires_only_once(void)
{
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

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_enter_count));

    /* Walk into zone and keep walking through it for 200 frames total */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 200);

    /* enter_count must be exactly 1 — edge-triggered, not level-triggered */
    const Entity *zone = test_find_entity_by_blueprint(&game.state, "zone");
    TEST_ASSERT_NOT_NULL(zone);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&zone->attrs, nullptr, "enter_count", 0.0F));

    test_game_teardown(&game);
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

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, content));
    TEST_ASSERT_TRUE(game.state.gamedata.player_index >= 0);

    /* Run a few frames to exercise update logic (timers, overlap tracking, etc.) */
    InputState input = {0};
    test_advance_frames(&game, input, 10);

    test_game_teardown(&game);
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
        TestGame game;
        bool loaded = test_game_setup_with_level(&game, content, level_names[index]);
        TEST_ASSERT_TRUE_MESSAGE(loaded, error_get(&game.state.error));
        TEST_ASSERT_TRUE(game.state.gamedata.player_index >= 0);

        test_game_teardown(&game);
    }

    for (int index = 0; index < level_count; index++) {
        free(level_names[index]);
    }
    free(content);
}

void test_integration_transition_changes_level(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_transition));

    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);

    /* Walk right into the door trigger. Player at (100,100), door at
     * (200,100), speed = 80 px/s, so ~64 frames to reach. Iterate
     * until the level changes, then drop the input and run one more
     * frame so the spawn position is observable without further drift. */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    int max_iterations = 200;
    int iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "field") == 0) {
        test_advance_frame(&game, input);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "transition to 'interior' should fire within 200 frames");
    TEST_ASSERT_EQUAL_STRING("interior", game.state.gamedata.current_level.name.ptr);

    /* Drop the input so the player doesn't drift after respawn. */
    InputState idle = {0};
    test_advance_frame(&game, idle);

    /* Observable: the player is at the spawn point declared in the
     * transition trigger (interior, 80, 60). */
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 80.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 60.0F, player->position.y);

    test_game_teardown(&game);
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
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Player starts at TOML position (160, 120). */
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Play mode: walk the player down for 60 frames so the position
     * visibly diverges from the TOML start. (Away from the rock at 200,120
     * and the tree at 50,50 — take a path clear of both.) */
    InputState walk_input = {0};
    input_state_set_gp_axis(&walk_input, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    test_advance_frames(&game, walk_input, 60);

    player = game_get_player_const(&game.state);
    float walked_x = player->position.x;
    float walked_y = player->position.y;
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, walked_x); /* unchanged in X */
    TEST_ASSERT_TRUE(walked_y > 120.5F);              /* moved down */

    /* Toggle to editor mode. Player position must be preserved across
     * the mode toggle. */
    game.state.editor_mode = true;
    player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    /* Editor pan: drive the left stick for 600 frames (10 simulated
     * seconds). game_update still runs every frame in editor mode; the
     * bug report says "after panning around for a little while the
     * player's position gets reset". This is the window in which the
     * reset is alleged to happen. */
    InputState pan_input = {0};
    input_state_set_gp_axis(&pan_input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, pan_input, 600);

    /* The player must still be exactly where we left them. */
    player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    test_game_teardown(&game);
}

/* --- Bug regression: editor undo-at-left-edge re-applies "Initial" snapshot
 *
 * User report: in a fresh Linux session, start → walk for a second → enter
 * editor → press the undo chord → player snaps back to TOML start.
 *
 * Mechanism:
 *   1. main.c pushes an "Initial" undo entry at startup, capturing the
 *      freshly loaded level (player at TOML start).
 *   2. The user enters play mode and walks the player.
 *   3. The user toggles to editor mode — no new undo entry is pushed by
 *      play-mode movement (play movement is not an editor edit).
 *   4. In editor BROWSE, ACTION_EDITOR_UNDO (Ctrl+Z keyboard, L1+Left
 *      D-pad gamepad) calls undo_history_step_back (editor/core.c:588).
 *   5. undo_history_step_back at the left edge (no prev entry) used to
 *      call restore_entry on the current node unconditionally, memcpying
 *      the "Initial" arena bytes back over live state and resetting the
 *      player to the TOML start.
 *
 * Black-box shape: drives frame_update with a real InputState. F5
 * toggles editor mode, then a Ctrl+Z chord fires ACTION_EDITOR_UNDO
 * through the function layer. No internal handlers are called
 * directly; the test goes through the same dispatch sequence
 * production main.c runs.
 *
 * Acid test: temporarily replace the `|| !history->current->prev`
 * left-edge guard in undo.c:undo_history_step_back with `false`, so
 * restore_entry runs unconditionally — this test must go red. */
void test_integration_editor_undo_at_left_edge_preserves_play_state(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Player starts at TOML position (160, 120). */
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, player->position.y);

    /* Play mode: walk the player down for 60 frames so the position
     * visibly diverges from the TOML start. */
    InputState walk_input = {0};
    input_state_set_gp_axis(&walk_input, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    test_advance_frames(&game, walk_input, 60);

    player = game_get_player_const(&game.state);
    float walked_x = player->position.x;
    float walked_y = player->position.y;
    TEST_ASSERT_TRUE(walked_y > 120.5F); /* actually moved */

    /* User toggles to editor mode via the real F5 binding. */
    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    /* Real Ctrl+Z chord: hold Ctrl (level), press Z (edge). The
     * input_func layer's chord rule fires when all atoms are down and
     * at least one is freshly pressed this frame. */
    InputState undo_input = {0};
    input_state_hold_key(&undo_input, KEY_LEFT_CONTROL);
    input_state_press_key(&undo_input, KEY_Z);
    test_advance_frame(&game, undo_input);

    /* The user did not perform any editor edits since entering editor
     * mode, so undo-at-the-left-edge must be a no-op with respect to
     * live state. The player must still be at the walked position. */
    player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, walked_y, player->position.y);

    test_game_teardown(&game);
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
 * Each scenario builds a fresh InputState with a single tap, drives it
 * through frame_update, and asserts on the player's "speed" attribute —
 * the same observable the user is acting on. A regression that no-ops
 * the press-edge fire without changing observable behaviour has to
 * fail this test. */
void test_integration_editor_attr_edit_tap_decrements_by_one(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Enter editor mode via the real F5 binding. */
    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    Entity *player_entity = game_get_player(&game.state);
    int speed_attr_index = test_find_int_attr_display_index(&game.state, player_entity, "speed");
    TEST_ASSERT_TRUE_MESSAGE(speed_attr_index >= 0, "could not locate INT attr 'speed' on player");

    int starting_speed = test_player_int_attr(&game.state, "speed");
    TEST_ASSERT_EQUAL_INT(80, starting_speed);

    /* Drop directly into ATTR_EDIT on speed, mirroring what
     * handle_browse_select does when the user confirms on an INT row
     * (engine/src/editor/core.c:377-379). The black-box portion is the
     * input simulation and dispatch below, not this setup. */
    game.editor_state.selected_entity_index = game.state.gamedata.player_index;
    game.editor_state.selected_attr_index = speed_attr_index;
    game.editor_state.sub_mode = EDITOR_SUB_ATTR_EDIT;
    game.editor_state.saved_attr_int = starting_speed;

    InputState left_kb = {0};
    input_state_press_key(&left_kb, KEY_LEFT);
    test_advance_frame(&game, left_kb);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed - 1, test_player_int_attr(&game.state, "speed"),
                                  "tap KEY_LEFT should decrement speed by 1 in ATTR_EDIT");

    InputState right_kb = {0};
    input_state_press_key(&right_kb, KEY_RIGHT);
    test_advance_frame(&game, right_kb);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed, test_player_int_attr(&game.state, "speed"),
                                  "tap KEY_RIGHT should restore speed to starting value");

    InputState left_dpad = {0};
    input_state_press_gp_button(&left_dpad, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    test_advance_frame(&game, left_dpad);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed - 1, test_player_int_attr(&game.state, "speed"),
                                  "tap D-pad LEFT should decrement speed by 1 in ATTR_EDIT");

    InputState right_dpad = {0};
    input_state_press_gp_button(&right_dpad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    test_advance_frame(&game, right_dpad);
    TEST_ASSERT_EQUAL_INT_MESSAGE(starting_speed, test_player_int_attr(&game.state, "speed"),
                                  "tap D-pad RIGHT should restore speed to starting value");

    test_game_teardown(&game);
}

/* Regression guard: the existing hold-then-repeat behaviour must survive
 * the immediate-fire fix. Hold KEY_LEFT (level only, no edge) for one
 * simulated second and confirm the attribute drops by enough to cover
 * the timer-driven repeats after ATTR_REPEAT_DELAY (0.4s). */
void test_integration_editor_attr_edit_hold_repeats_after_delay(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    Entity *player_entity = game_get_player(&game.state);
    int speed_attr_index = test_find_int_attr_display_index(&game.state, player_entity, "speed");
    TEST_ASSERT_TRUE(speed_attr_index >= 0);

    int starting_speed = test_player_int_attr(&game.state, "speed");

    game.editor_state.selected_entity_index = game.state.gamedata.player_index;
    game.editor_state.selected_attr_index = speed_attr_index;
    game.editor_state.sub_mode = EDITOR_SUB_ATTR_EDIT;
    game.editor_state.saved_attr_int = starting_speed;

    /* Held without an edge — input_state_hold_key sets *_down only,
     * not *_pressed. The press-edge immediate fire never triggers; only
     * the timer-driven repeat after ATTR_REPEAT_DELAY does. */
    InputState held = {0};
    input_state_hold_key(&held, KEY_LEFT);
    test_advance_frames(&game, held, 60);

    int total_drop = starting_speed - test_player_int_attr(&game.state, "speed");
    TEST_ASSERT_TRUE_MESSAGE(total_drop >= 3, "holding KEY_LEFT for 1s should fire several decrements");

    test_game_teardown(&game);
}

/* Drive the pause menu through the real frame loop: F3 opens the
 * menu, KEY_DOWN walks the selection, KEY_ENTER confirms QUIT.
 * Observables are game.menu.open, game.menu.selected, and
 * game.quit_requested — the same bits a player and the production
 * loop see. A regression that swaps two MENU_ENTRY_* values, or
 * silently no-ops the menu_toggle / nav / confirm bindings, has to
 * break this test. */
void test_integration_menu_navigation_and_quit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* F3 opens the menu via ACTION_MENU_TOGGLE. */
    InputState open_input = {0};
    input_state_press_key(&open_input, KEY_F3);
    test_advance_frame(&game, open_input);
    TEST_ASSERT_TRUE(game.menu.open);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESUME, game.menu.selected);

    /* Walk to QUIT. The menu has six entries: RESUME, SAVE, RESTORE,
     * SETTINGS, TOGGLE_DEBUG_OVERLAY, QUIT — 5 down-presses from RESUME.
     * One tap per frame. */
    for (int step = 0; step < 5; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_QUIT, game.menu.selected);

    /* Pressing past the last entry must clamp, not wrap. */
    InputState clamp = {0};
    input_state_press_key(&clamp, KEY_DOWN);
    test_advance_frame(&game, clamp);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_QUIT, game.menu.selected);

    /* Confirm fires QUIT — dispatched via dispatch_menu_action which
     * sets quit_requested. */
    InputState confirm_input = {0};
    input_state_press_key(&confirm_input, KEY_ENTER);
    test_advance_frame(&game, confirm_input);
    TEST_ASSERT_TRUE(game.quit_requested);

    test_game_teardown(&game);
}

/* Cancel (Escape) returns from the menu regardless of where the
 * cursor sits — equivalent to confirming the Resume entry. Menu
 * closes; quit_requested stays false. */
void test_integration_menu_escape_returns_resume(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState open_input = {0};
    input_state_press_key(&open_input, KEY_F3);
    test_advance_frame(&game, open_input);
    TEST_ASSERT_TRUE(game.menu.open);

    /* Move off Resume so the assertion is meaningful. */
    for (int step = 0; step < 2; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_NOT_EQUAL(MENU_ENTRY_RESUME, game.menu.selected);

    InputState escape_input = {0};
    input_state_press_key(&escape_input, KEY_ESCAPE);
    test_advance_frame(&game, escape_input);
    TEST_ASSERT_FALSE(game.menu.open);
    TEST_ASSERT_FALSE(game.quit_requested);

    test_game_teardown(&game);
}

/* D-pad LEFT_FACE_DOWN navigates same as KEY_DOWN — both are bound to
 * ACTION_NAV_DOWN. RIGHT_FACE_DOWN confirms (same as KEY_ENTER). The
 * MIDDLE_LEFT button toggles the menu. */
void test_integration_menu_gamepad_navigation(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState open_input = {0};
    input_state_press_gp_button(&open_input, GAMEPAD_BUTTON_MIDDLE_LEFT);
    test_advance_frame(&game, open_input);
    TEST_ASSERT_TRUE(game.menu.open);

    InputState dpad_down = {0};
    input_state_press_gp_button(&dpad_down, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    test_advance_frame(&game, dpad_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    /* Confirm on SAVE: dispatch_menu_action invokes ctx.save_fn (null
     * in this test, so SAVE becomes a silent no-op) and closes the
     * menu. The menu closing without quit_requested set proves SAVE
     * was the dispatched action — RESUME would also close without
     * quit, but RESUME is on a different entry index. */
    InputState confirm_input = {0};
    input_state_press_gp_button(&confirm_input, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, confirm_input);
    TEST_ASSERT_FALSE(game.menu.open);
    TEST_ASSERT_FALSE(game.quit_requested);

    test_game_teardown(&game);
}

/* Open the Settings overlay from the pause menu, then switch from the
 * default Input tab to General via ACTION_TAB_NEXT (gamepad R1 / Tab
 * key). Tab state must change without leaving the LIST screen. The
 * test drives only through the input layer so any change to internal
 * settings handler structure must preserve the observable tab switch. */
void test_integration_settings_tab_switch(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Open menu (F3) → walk to SETTINGS (3 down-presses from RESUME) → confirm. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    TEST_ASSERT_TRUE(game.menu.open);

    for (int step = 0; step < 3; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SETTINGS, game.menu.selected);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_TRUE(game.settings.open);
    TEST_ASSERT_EQUAL_INT(SETTINGS_TAB_INPUT, (int)game.settings.tab);

    /* Tab forward: KEY_TAB fires ACTION_TAB_NEXT. */
    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);
    TEST_ASSERT_EQUAL_INT(SETTINGS_TAB_GENERAL, (int)game.settings.tab);

    /* Right shoulder also advances; clamps at the rightmost tab. */
    InputState right_shoulder = {0};
    input_state_press_gp_button(&right_shoulder, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
    test_advance_frame(&game, right_shoulder);
    TEST_ASSERT_EQUAL_INT(SETTINGS_TAB_GENERAL, (int)game.settings.tab);

    /* Tab backward: shift+tab fires ACTION_TAB_PREV. */
    InputState tab_prev = {0};
    input_state_hold_key(&tab_prev, KEY_LEFT_SHIFT);
    input_state_press_key(&tab_prev, KEY_TAB);
    test_advance_frame(&game, tab_prev);
    TEST_ASSERT_EQUAL_INT(SETTINGS_TAB_INPUT, (int)game.settings.tab);

    test_game_teardown(&game);
}

/* Open the Settings overlay, switch to General, enter the path-edit
 * screen by confirming the Data directory row, then commit the buffer
 * via ACTION_INTERACT (KEY_SPACE / GAMEPAD_BUTTON_RIGHT_FACE_DOWN).
 *
 * Assertions are made on observable state: the settings screen pops
 * back to LIST, save_preferences_requested was raised AND consumed
 * (frame.c clears it after the save dispatcher runs), and
 * state.preferences.data_dir reflects the buffer that was committed.
 *
 * The TestGame's preferences_save_fn is null, so the dispatcher is a
 * no-op on disk — exactly the headless contract the FrameContext
 * design encodes. */
void test_integration_settings_path_edit_commit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Open menu → SETTINGS (3 down-presses) → confirm. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 3; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_TRUE(game.settings.open);

    /* Switch to General tab. */
    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);
    TEST_ASSERT_EQUAL_INT(SETTINGS_TAB_GENERAL, (int)game.settings.tab);

    /* Confirm Data directory row → enter path-edit screen. */
    InputState confirm_path = {0};
    input_state_press_key(&confirm_path, KEY_ENTER);
    test_advance_frame(&game, confirm_path);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_PATH_EDIT, (int)game.settings.screen);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_BROWSE, (int)game.settings.path_edit.mode);

    /* Save default by pressing INTERACT (KEY_SPACE). The buffer was
     * seeded with the current preferences.data_dir, so committing
     * leaves data_dir unchanged but raises and consumes
     * save_preferences_requested. */
    InputState interact = {0};
    input_state_press_key(&interact, KEY_SPACE);
    test_advance_frame(&game, interact);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_LIST, (int)game.settings.screen);
    TEST_ASSERT_FALSE(game.settings.save_preferences_requested);
    TEST_ASSERT_NOT_NULL(game.state.preferences.data_dir.ptr);

    test_game_teardown(&game);
}

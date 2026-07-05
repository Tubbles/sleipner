#include "unity.h"
#include "arena.h"
#include "atlas.h"
#include "attribute.h"
#include "blueprint.h"
#include "editor/editor.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "input.h"
#include "input_func.h"
#include "inventory_screen.h"
#include "level.h"
#include "menu.h"
#include "rule.h"
#include "strv.h"
#include "test_helpers.h"
#include "undo.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* Fixture for per-level music crossfade (S6.13b, D32): three levels,
 * "field" (music theme_a.mp3) -> "interior" (music theme_b.mp3, a
 * DIFFERENT track -- crossfade should start) -> "interior2" (music
 * theme_b.mp3 too, the SAME track as "interior" -- crossfade should NOT
 * restart even though the level itself changes). Player at (100,100),
 * door_to_interior at (200,100); interior's player at (80,60),
 * door_to_interior2 at (80,110). */
static const char *fixture_music_transition = "[[blueprint]]\n"
                                              "name = \"player\"\n"
                                              "texture = \"player.png\"\n"
                                              "src = [0, 0, 32, 32]\n"
                                              "collision_offset = [0, 0]\n"
                                              "collision_size = [16, 16]\n"
                                              "behavior = \"player\"\n"
                                              "speed = 80\n"
                                              "\n"
                                              "[[blueprint]]\n"
                                              "name = \"door_to_interior\"\n"
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
                                              "name = \"door_to_interior2\"\n"
                                              "texture = \"rock.png\"\n"
                                              "src = [0, 0, 16, 16]\n"
                                              "collision_offset = [0, 0]\n"
                                              "collision_size = [32, 32]\n"
                                              "solid = false\n"
                                              "\n"
                                              "[[blueprint.rule]]\n"
                                              "trigger = \"enter\"\n"
                                              "actions = [\"transition:interior2,40,40\"]\n"
                                              "\n"
                                              "[[level]]\n"
                                              "name = \"field\"\n"
                                              "music = \"theme_a.mp3\"\n"
                                              "size = [320, 240]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"player\"\n"
                                              "pos = [100, 100]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"door_to_interior\"\n"
                                              "pos = [200, 100]\n"
                                              "\n"
                                              "[[level]]\n"
                                              "name = \"interior\"\n"
                                              "music = \"theme_b.mp3\"\n"
                                              "size = [160, 120]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"player\"\n"
                                              "pos = [80, 60]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"door_to_interior2\"\n"
                                              "pos = [80, 110]\n"
                                              "\n"
                                              "[[level]]\n"
                                              "name = \"interior2\"\n"
                                              "music = \"theme_b.mp3\"\n"
                                              "size = [100, 100]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"player\"\n"
                                              "pos = [40, 40]\n";

/* Fixture for give_item/remove_item/has_item (S6.8a, D25). Player at
 * (50,50); item_giver/item_remover/item_checker sit 100px apart on the
 * same row so INTERACT_RANGE (24px) never overlaps two at once. Each
 * fires on interact: giver gives "key", remover removes it, checker
 * sets "key_present" only if has_item:key currently holds -- this rule
 * is the give/has_item repro: it would fire (wrongly) even before any
 * give under the old COND_HAS_ITEM stub (always true). */
static const char *fixture_items = "[[blueprint]]\n"
                                   "name = \"player\"\n"
                                   "texture = \"player.png\"\n"
                                   "src = [0, 0, 32, 32]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [16, 16]\n"
                                   "behavior = \"player\"\n"
                                   "speed = 80\n"
                                   "\n"
                                   "[[blueprint]]\n"
                                   "name = \"item_giver\"\n"
                                   "texture = \"rock.png\"\n"
                                   "src = [0, 0, 16, 16]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [16, 16]\n"
                                   "solid = false\n"
                                   "\n"
                                   "[[blueprint.rule]]\n"
                                   "trigger = \"interact\"\n"
                                   "actions = [\"give_item:key\"]\n"
                                   "\n"
                                   "[[blueprint]]\n"
                                   "name = \"item_remover\"\n"
                                   "texture = \"rock.png\"\n"
                                   "src = [0, 0, 16, 16]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [16, 16]\n"
                                   "solid = false\n"
                                   "\n"
                                   "[[blueprint.rule]]\n"
                                   "trigger = \"interact\"\n"
                                   "actions = [\"remove_item:key\"]\n"
                                   "\n"
                                   "[[blueprint]]\n"
                                   "name = \"item_checker\"\n"
                                   "texture = \"rock.png\"\n"
                                   "src = [0, 0, 16, 16]\n"
                                   "collision_offset = [0, 0]\n"
                                   "collision_size = [16, 16]\n"
                                   "solid = false\n"
                                   "\n"
                                   "[[blueprint.rule]]\n"
                                   "trigger = \"interact\"\n"
                                   "conditions = [\"has_item:key\"]\n"
                                   "actions = [\"set_flag:key_present\"]\n"
                                   "\n"
                                   "[[level]]\n"
                                   "name = \"test\"\n"
                                   "size = [400, 300]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"player\"\n"
                                   "pos = [50, 50]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"item_giver\"\n"
                                   "pos = [150, 50]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"item_remover\"\n"
                                   "pos = [250, 50]\n"
                                   "\n"
                                   "[[level.entity]]\n"
                                   "blueprint = \"item_checker\"\n"
                                   "pos = [350, 50]\n";

/* Two-level fixture for the item-survives-transition test: give an item
 * in "field" (item_giver, interact), walk into "door" (enter ->
 * transition:interior), then the "interior" level's item_checker fires
 * has_item:key on_spawn -- proving the item's map entry survived the
 * transition's gamedata_arena rewind (rides S2.5, mirrors
 * fixture_transition/test_integration_progression_survives_transition). */
static const char *fixture_item_transition = "[[blueprint]]\n"
                                             "name = \"player\"\n"
                                             "texture = \"player.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "behavior = \"player\"\n"
                                             "speed = 80\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"item_giver\"\n"
                                             "texture = \"rock.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "solid = false\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"interact\"\n"
                                             "actions = [\"give_item:key\"]\n"
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
                                             "name = \"item_checker\"\n"
                                             "texture = \"rock.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "solid = false\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"on_spawn\"\n"
                                             "conditions = [\"has_item:key\"]\n"
                                             "actions = [\"set_flag:key_present\"]\n"
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
                                             "blueprint = \"item_giver\"\n"
                                             "pos = [150, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"door\"\n"
                                             "pos = [250, 100]\n"
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
                                             "blueprint = \"item_checker\"\n"
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

/* Regression guard for TODO.md's "wall-warp" report ("running against the
 * walls significantly warps the sprite"). Drives the player into every
 * cardinal level-boundary wall and both reachable corners of the "field"
 * level (320x240), recording position every single frame, and asserts no
 * frame-to-frame move ever exceeds one movement step. A "warp" is a
 * discontinuous jump far larger than a single step; smooth clamping against
 * the boundary (or push-back from the solid rock/tree obstacles the
 * cardinal runs also reach) must never produce one. */
void test_integration_wall_press_no_position_jump(void)
{
    /* speed=80px/s (fixture_gamedata) at the fixed 1/60s test delta_time =>
     * ~1.333px/frame max single-axis step. Diagonal input is clamped to the
     * unit disc (input_apply_deadzone), so it never exceeds that same
     * per-axis magnitude either. Generous epsilon for float slop. */
    const float max_step_per_axis = (DEFAULT_PLAYER_SPEED / 60.0F) + 0.5F;

    struct {
        float axis_x;
        float axis_y;
        const char *label;
    } approaches[] = {
        {-1.0F, 0.0F, "left wall"},          {1.0F, 0.0F, "right wall"},          {0.0F, -1.0F, "top wall"},
        {0.0F, 1.0F, "bottom wall"},         {-1.0F, -1.0F, "top-left corner"},   {1.0F, -1.0F, "top-right corner"},
        {-1.0F, 1.0F, "bottom-left corner"}, {1.0F, 1.0F, "bottom-right corner"},
    };

    for (size_t approach = 0; approach < sizeof(approaches) / sizeof(approaches[0]); approach++) {
        TestGame game;
        TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

        InputState input = {0};
        input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, approaches[approach].axis_x);
        input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_Y, approaches[approach].axis_y);

        Vector2 previous = game_get_player_const(&game.state)->position;
        for (int frame = 0; frame < 400; frame++) {
            test_advance_frame(&game, input);
            Vector2 current = game_get_player_const(&game.state)->position;
            char message[128];
            (void)snprintf(message, sizeof(message), "%s: frame %d moved (%.3f, %.3f) (max %.3fpx/axis)",
                           approaches[approach].label, frame, (double)(current.x - previous.x),
                           (double)(current.y - previous.y), (double)max_step_per_axis);
            TEST_ASSERT_TRUE_MESSAGE(fabsf(current.x - previous.x) <= max_step_per_axis, message);
            TEST_ASSERT_TRUE_MESSAGE(fabsf(current.y - previous.y) <= max_step_per_axis, message);
            previous = current;
        }

        test_game_teardown(&game);
    }
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
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "beacon_spawned"));

    test_game_teardown(&game);
}

void test_integration_enter_trigger_fires_on_overlap(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_triggers));

    /* zone_entered must not be set before the player reaches the zone */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "zone_entered"));

    /* Player at (100,100), collision [100,100,16,16]. Zone at (200,100), collision [200,100,32,32].
     * Player right edge starts at 116, zone left edge at 200. Gap = 84px.
     * Speed = 80 px/s → need ~63 frames at 1/60s. Run 80 to be safe. */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 80);

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "zone_entered"));

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

void test_integration_change_sprite_action_updates_source_rect(void)
{
    /* Zone blueprint starts at src = [0,0,16,16] and has enter ->
     * change_sprite:16,32,48,64. Player/zone geometry copied from
     * fixture_triggers so the walk-in-and-overlap timing matches
     * test_integration_enter_trigger_fires_on_overlap. */
    static const char *fixture_change_sprite = "[[blueprint]]\n"
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
                                               "actions = [\"change_sprite:16,32,48,64\"]\n"
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
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_change_sprite));

    /* Before the player ever reaches the zone, the source rect must still be
     * the blueprint's declared src = [0,0,16,16]. */
    const Entity *zone = test_find_entity_by_blueprint(&game.state, "zone");
    TEST_ASSERT_NOT_NULL(zone);
    const AttrSet *defaults_before = entity_resolve_defaults(&game.state, zone->id);
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&zone->attrs, defaults_before, "src_x", -1.0F));
    TEST_ASSERT_EQUAL_INT(16, (int)attr_get_scoped_float(&zone->attrs, defaults_before, "src_w", -1.0F));

    /* Walk into the zone (same geometry/timing as the enter-trigger test above) */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, input, 80);

    zone = test_find_entity_by_blueprint(&game.state, "zone");
    TEST_ASSERT_NOT_NULL(zone);
    const AttrSet *defaults = entity_resolve_defaults(&game.state, zone->id);
    TEST_ASSERT_EQUAL_INT(16, (int)attr_get_scoped_float(&zone->attrs, defaults, "src_x", 0.0F));
    TEST_ASSERT_EQUAL_INT(32, (int)attr_get_scoped_float(&zone->attrs, defaults, "src_y", 0.0F));
    TEST_ASSERT_EQUAL_INT(48, (int)attr_get_scoped_float(&zone->attrs, defaults, "src_w", 0.0F));
    TEST_ASSERT_EQUAL_INT(64, (int)attr_get_scoped_float(&zone->attrs, defaults, "src_h", 0.0F));

    test_game_teardown(&game);
}

void test_integration_play_sound_enqueues(void)
{
    /* Same player/zone geometry and enter-trigger timing as
     * test_integration_change_sprite_action_updates_source_rect above,
     * swapping the action for play_sound:pickup.wav. "pickup.wav" matches
     * one of the two placeholder SFX main.c's load_persistent_assets
     * registers into the SFX registry (S6.4); this fixture doesn't need
     * that registry to exist, since the assertion below is purely about
     * the VM enqueuing the request. */
    static const char *fixture_play_sound = "[[blueprint]]\n"
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
                                            "actions = [\"play_sound:pickup.wav\"]\n"
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
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_play_sound));
    TEST_ASSERT_EQUAL_INT(0, game.state.effects.sounds.count);

    /* Drive game_update directly rather than test_advance_frame(s): D32's
     * per-frame drain (frame.c's apply_effect_queue, run right after
     * game_update inside run_active_frame) clears state->effects every
     * frame it runs, including the one that pushes this request, so
     * inspecting the queue through frame_update would always see it
     * already emptied. Calling game_update -- the engine's headless
     * update() entry point -- drives the real rule VM through the same
     * real InputState and skips only that same-frame drain, which is
     * exactly what "headless asserts queue" (D32) needs to observe. */
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 1.0F);
    for (int frame = 0; frame < 80; frame++) {
        game_update(&game.diag, &game.state, input, 1.0F / 60.0F);
    }

    TEST_ASSERT_EQUAL_INT(1, game.state.effects.sounds.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(game.state.effects.sounds.data[0].name, "pickup.wav"));

    test_game_teardown(&game);
}

void test_integration_camera_pan_moves_target(void)
{
    /* Same 84px-gap enter-trigger geometry as fixture_triggers /
     * test_integration_enter_trigger_fires_on_overlap, translated +200 in
     * X and embedded in an 800x600 level (viewport is the 320x240
     * test_helpers.c default) so both the walk-in path and the pan target
     * below sit inside camera_clamp_target's unclamped range -- a level
     * only as big as the 320x240/240x sized fixtures elsewhere in this
     * file would force camera_target to the level center regardless of
     * the pan, since level_size <= viewport takes the centering branch. */
    static const char *fixture_camera_pan = "[[blueprint]]\n"
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
                                            "actions = [\"camera_pan:600,450,1.0\", \"set_flag:zone_entered\"]\n"
                                            "\n"
                                            "[[level]]\n"
                                            "name = \"test\"\n"
                                            "size = [800, 600]\n"
                                            "\n"
                                            "[[level.entity]]\n"
                                            "blueprint = \"player\"\n"
                                            "pos = [300, 300]\n"
                                            "\n"
                                            "[[level.entity]]\n"
                                            "blueprint = \"zone\"\n"
                                            "pos = [400, 300]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_camera_pan));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "zone_entered"));

    /* Walk right one frame at a time until the enter trigger fires --
     * the exact frame is needed (not just "fired somewhere in N frames")
     * so the elapsed-time checks below line up with a known pan start. */
    InputState walk = {0};
    input_state_set_gp_axis(&walk, GAMEPAD_AXIS_LEFT_X, 1.0F);
    bool triggered = false;
    for (int frame = 0; frame < 100; frame++) {
        test_advance_frame(&game, walk);
        if (flag_get(&game.state.progression.flags, "zone_entered")) {
            triggered = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(triggered, "enter trigger never fired");

    /* apply_effect_queue (frame.c) already ran this same frame and
     * captured this camera_target as the pan's `from` -- see
     * apply_camera_pan_effects. */
    Vector2 pan_start = game.state.gamedata.camera_target;

    /* Player goes idle from here on: pan overriding follow (D22/D26) is
     * the only thing that may still move camera_target. */
    InputState idle = {0};

    /* Half the 1.0s duration (60 frames at 1/60s) -- camera_target should
     * be roughly midway from pan_start to the pan target (600, 450). */
    test_advance_frames(&game, idle, 30);
    Vector2 midpoint = game.state.gamedata.camera_target;
    float expected_mid_x = pan_start.x + (600.0F - pan_start.x) * 0.5F;
    float expected_mid_y = pan_start.y + (450.0F - pan_start.y) * 0.5F;
    TEST_ASSERT_FLOAT_WITHIN(2.0F, expected_mid_x, midpoint.x);
    TEST_ASSERT_FLOAT_WITHIN(2.0F, expected_mid_y, midpoint.y);

    /* Remaining half -- pan should have reached its target exactly */
    test_advance_frames(&game, idle, 30);
    Vector2 at_end = game.state.gamedata.camera_target;
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 600.0F, at_end.x);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 450.0F, at_end.y);

    /* Pan is over: normal player-follow must resume. The idle player
     * stopped well short of (600, 450), so a resumed follow pulls
     * camera_target measurably back toward it over further idle frames --
     * observed through camera_target itself, not camera_effect.pan.active. */
    test_advance_frames(&game, idle, 30);
    Vector2 after_pan = game.state.gamedata.camera_target;
    TEST_ASSERT_TRUE_MESSAGE(after_pan.x < at_end.x - 1.0F, "follow did not resume after pan completed");

    test_game_teardown(&game);
}

void test_integration_spawn_creates_entity(void)
{
    /* Same 84px-gap enter-trigger geometry as fixture_play_sound /
     * fixture_camera_pan above -- walking the player from (100,100) into
     * the zone at (200,100) fires "enter" well inside 100 frames at
     * speed=80. The zone's rule spawns a "goblin" (a distinct blueprint
     * from anything already in the level, with its own default attr) at
     * a fixed position away from both player and zone. */
    static const char *fixture_spawn = "[[blueprint]]\n"
                                       "name = \"player\"\n"
                                       "texture = \"player.png\"\n"
                                       "src = [0, 0, 32, 32]\n"
                                       "collision_offset = [0, 0]\n"
                                       "collision_size = [16, 16]\n"
                                       "behavior = \"player\"\n"
                                       "speed = 80\n"
                                       "\n"
                                       "[[blueprint]]\n"
                                       "name = \"goblin\"\n"
                                       "texture = \"rock.png\"\n"
                                       "src = [0, 0, 16, 16]\n"
                                       "collision_offset = [0, 0]\n"
                                       "collision_size = [16, 16]\n"
                                       "hp = 7\n"
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
                                       "actions = [\"spawn:goblin,250,180\"]\n"
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
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_spawn));
    TEST_ASSERT_EQUAL_INT(0, test_count_entities_by_blueprint(&game.state, "goblin"));

    /* Drive test_advance_frame (the real frame_update path, not
     * game_update directly) so the spawn is applied by
     * apply_effect_queue's drain, not just enqueued -- unlike
     * test_integration_play_sound_enqueues, this test asserts on the
     * post-drain world, not the raw queue. */
    InputState walk = {0};
    input_state_set_gp_axis(&walk, GAMEPAD_AXIS_LEFT_X, 1.0F);
    bool spawned = false;
    for (int frame = 0; frame < 100; frame++) {
        test_advance_frame(&game, walk);
        if (test_count_entities_by_blueprint(&game.state, "goblin") > 0) {
            spawned = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(spawned, "spawn action never fired");
    TEST_ASSERT_EQUAL_INT(1, test_count_entities_by_blueprint(&game.state, "goblin"));

    /* The player is standing inside the zone right now -- that is what just
     * fired the one-shot enter. Hold it there (idle) for more frames and
     * assert the count stays EXACTLY one: the spawn's tracking rebuild must
     * preserve the player-overlap edge state, or detect_enter_targets sees
     * "not overlapping last frame" again next frame and refires enter,
     * spawning a fresh goblin every frame. Count-exact, so a refire fails. */
    InputState idle = {0};
    test_advance_frames(&game, idle, 30);
    TEST_ASSERT_EQUAL_INT(1, test_count_entities_by_blueprint(&game.state, "goblin"));

    Entity *goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    TEST_ASSERT_NOT_NULL(goblin);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 250.0F, goblin->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 180.0F, goblin->position.y);

    /* Blueprint defaults must be resolvable -- this only works if
     * the spawn's setup_current_level_runtime rebuild actually
     * registered the new entity's id in entity_blueprints (F24's map
     * fix-up), not just pushed it into current_level.entities. */
    const AttrSet *defaults = entity_resolve_defaults(&game.state, goblin->id);
    TEST_ASSERT_NOT_NULL(defaults);
    TEST_ASSERT_EQUAL_INT(7, attr_get_scoped_int(&goblin->attrs, defaults, "hp", -1));

    test_game_teardown(&game);
}

void test_integration_spawn_inside_for_each_deferred(void)
{
    /* Three "target_marker" entities (marked via a custom is_target attr,
     * so the for_each filter can select exactly them and not the player/
     * zone/goblin blueprints) sit away from the player's walk path. The
     * zone's enter rule iterates every entity, filters to the marked
     * three, and spawns one goblin per match -- exercising for_each +
     * spawn together, the core F24 safety scenario: if spawn mutated
     * current_level.entities synchronously instead of deferring to the
     * drain, this is exactly the setup (a for_each body enqueuing spawns)
     * that could revisit or corrupt the array it is still iterating. */
    static const char *fixture_for_each_spawn = "[[blueprint]]\n"
                                                "name = \"player\"\n"
                                                "texture = \"player.png\"\n"
                                                "src = [0, 0, 32, 32]\n"
                                                "collision_offset = [0, 0]\n"
                                                "collision_size = [16, 16]\n"
                                                "behavior = \"player\"\n"
                                                "speed = 80\n"
                                                "\n"
                                                "[[blueprint]]\n"
                                                "name = \"target_marker\"\n"
                                                "texture = \"rock.png\"\n"
                                                "src = [0, 0, 16, 16]\n"
                                                "collision_offset = [0, 0]\n"
                                                "collision_size = [16, 16]\n"
                                                "solid = false\n"
                                                "is_target = true\n"
                                                "\n"
                                                "[[blueprint]]\n"
                                                "name = \"goblin\"\n"
                                                "texture = \"rock.png\"\n"
                                                "src = [0, 0, 16, 16]\n"
                                                "collision_offset = [0, 0]\n"
                                                "collision_size = [16, 16]\n"
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
                                                "actions = [{ for_each = \"entities\", bind = \"target\", "
                                                "conditions = [\"attr:is_target\"], do = [\"spawn:goblin,999,999\"] "
                                                "}]\n"
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
                                                "blueprint = \"target_marker\"\n"
                                                "pos = [10, 10]\n"
                                                "\n"
                                                "[[level.entity]]\n"
                                                "blueprint = \"target_marker\"\n"
                                                "pos = [20, 20]\n"
                                                "\n"
                                                "[[level.entity]]\n"
                                                "blueprint = \"target_marker\"\n"
                                                "pos = [30, 30]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_for_each_spawn));
    TEST_ASSERT_EQUAL_INT(3, test_count_entities_by_blueprint(&game.state, "target_marker"));
    TEST_ASSERT_EQUAL_INT(0, test_count_entities_by_blueprint(&game.state, "goblin"));

    InputState walk = {0};
    input_state_set_gp_axis(&walk, GAMEPAD_AXIS_LEFT_X, 1.0F);
    bool spawned = false;
    for (int frame = 0; frame < 100; frame++) {
        test_advance_frame(&game, walk);
        if (test_count_entities_by_blueprint(&game.state, "goblin") > 0) {
            spawned = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(spawned, "for_each spawn action never fired");

    /* Exactly one goblin per target_marker, never more. The batch's
     * EntityView array (game.c) is built once per batch with a fixed
     * view_count captured by value before rules_evaluate_batch runs, so
     * even a hypothetical non-deferred spawn couldn't extend the for_each
     * loop bound mid-iteration -- but a synchronous spawn growing/
     * reallocating current_level.entities while for_each still holds
     * Entity* pointers into the pre-growth array is exactly the
     * corruption CLAUDE.md's vec-growth rule warns about. Deferring to
     * the drain (after game_update returns, after this whole batch is
     * done iterating) sidesteps it entirely. */
    TEST_ASSERT_EQUAL_INT(3, test_count_entities_by_blueprint(&game.state, "goblin"));

    /* A few more idle frames rule out a delayed second wave, not just an
     * immediate one. */
    InputState idle = {0};
    test_advance_frames(&game, idle, 10);
    TEST_ASSERT_EQUAL_INT(3, test_count_entities_by_blueprint(&game.state, "goblin"));

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
    char *content = read_file(GAMEDATA_FIXTURE_PATH);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "could not read " GAMEDATA_FIXTURE_PATH);

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
    char *content = read_file(GAMEDATA_FIXTURE_PATH);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "could not read " GAMEDATA_FIXTURE_PATH);

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

/* S6.14, D27: a level transition now runs a 0.3s fade-out -> swap-at-
 * midpoint -> 0.3s fade-in state machine instead of swapping the instant
 * the trigger fires. Driven through the same fixture/enter-trigger path
 * as test_integration_transition_changes_level above -- this test adds
 * the fade-timing and input-suppression assertions D27 requires. Verified
 * to fail (the level is already "interior" the very frame the fade
 * starts) against a temporary revert of handle_transition to the
 * pre-S6.14 instant swap before writing the fix. */
void test_integration_transition_fades_and_suppresses_input(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_transition));

    /* Walk right into the door trigger (player at (100,100), door at
     * (200,100), speed 80px/s -- ~64 frames to reach) until the fade
     * itself starts. */
    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    int max_iterations = 200;
    int iteration = 0;
    while (iteration < max_iterations && game.state.fade.phase == TRANSITION_FADE_NONE) {
        test_advance_frame(&game, right);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "fade should start within 200 frames");
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_OUT, game.state.fade.phase);

    /* Key D27 guarantee: the swap happens at the fade's midpoint, not
     * instantly -- the level is still "field" the frame the fade starts. */
    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    Vector2 position_at_fade_start = game_get_player_const(&game.state)->position;

    /* Feed movement input for several frames while still fading out.
     * TRANSITION_FADE_SECONDS is 0.3s = 18 frames at 1/60s; 10 is
     * comfortably inside that window. Input must be suppressed (no
     * movement) and the level must not have swapped yet. */
    for (int frame = 0; frame < 10; frame++) {
        test_advance_frame(&game, right);
    }
    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    Vector2 position_mid_fade_out = game_get_player_const(&game.state)->position;
    TEST_ASSERT_EQUAL_FLOAT(position_at_fade_start.x, position_mid_fade_out.x);
    TEST_ASSERT_EQUAL_FLOAT(position_at_fade_start.y, position_mid_fade_out.y);

    /* Continue past the ~0.3s mark: the swap must have landed by now,
     * at the spawn point the transition action declared. */
    iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "field") == 0) {
        test_advance_frame(&game, right);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "swap should land within 200 frames");
    TEST_ASSERT_EQUAL_STRING("interior", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_IN, game.state.fade.phase);
    Vector2 position_after_swap = game_get_player_const(&game.state)->position;

    /* Still suppressed during fade-in: feed movement, player must not
     * drift from the spawn point. */
    for (int frame = 0; frame < 10; frame++) {
        test_advance_frame(&game, right);
    }
    Vector2 position_mid_fade_in = game_get_player_const(&game.state)->position;
    TEST_ASSERT_EQUAL_FLOAT(position_after_swap.x, position_mid_fade_in.x);
    TEST_ASSERT_EQUAL_FLOAT(position_after_swap.y, position_mid_fade_in.y);

    /* Run out the fade-in's remaining ~0.3s: control resumes once the
     * fade returns to idle. */
    iteration = 0;
    InputState idle = {0};
    while (iteration < max_iterations && game.state.fade.phase != TRANSITION_FADE_NONE) {
        test_advance_frame(&game, idle);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "fade-in should end within 200 frames");

    Vector2 position_before_final_move = game_get_player_const(&game.state)->position;
    test_advance_frame(&game, right);
    Vector2 position_after_final_move = game_get_player_const(&game.state)->position;
    TEST_ASSERT_TRUE(position_after_final_move.x > position_before_final_move.x);

    test_game_teardown(&game);
}

/* S6.13b, D32: loading a level sets its music_name as the current track
 * with no crossfade -- there is nothing playing yet to fade from. Driven
 * through the real load path (test_game_setup -> game_load_gamedata). */
void test_integration_level_load_sets_music_with_no_crossfade(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_music_transition));

    TEST_ASSERT_EQUAL_STRING("theme_a.mp3", game.state.music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("", game.state.music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, game.state.music.crossfade_timer);

    test_game_teardown(&game);
}

/* S6.13b, D32: transitioning into a level whose music_name differs from
 * the current track starts a 1.0s linear crossfade -- the old track
 * becomes "outgoing" at a fresh full timer, the new track becomes
 * "current". Driven through the real enter-trigger -> transition:interior
 * path (frame_update + handle_transition), the same mechanism
 * test_integration_transition_changes_level exercises -- not a direct
 * game_load_gamedata/level_activate call. */
void test_integration_transition_to_different_track_starts_crossfade(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_music_transition));

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

    TEST_ASSERT_EQUAL_STRING("theme_b.mp3", game.state.music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("theme_a.mp3", game.state.music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(MUSIC_CROSSFADE_SECONDS, game.state.music.crossfade_timer);

    test_game_teardown(&game);
}

/* S6.13b, D32: once the crossfade above finishes (ticked past
 * MUSIC_CROSSFADE_SECONDS at 1/60s per frame -- 61 frames is enough) and
 * the player then walks into a SECOND level that names the SAME track
 * ("interior2", also theme_b.mp3), no new crossfade starts -- the track
 * NAME, not the level identity, drives the decision. */
void test_integration_transition_to_same_track_does_not_restart_crossfade(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_music_transition));

    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    int max_iterations = 200;
    int iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "field") == 0) {
        test_advance_frame(&game, right);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "transition to 'interior' should fire within 200 frames");

    InputState idle = {0};
    for (int frame = 0; frame < 61; frame++) {
        test_advance_frame(&game, idle);
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0F, game.state.music.crossfade_timer);
    TEST_ASSERT_EQUAL_STRING("", game.state.music.outgoing_track_name);
    TEST_ASSERT_EQUAL_STRING("theme_b.mp3", game.state.music.current_track_name);

    /* interior's player is at (80,60), door_to_interior2 at (80,110) --
     * walk down (+y). */
    InputState down = {0};
    input_state_set_gp_axis(&down, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "interior") == 0) {
        test_advance_frame(&game, down);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "transition to 'interior2' should fire within 200 frames");
    TEST_ASSERT_EQUAL_STRING("interior2", game.state.gamedata.current_level.name.ptr);

    TEST_ASSERT_EQUAL_STRING("theme_b.mp3", game.state.music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("", game.state.music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, game.state.music.crossfade_timer);

    test_game_teardown(&game);
}

/* Walk the player toward `destination`, re-aiming every frame, until
 * within `range` pixels or `max_iterations` frames elapse. Re-aiming
 * each frame (rather than a fixed heading) rides along any incidental
 * obstacle edge in a straight-line path, but does not route around
 * one — callers crossing a solid obstacle (e.g. a fence) need
 * intermediate waypoints. Returns the frame count actually taken, or
 * max_iterations if it never got close. Parameter order keeps `range`
 * and `max_iterations` non-adjacent (Vector2 between them) to avoid
 * bugprone-easily-swappable-parameters on the float/int pair. */
static int walk_player_to(TestGame *game, float range, Vector2 destination, int max_iterations)
{
    for (int iteration = 0; iteration < max_iterations; iteration++) {
        const Entity *player = game_get_player_const(&game->state);
        float delta_x = destination.x - player->position.x;
        float delta_y = destination.y - player->position.y;
        if ((delta_x * delta_x) + (delta_y * delta_y) <= range * range) {
            return iteration;
        }
        InputState step = {0};
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_X, delta_x > 0.0F ? 1.0F : -1.0F);
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_Y, delta_y > 0.0F ? 1.0F : -1.0F);
        test_advance_frame(game, step);
    }
    return max_iterations;
}

/* F17: progression (flags, global vars) must survive a level
 * transition. Reproduces the shipped gamedata.toml demo: opening a
 * chest in "house_interior" (the default starting level) sets
 * chest_opened, then walking out through exit_door transitions to the
 * much larger "overworld" level. The flag must still read true
 * afterwards — it lives outside the gamedata arena that the
 * transition's arena_restore rewinds. Going small-level -> big-level
 * matters: game_load_gamedata's post-parse per-entity-pair tracking
 * (prev_solid_collisions, sized to the *new* current level's entity
 * count squared) is what actually overwrites the flag's old bytes; a
 * big -> small transition leaves them untouched by coincidence, which
 * is why this exact direction is the reliable repro. */
void test_integration_progression_survives_transition(void)
{
    char *content = read_file(GAMEDATA_FIXTURE_PATH);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "could not read " GAMEDATA_FIXTURE_PATH);

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup_with_level(&game, content, "house_interior"));

    const Entity *chest = test_find_entity_by_blueprint(&game.state, "chest");
    TEST_ASSERT_NOT_NULL(chest);
    (void)walk_player_to(&game, 10.0F, chest->position, 300);

    InputState interact = {0};
    input_state_press_gp_button(&interact, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, interact);

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "chest_opened"));

    const Entity *exit_door = test_find_entity_by_blueprint(&game.state, "exit_door");
    TEST_ASSERT_NOT_NULL(exit_door);
    Vector2 exit_position = exit_door->position;
    int max_iterations = 300;
    int iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "house_interior") == 0) {
        const Entity *player = game_get_player_const(&game.state);
        float delta_x = exit_position.x - player->position.x;
        float delta_y = exit_position.y - player->position.y;
        InputState step = {0};
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_X, delta_x > 0.0F ? 1.0F : -1.0F);
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_Y, delta_y > 0.0F ? 1.0F : -1.0F);
        test_advance_frame(&game, step);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "transition to 'overworld' should fire within 300 frames");
    TEST_ASSERT_EQUAL_STRING("overworld", game.state.gamedata.current_level.name.ptr);

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "chest_opened"));

    test_game_teardown(&game);
    free(content);
}

/* F17: progression must also survive a hot-reload (gamedata.toml
 * edited on disk while the game is running). The mtime-based polling
 * and disk read that trigger this in production are I/O plumbing
 * already excluded from headless tests (see test_level_loader), so the
 * test drives the same game_load_gamedata call test_trigger_hot_reload
 * wraps. */
void test_integration_progression_survives_hot_reload(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_triggers));

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "beacon_spawned"));

    TEST_ASSERT_TRUE(test_trigger_hot_reload(&game));

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "beacon_spawned"));

    test_game_teardown(&game);
}

/* F17: unlike hot-reload, the pause-menu RESTORE action is a deliberate
 * "discard my changes" reset — progression must be cleared, not
 * preserved. Drives the real menu open/navigate/confirm input path;
 * test_restore_fn mirrors main.c's menu_dispatch_restore (reload +
 * game_reset_progression) the same way test_recording_preferences_save
 * mirrors dispatch_save_preferences, since main.c itself isn't linked
 * into the test binary. */
void test_integration_progression_restore_clears_progression(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_triggers));
    game.frame_ctx.restore_fn = test_restore_fn;

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "beacon_spawned"));

    InputState open_input = {0};
    input_state_press_key(&open_input, KEY_F3);
    test_advance_frame(&game, open_input);
    TEST_ASSERT_TRUE(game.menu.open);

    for (int step = 0; step < 2; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESTORE, game.menu.selected);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_FALSE(game.menu.open);

    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "beacon_spawned"));

    test_game_teardown(&game);
}

/* S6.8a, D25: a rule gated on has_item:key must only fire AFTER give_item
 * has run -- this is the repro for the COND_HAS_ITEM stub (rule.c, fixed
 * from "return true" to a real count>0 lookup): with the stub, item_checker
 * would set "key_present" on the very first interact, before any give.
 * Verified against the stub directly (temporarily restoring `return true;`
 * makes this test fail on the first assertion, since key_present would
 * already be true) before writing the fix. */
void test_integration_give_item_then_has_item(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_items));

    Entity *checker = test_find_entity_by_blueprint(&game.state, "item_checker");
    TEST_ASSERT_NOT_NULL(checker);
    (void)walk_player_to(&game, 10.0F, checker->position, 300);

    InputState interact_before = {0};
    input_state_press_gp_button(&interact_before, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, interact_before);
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "key_present"));

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to(&game, 10.0F, giver->position, 300);

    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);
    TEST_ASSERT_TRUE(item_has(&game.state.progression.items, "key"));

    (void)walk_player_to(&game, 10.0F, checker->position, 300);
    InputState interact_after = {0};
    input_state_press_gp_button(&interact_after, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, interact_after);
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "key_present"));

    test_game_teardown(&game);
}

/* S6.8b, D25: give_item enqueues a fire-and-forget toast effect (EffectQueue's
 * ToastRequest) carrying the raw item name; frame.c's apply_toast_effects
 * formats "Got <item>" into editor_state->toast_msg_buf and points
 * toast_text/toast_timer at it, reusing S1.2's existing toast surface.
 * Verified to fail (toast_timer stays 0, no toast) with the
 * effect_queue_push_toast call temporarily removed from rule.c's
 * ACTION_GIVE_ITEM case before writing the fix. */
void test_integration_give_item_shows_toast(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_items));

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to(&game, 10.0F, giver->position, 300);

    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);

    TEST_ASSERT_TRUE(game.editor_state.toast_timer > 0.0F);
    TEST_ASSERT_NOT_NULL(strstr(game.editor_state.toast_text.ptr, "key"));

    test_game_teardown(&game);
}

/* S6.8b, D25: regression test for a toast-tick gating bug this feature
 * exposed. run_active_frame used to tick toast_timer down only inside
 * `if (state->editor_mode)`, which happened to be harmless before this
 * feature because every existing toast source (editor actions, menu
 * save/reload) was only ever reachable from a code path that also ticked
 * (the editor_mode branch itself, or frame_update's menu branch, which
 * ticks unconditionally of editor_mode). give_item is the first toast
 * source reachable from pure play mode -- no editor, no menu -- so with
 * the old gating this toast would never decrement and would stay on
 * screen forever. Verified to fail (toast_timer still > 0 well past
 * TOAST_DURATION) against the editor_mode-gated tick before moving it out
 * unconditionally in run_active_frame (frame.c). 130 frames at 1/60s each
 * is ~2.17s, comfortably past the 2.0s TOAST_DURATION. */
void test_integration_give_item_toast_fades_in_play_mode(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_items));
    TEST_ASSERT_FALSE(game.state.editor_mode);

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to(&game, 10.0F, giver->position, 300);

    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);
    TEST_ASSERT_TRUE(game.editor_state.toast_timer > 0.0F);

    InputState idle = {0};
    test_advance_frames(&game, idle, 130);
    TEST_ASSERT_TRUE(game.editor_state.toast_timer <= 0.0F);

    test_game_teardown(&game);
}

/* S6.8a, D25: remove_item decrements and removes the map entry at 0;
 * give_item increments an existing entry rather than resetting it to 1.
 * "give once, remove once" must clear has_item; "give twice, remove once"
 * must leave it true with count 1 -- the count semantics D25 specifies. */
void test_integration_remove_item_at_zero(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_items));

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    Entity *remover = test_find_entity_by_blueprint(&game.state, "item_remover");
    TEST_ASSERT_NOT_NULL(giver);
    TEST_ASSERT_NOT_NULL(remover);

    (void)walk_player_to(&game, 10.0F, giver->position, 300);
    InputState give_once = {0};
    input_state_press_gp_button(&give_once, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give_once);
    TEST_ASSERT_EQUAL_INT(1, item_count(&game.state.progression.items, "key"));

    (void)walk_player_to(&game, 10.0F, remover->position, 300);
    InputState remove_once = {0};
    input_state_press_gp_button(&remove_once, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, remove_once);
    TEST_ASSERT_FALSE(item_has(&game.state.progression.items, "key"));
    TEST_ASSERT_EQUAL_INT(0, item_count(&game.state.progression.items, "key"));

    (void)walk_player_to(&game, 10.0F, giver->position, 300);
    InputState give_first = {0};
    input_state_press_gp_button(&give_first, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give_first);
    InputState give_second = {0};
    input_state_press_gp_button(&give_second, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give_second);
    TEST_ASSERT_EQUAL_INT(2, item_count(&game.state.progression.items, "key"));

    (void)walk_player_to(&game, 10.0F, remover->position, 300);
    InputState remove_again = {0};
    input_state_press_gp_button(&remove_again, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, remove_again);
    TEST_ASSERT_TRUE(item_has(&game.state.progression.items, "key"));
    TEST_ASSERT_EQUAL_INT(1, item_count(&game.state.progression.items, "key"));

    test_game_teardown(&game);
}

/* S6.8a, D25: items ride progression_arena (like flags/vars) precisely so
 * they survive a level transition's gamedata_arena rewind -- mirrors
 * test_integration_progression_survives_transition. This is the S2.5 ride
 * the map-key-copy exists for: if item_give had stored a bare view into
 * the gamedata-arena-backed ActionNode argument instead of copying it into
 * progression_alloc, the key would dangle after "field" -> "interior" and
 * the interior item_checker's has_item:key lookup would silently miss. */
void test_integration_item_survives_transition(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_item_transition));

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to(&game, 10.0F, giver->position, 300);

    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);
    TEST_ASSERT_TRUE(item_has(&game.state.progression.items, "key"));

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

    /* item_checker's on_spawn rule (conditions = ["has_item:key"]) only
     * sets "key_present" if the item survived -- see the fixture comment. */
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "key_present"));

    test_game_teardown(&game);
}

/* S6.8a, D25: the pause-menu RESTORE action clears ProgressionState
 * wholesale (game_reset_progression zeroes the struct), same as it already
 * does for flags/vars -- mirrors
 * test_integration_progression_restore_clears_progression exactly, with an
 * item instead of a flag. No game_reset_progression code change was needed:
 * `state->progression = (ProgressionState){0}` already zeroes the new
 * `items` field, and a zero-valued map is a valid empty ItemSet per map.h. */
void test_integration_restore_clears_items(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_items));
    game.frame_ctx.restore_fn = test_restore_fn;

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to(&game, 10.0F, giver->position, 300);

    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);
    TEST_ASSERT_TRUE(item_has(&game.state.progression.items, "key"));

    InputState open_input = {0};
    input_state_press_key(&open_input, KEY_F3);
    test_advance_frame(&game, open_input);
    TEST_ASSERT_TRUE(game.menu.open);

    for (int step = 0; step < 2; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESTORE, game.menu.selected);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_FALSE(game.menu.open);

    TEST_ASSERT_FALSE(item_has(&game.state.progression.items, "key"));

    test_game_teardown(&game);
}

/* S6.12b, D25/D34: the pause-menu Inventory entry opens a modal grid
 * overlay of the player's items, freezing the world while open -- mirrors
 * test_integration_dialogue_world_frozen's world-freeze proof (held
 * movement input never moves the player) and the existing menu-navigation
 * tests' real F3/DOWN/ENTER/ESCAPE input-layer drive. */
void test_integration_inventory_screen_open_close_freezes_world(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState open_menu = {0};
    input_state_press_key(&open_menu, KEY_F3);
    test_advance_frame(&game, open_menu);
    TEST_ASSERT_TRUE(game.menu.open);

    /* Walk to INVENTORY: RESUME, SAVE, RESTORE, INVENTORY -- 3 down-presses. */
    for (int step = 0; step < 3; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_INVENTORY, game.menu.selected);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_TRUE(inventory_screen_is_open(&game.inventory));
    TEST_ASSERT_FALSE(game.menu.open);

    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    Vector2 start_position = player->position;

    InputState move_right = {0};
    input_state_hold_key(&move_right, KEY_RIGHT);
    test_advance_frames(&game, move_right, 20);

    TEST_ASSERT_TRUE(inventory_screen_is_open(&game.inventory));
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_FLOAT(start_position.x, player->position.x);
    TEST_ASSERT_EQUAL_FLOAT(start_position.y, player->position.y);

    InputState cancel = {0};
    input_state_press_key(&cancel, KEY_ESCAPE);
    test_advance_frame(&game, cancel);
    TEST_ASSERT_FALSE(inventory_screen_is_open(&game.inventory));
    TEST_ASSERT_TRUE(game.menu.open);

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
    game.editor_state.selected_entity_id = player_entity->id;
    test_set_selected_attr(&game.state, &game.editor_state, player_entity, speed_attr_index);
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

    game.editor_state.selected_entity_id = player_entity->id;
    test_set_selected_attr(&game.state, &game.editor_state, player_entity, speed_attr_index);
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

/* F27 regression guard: selected_entity_index used to be a raw array index,
 * so any compaction of the entity array (e.g. deleting a DIFFERENT entity)
 * silently invalidated it. EditorState now stores the entity's stable id
 * instead, resolved to an index via level_find_entity_by_id at the point of
 * use, so a selection survives structural changes elsewhere in the level.
 *
 * "wagon" has one blueprint child ("lantern", tag "front_light"); "player"
 * is an unrelated second root entity. The scenario drives entirely through
 * the real input layer: F5 into editor mode, CONFIRM to select the nearest
 * root entity (deterministically "wagon" — the editor camera starts at the
 * origin and "wagon" at (50,50) is closer than "player" at (160,120)),
 * NAV_DOWN to enter the tree section onto the lantern child row, then
 * EDITOR_DELETE. Deleting a blueprint child removes the spawned child
 * entity from every instance — a DIFFERENT entity than the selected wagon,
 * which is never itself touched. */
static const char *fixture_child_delete = "[[blueprint]]\n"
                                          "name = \"player\"\n"
                                          "texture = \"player.png\"\n"
                                          "src = [0, 0, 32, 32]\n"
                                          "collision_offset = [0, 0]\n"
                                          "collision_size = [16, 16]\n"
                                          "behavior = \"player\"\n"
                                          "speed = 80\n"
                                          "\n"
                                          "[[blueprint]]\n"
                                          "name = \"lantern\"\n"
                                          "texture = \"lantern.png\"\n"
                                          "src = [0, 0, 8, 8]\n"
                                          "\n"
                                          "[[blueprint]]\n"
                                          "name = \"wagon\"\n"
                                          "texture = \"wagon.png\"\n"
                                          "src = [0, 0, 32, 32]\n"
                                          "\n"
                                          "[[blueprint.child]]\n"
                                          "blueprint = \"lantern\"\n"
                                          "tag = \"front_light\"\n"
                                          "offset = [10, 0]\n"
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
                                          "blueprint = \"wagon\"\n"
                                          "pos = [50, 50]\n";

void test_integration_editor_selection_survives_deleting_different_entity(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_child_delete));
    TEST_ASSERT_EQUAL_INT(1, test_count_entities_by_blueprint(&game.state, "lantern"));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);

    Entity *wagon = test_find_entity_by_blueprint(&game.state, "wagon");
    TEST_ASSERT_NOT_NULL(wagon);
    int wagon_id = wagon->id;
    TEST_ASSERT_EQUAL_INT(wagon_id, game.editor_state.selected_entity_id);

    /* Enter the tree section: wagon has no parent row, so tree index 0
     * is its only child row (the lantern). */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.selected_tree_index);

    InputState delete_input = {0};
    input_state_press_key(&delete_input, KEY_DELETE);
    test_advance_frame(&game, delete_input);

    /* The lantern child is really gone... */
    TEST_ASSERT_EQUAL_INT(0, test_count_entities_by_blueprint(&game.state, "lantern"));
    /* ...but wagon's selection survived the compaction that removed it. */
    TEST_ASSERT_EQUAL_INT(wagon_id, game.editor_state.selected_entity_id);
    int resolved_index =
        level_find_entity_by_id(&game.state.gamedata.current_level, game.editor_state.selected_entity_id);
    TEST_ASSERT_TRUE(resolved_index >= 0);
    TEST_ASSERT_EQUAL_STRING("wagon",
                             game.state.gamedata.current_level.entities.data[resolved_index].blueprint_name.ptr);

    test_game_teardown(&game);
}

/* F27 regression guard (undo path): selection is now keyed by stable entity
 * id, so it must survive an undo that restores different gamedata, as long as
 * the selected entity still exists in the undone state. The scenario drives
 * entirely through the real input layer: F5 into editor, CONFIRM to select
 * the nearest root entity (deterministically "tall_tree" — the editor camera
 * starts at (0,0) and the tree at (50,50) is the closest root), grab + move
 * it right (a real edit that pushes a "Move entity" undo entry), CONFIRM the
 * move, then ACTION_EDITOR_UNDO. Undo restores the pre-move position but the
 * tree entity still exists, so its id-keyed selection must remain intact. */
void test_integration_editor_selection_survives_undo_of_edit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);

    Entity *tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    TEST_ASSERT_NOT_NULL(tree);
    int tree_id = tree->id;
    float original_x = tree->position.x;
    TEST_ASSERT_EQUAL_INT(tree_id, game.editor_state.selected_entity_id);

    /* Grab (KEY_G) enters DRAG on the selected tree. */
    InputState grab_input = {0};
    input_state_press_key(&grab_input, KEY_G);
    test_advance_frame(&game, grab_input);

    /* Hold KEY_RIGHT (AXIS_PRIMARY_X positive) for several frames to move the
     * tree a clearly non-zero distance to the right. */
    for (int step = 0; step < 5; step++) {
        InputState move_input = {0};
        input_state_hold_key(&move_input, KEY_RIGHT);
        test_advance_frame(&game, move_input);
    }

    InputState confirm_move = {0};
    input_state_press_key(&confirm_move, KEY_ENTER);
    test_advance_frame(&game, confirm_move);

    int moved_index = level_find_entity_by_id(&game.state.gamedata.current_level, tree_id);
    TEST_ASSERT_TRUE(moved_index >= 0);
    float moved_x = game.state.gamedata.current_level.entities.data[moved_index].position.x;
    TEST_ASSERT_TRUE_MESSAGE(moved_x > original_x + 1.0F, "grab+move should have shifted the tree right");

    /* Real Ctrl+Z chord: hold Ctrl (level), press Z (edge). */
    InputState undo_input = {0};
    input_state_hold_key(&undo_input, KEY_LEFT_CONTROL);
    input_state_press_key(&undo_input, KEY_Z);
    test_advance_frame(&game, undo_input);

    /* Undo restored the pre-move position (proves it undid a real edit)... */
    int undone_index = level_find_entity_by_id(&game.state.gamedata.current_level, tree_id);
    TEST_ASSERT_TRUE(undone_index >= 0);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, original_x,
                             game.state.gamedata.current_level.entities.data[undone_index].position.x);
    /* ...and the id-keyed selection survived the undo. */
    TEST_ASSERT_EQUAL_INT(tree_id, game.editor_state.selected_entity_id);
    TEST_ASSERT_EQUAL_STRING("tall_tree",
                             game.state.gamedata.current_level.entities.data[undone_index].blueprint_name.ptr);

    test_game_teardown(&game);
}

/* S5.7/D38: multi-select + group move. Drives entirely through the real
 * input layer: F5 into editor, plain CONFIRM selects the nearest root
 * entity to the camera (tall_tree at (50,50), same reasoning as the undo
 * test above) and seeds the multi-selection with it. TestGame exposes
 * editor_camera directly, so the test pans it onto rock's position (200,120)
 * without simulating a multi-second stick hold, then the real
 * multi-select-ADD chord (Ctrl+Enter keyboard, L1+A gamepad) adds rock to
 * the selection. Grab + hold RIGHT drags the whole group; CONFIRM commits.
 * Asserts both entities moved by the same x delta, each resolved by its
 * stable id (editor/core.c's handle_drag_input walks multiselect_ids, never
 * a cached index). */
void test_integration_editor_multiselect_group_move(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);

    Entity *tall_tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    TEST_ASSERT_NOT_NULL(tall_tree);
    int tall_tree_id = tall_tree->id;
    float tall_tree_start_x = tall_tree->position.x;
    TEST_ASSERT_EQUAL_INT(tall_tree_id, game.editor_state.selected_entity_id);
    /* Plain CONFIRM seeds the multi-selection with the freshly picked entity. */
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.multiselect_count);
    TEST_ASSERT_EQUAL_INT(tall_tree_id, game.editor_state.multiselect_ids[0]);

    Entity *rock = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_NOT_NULL(rock);
    int rock_id = rock->id;
    float rock_start_x = rock->position.x;
    /* Pan the camera onto rock so it resolves as nearest for the ADD chord
     * below (find_nearest_entity, editor/draw.c) — same target math the ADD
     * handler itself uses, set directly rather than simulated over frames. */
    game.editor_camera.target = rock->position;

    InputState add_input = {0};
    input_state_hold_key(&add_input, KEY_LEFT_CONTROL);
    input_state_press_key(&add_input, KEY_ENTER);
    test_advance_frame(&game, add_input);

    TEST_ASSERT_EQUAL_INT(2, game.editor_state.multiselect_count);
    bool multiselect_has_tall_tree = false;
    bool multiselect_has_rock = false;
    for (int index = 0; index < game.editor_state.multiselect_count; index++) {
        if (game.editor_state.multiselect_ids[index] == tall_tree_id) {
            multiselect_has_tall_tree = true;
        }
        if (game.editor_state.multiselect_ids[index] == rock_id) {
            multiselect_has_rock = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(multiselect_has_tall_tree, "multiselect should still contain the anchor (tall_tree)");
    TEST_ASSERT_TRUE_MESSAGE(multiselect_has_rock, "multiselect should now also contain rock");
    /* The ADD chord only grows the set — single-select's own anchor is untouched. */
    TEST_ASSERT_EQUAL_INT(tall_tree_id, game.editor_state.selected_entity_id);

    InputState grab_input = {0};
    input_state_press_key(&grab_input, KEY_G);
    test_advance_frame(&game, grab_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_DRAG, game.editor_state.sub_mode);

    for (int step = 0; step < 5; step++) {
        InputState move_input = {0};
        input_state_hold_key(&move_input, KEY_RIGHT);
        test_advance_frame(&game, move_input);
    }

    InputState confirm_move = {0};
    input_state_press_key(&confirm_move, KEY_ENTER);
    test_advance_frame(&game, confirm_move);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    Entity *moved_tall_tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    Entity *moved_rock = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_NOT_NULL(moved_tall_tree);
    TEST_ASSERT_NOT_NULL(moved_rock);

    float tall_tree_delta = moved_tall_tree->position.x - tall_tree_start_x;
    float rock_delta = moved_rock->position.x - rock_start_x;
    TEST_ASSERT_TRUE_MESSAGE(tall_tree_delta > 1.0F, "group move should have shifted tall_tree right");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01F, tall_tree_delta, rock_delta,
                                     "group move should shift both by the same delta");

    test_game_teardown(&game);
}

/* S5.1: WATCH-LIST picker submode. Drives entirely through the real input
 * layer: F5 into editor, CONFIRM selects the nearest root entity
 * (deterministically "tall_tree" — the editor camera starts at (0,0) and the
 * tree at (50,50) is the closest root, same reasoning as the undo test
 * above), the real watch-toggle binding (Left Shift) adds it to the watch
 * list, TAB opens the Tools radial, test_radial_select_item aims the stick
 * at the "Watch list" sector (EDITOR_TOOLS_WATCH_LIST_INDEX) and confirms,
 * one more frame lets BROWSE dispatch the pending radial choice and enter
 * EDITOR_SUB_WATCH_LIST, then a final CONFIRM removes the focused (only)
 * entry. Removing the last watch must also close the picker back to
 * BROWSE. See test_radial_select_item's doc comment (test_helpers.h) for
 * why the stick angle is computed instead of hardcoded. */
void test_integration_editor_watch_list_removes_focused_entry(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);
    Entity *tall_tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    TEST_ASSERT_NOT_NULL(tall_tree);
    TEST_ASSERT_EQUAL_INT(tall_tree->id, game.editor_state.selected_entity_id);

    /* Real watch-toggle binding (Left Shift) adds the selected entity. */
    InputState watch_input = {0};
    input_state_press_key(&watch_input, KEY_LEFT_SHIFT);
    test_advance_frame(&game, watch_input);
    TEST_ASSERT_EQUAL_INT(1, game.watches.count);
    TEST_ASSERT_EQUAL_INT(tall_tree->id, game.watches.watch_ids[0]);

    /* Open the Tools radial (real TAB binding). */
    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOOLS_ITEM_COUNT, game.editor_state.radial_item_count);

    /* Aim the stick at the "Watch list" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_WATCH_LIST_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WATCH_LIST, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(1, game.watches.count);

    /* CONFIRM removes the only (focused) entry, which empties the list and
     * closes the picker back to BROWSE. */
    InputState remove_input = {0};
    input_state_press_key(&remove_input, KEY_ENTER);
    test_advance_frame(&game, remove_input);
    TEST_ASSERT_EQUAL_INT(0, game.watches.count);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    test_game_teardown(&game);
}

/* S5.2a: LEVEL top mode with in-memory switching. fixture_gamedata has two
 * levels: "field" (player, rock, tall_tree — 3 entities) and "cave"
 * (player, rock — 2 entities). Drives entirely through the real input
 * layer: F5 into editor, TAB opens the Tools radial,
 * test_radial_select_item aims the stick at the "Levels" sector
 * (EDITOR_TOOLS_LEVELS_INDEX) and confirms, one more frame lets BROWSE
 * dispatch the pending radial choice and enter EDITOR_TOP_LEVEL. NAV_DOWN
 * moves onto "cave" (the only other_levels entry).
 *
 * A freshly-loaded session is dirty by undo_history_is_dirty's own
 * definition (the "Initial" baseline entry is never undo_history_mark_saved,
 * same as production's real startup path in main.c) — so the first CONFIRM
 * only arms the dirty-check toast and must NOT switch yet; a second CONFIRM
 * commits it. This exercises the dirty-check explicitly rather than
 * incidentally routing around it. Switching lands back in Scene mode so the
 * user sees the newly-active level. The test then repeats the same
 * Tools-radial-plus-double-CONFIRM dance to switch back to "field",
 * asserting the round trip preserved both levels' entities (tall_tree
 * survives being swapped out and back in) and that a subsequent non-editor
 * frame update runs cleanly against the restored level (player resolves,
 * collision tracking arrays are sized correctly for field's 3 entities). */
void test_integration_editor_level_switch_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));
    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(3, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_TRUE(undo_history_is_dirty(&game.undo_history));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    /* Open the Tools radial (real TAB binding). */
    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOOLS_ITEM_COUNT, game.editor_state.radial_item_count);

    /* Aim the stick at the "Levels" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_LEVELS_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.level_list_scroll);

    /* Move the cursor onto "cave" (the only other_levels entry). */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.level_list_scroll);

    /* First CONFIRM: dirty-check arms the pending flag, no switch yet. */
    InputState confirm_switch = {0};
    input_state_press_key(&confirm_switch, KEY_ENTER);
    test_advance_frame(&game, confirm_switch);
    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_TRUE(game.editor_state.level_switch_confirm_pending);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, game.editor_state.top_mode);

    /* Second CONFIRM: commits the switch. */
    test_advance_frame(&game, confirm_switch);

    TEST_ASSERT_EQUAL_STRING("cave", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(2, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_NOT_NULL(test_find_entity_by_blueprint(&game.state, "rock"));
    TEST_ASSERT_NULL(test_find_entity_by_blueprint(&game.state, "tall_tree"));
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_SCENE, game.editor_state.top_mode);
    TEST_ASSERT_FALSE(game.editor_state.level_switch_confirm_pending);

    /* Switch back to "field" via the same Tools-radial dance. The undo
     * reset after the first switch left history dirty again (a fresh
     * "Switch level" baseline, never marked saved), so this is once again
     * a double-CONFIRM. */
    InputState open_tools_2 = {0};
    input_state_press_key(&open_tools_2, KEY_TAB);
    test_advance_frame(&game, open_tools_2);

    test_radial_select_item(&game, EDITOR_TOOLS_LEVELS_INDEX);
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, game.editor_state.top_mode);

    /* The cursor resets to 0 ("cave", now current) each time Level mode
     * opens; NAV_DOWN moves to slot 1 ("field", the only other_levels
     * entry now that "cave" is active). */
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.level_list_scroll);

    test_advance_frame(&game, confirm_switch); /* first CONFIRM: arm pending */
    TEST_ASSERT_EQUAL_STRING("cave", game.state.gamedata.current_level.name.ptr);
    test_advance_frame(&game, confirm_switch); /* second CONFIRM: commit */

    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(3, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_NOT_NULL(test_find_entity_by_blueprint(&game.state, "tall_tree"));
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_SCENE, game.editor_state.top_mode);

    /* player_index / collision tracking must be valid after the round
     * trip: leave editor mode and run a real frame — game_update must not
     * crash and the player must still resolve and move. */
    InputState editor_off = {0};
    input_state_press_key(&editor_off, KEY_F5);
    test_advance_frame(&game, editor_off);
    TEST_ASSERT_FALSE(game.state.editor_mode);

    Entity *player = game_get_player(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    float start_x = player->position.x;

    InputState move_right = {0};
    input_state_set_gp_axis(&move_right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, move_right, 30);

    player = game_get_player(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_TRUE(player->position.x > start_x);

    test_game_teardown(&game);
}

/* Find a [[level]] table by name in a re-parsed TOML root. Standalone
 * helper (not level.c's file-local find_level_table) for the S5.2b
 * round-trip tests below, which reparse the recording save fake's
 * emitted buffer via the raw tomlc99 API. */
static toml_table_t *test_find_level_table_by_name(toml_array_t *levels, const char *name)
{
    int count = toml_array_nelem(levels);
    for (int index = 0; index < count; index++) {
        toml_table_t *candidate = toml_table_at(levels, index);
        toml_datum_t table_name = toml_string_in(candidate, "name");
        if (!table_name.ok) {
            continue;
        }
        bool match = strcmp(table_name.u.s, name) == 0;
        free(table_name.u.s);
        if (match) {
            return candidate;
        }
    }
    return nullptr;
}

/* Fixture for the copy/paste round-trip test below: same blueprint shapes
 * as fixture_gamedata, plus a custom instance attr ("hp") on rock so the
 * test can verify persisted attrs survive copy/paste, not just position
 * and blueprint. Level is sized to comfortably contain the pasted clones'
 * offset position. */
static const char *fixture_gamedata_copy_paste = "[[blueprint]]\n"
                                                 "name = \"player\"\n"
                                                 "texture = \"player.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "behavior = \"player\"\n"
                                                 "speed = 80\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"rock\"\n"
                                                 "texture = \"rock.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "solid = true\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"tall_tree\"\n"
                                                 "texture = \"tree.png\"\n"
                                                 "src = [0, 0, 32, 48]\n"
                                                 "solid = true\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"field\"\n"
                                                 "size = [640, 480]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"player\"\n"
                                                 "pos = [160, 120]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"rock\"\n"
                                                 "pos = [200, 120]\n"
                                                 "hp = 5\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"tall_tree\"\n"
                                                 "pos = [50, 50]\n";

/* Find the entity with the given stable id — used below to distinguish a
 * pasted clone from the original it was copied from, since both share the
 * same blueprint name. */
static Entity *test_find_entity_by_id(GameState *state, int entity_id)
{
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < level->entities.count; index++) {
        if (level->entities.data[index].id == entity_id) {
            return &level->entities.data[index];
        }
    }
    return nullptr;
}

/* Find the entity with blueprint_name == name and id != exclude_id — the
 * pasted clone, once the original's id is known. */
static Entity *test_find_entity_by_blueprint_excluding_id(GameState *state, const char *name, int exclude_id)
{
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        if (entity->id != exclude_id && strcmp(entity->blueprint_name.ptr, name) == 0) {
            return entity;
        }
    }
    return nullptr;
}

/* S5.7/D38: copy/paste. Builds the same tall_tree+rock multi-selection as
 * the group-move test above (select tall_tree, pan onto rock, ADD chord),
 * copies (Ctrl+C), pans the camera to a fresh anchor away from the
 * originals, then pastes (Ctrl+V). Asserts: the originals are untouched;
 * two new clones exist at the new anchor plus each entry's offset from the
 * first copied entity (preserving the copied group's relative layout);
 * rock's persisted "hp" attr rode along onto its clone; the pasted clones
 * become the new multi-selection. Finishes with a real pause-menu Save and
 * reparses the emitted TOML to confirm the clones round-trip (blueprint,
 * pos, and the "hp" attr all present in the saved file). */
void test_integration_editor_copy_paste(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata_copy_paste));
    game.frame_ctx.save_fn = test_recording_gamedata_save;

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);

    Entity *tall_tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    TEST_ASSERT_NOT_NULL(tall_tree);
    int tall_tree_id = tall_tree->id;
    Vector2 tall_tree_original_pos = tall_tree->position;

    Entity *rock = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_NOT_NULL(rock);
    int rock_id = rock->id;
    Vector2 rock_original_pos = rock->position;
    game.editor_camera.target = rock->position;

    InputState add_input = {0};
    input_state_hold_key(&add_input, KEY_LEFT_CONTROL);
    input_state_press_key(&add_input, KEY_ENTER);
    test_advance_frame(&game, add_input);
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.multiselect_count);

    /* Copy: Ctrl+C (keyboard), L1+Y (gamepad). */
    InputState copy_input = {0};
    input_state_hold_key(&copy_input, KEY_LEFT_CONTROL);
    input_state_press_key(&copy_input, KEY_C);
    test_advance_frame(&game, copy_input);
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.copy_buffer_count);

    /* Pan to a fresh anchor, well away from either original, before pasting. */
    Vector2 paste_anchor = {400.0F, 300.0F};
    game.editor_camera.target = paste_anchor;

    /* Paste: Ctrl+V (keyboard), L1+X (gamepad). */
    InputState paste_input = {0};
    input_state_hold_key(&paste_input, KEY_LEFT_CONTROL);
    input_state_press_key(&paste_input, KEY_V);
    test_advance_frame(&game, paste_input);

    /* Originals are untouched. */
    Entity *still_tall_tree = test_find_entity_by_id(&game.state, tall_tree_id);
    TEST_ASSERT_NOT_NULL(still_tall_tree);
    TEST_ASSERT_EQUAL_FLOAT(tall_tree_original_pos.x, still_tall_tree->position.x);
    TEST_ASSERT_EQUAL_FLOAT(tall_tree_original_pos.y, still_tall_tree->position.y);
    Entity *still_rock = test_find_entity_by_id(&game.state, rock_id);
    TEST_ASSERT_NOT_NULL(still_rock);
    TEST_ASSERT_EQUAL_FLOAT(rock_original_pos.x, still_rock->position.x);
    TEST_ASSERT_EQUAL_FLOAT(rock_original_pos.y, still_rock->position.y);

    /* Two clones exist, at the new anchor plus their offset from tall_tree
     * (the first copied entity, so its own relative offset is zero). */
    TEST_ASSERT_EQUAL_INT(2, test_count_entities_by_blueprint(&game.state, "tall_tree"));
    TEST_ASSERT_EQUAL_INT(2, test_count_entities_by_blueprint(&game.state, "rock"));

    Entity *clone_tall_tree = test_find_entity_by_blueprint_excluding_id(&game.state, "tall_tree", tall_tree_id);
    TEST_ASSERT_NOT_NULL(clone_tall_tree);
    TEST_ASSERT_EQUAL_FLOAT(paste_anchor.x, clone_tall_tree->position.x);
    TEST_ASSERT_EQUAL_FLOAT(paste_anchor.y, clone_tall_tree->position.y);

    Entity *clone_rock = test_find_entity_by_blueprint_excluding_id(&game.state, "rock", rock_id);
    TEST_ASSERT_NOT_NULL(clone_rock);
    float expected_clone_rock_x = paste_anchor.x + (rock_original_pos.x - tall_tree_original_pos.x);
    float expected_clone_rock_y = paste_anchor.y + (rock_original_pos.y - tall_tree_original_pos.y);
    TEST_ASSERT_EQUAL_FLOAT(expected_clone_rock_x, clone_rock->position.x);
    TEST_ASSERT_EQUAL_FLOAT(expected_clone_rock_y, clone_rock->position.y);
    /* The persisted "hp" attr rode along from the copied original. */
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&clone_rock->persisted_attrs, "hp", -1));

    /* Paste selects the clones. */
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.multiselect_count);
    bool multiselect_has_clone_tall_tree = false;
    bool multiselect_has_clone_rock = false;
    for (int index = 0; index < game.editor_state.multiselect_count; index++) {
        if (game.editor_state.multiselect_ids[index] == clone_tall_tree->id) {
            multiselect_has_clone_tall_tree = true;
        }
        if (game.editor_state.multiselect_ids[index] == clone_rock->id) {
            multiselect_has_clone_rock = true;
        }
    }
    TEST_ASSERT_TRUE(multiselect_has_clone_tall_tree);
    TEST_ASSERT_TRUE(multiselect_has_clone_rock);

    /* Save through the real pause-menu path (F3 -> DOWN to SAVE -> CONFIRM),
     * then reparse to confirm the clones round-trip. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *levels = toml_array_in(root, "level");
    TEST_ASSERT_NOT_NULL(levels);
    toml_table_t *field_table = test_find_level_table_by_name(levels, "field");
    TEST_ASSERT_NOT_NULL_MESSAGE(field_table, "'field' missing from saved TOML");

    toml_array_t *entities = toml_array_in(field_table, "entity");
    TEST_ASSERT_NOT_NULL(entities);
    int rock_entity_count = 0;
    int tall_tree_entity_count = 0;
    bool found_clone_rock_hp = false;
    int entity_count = toml_array_nelem(entities);
    for (int index = 0; index < entity_count; index++) {
        toml_table_t *entity_table = toml_table_at(entities, index);
        toml_datum_t blueprint = toml_string_in(entity_table, "blueprint");
        TEST_ASSERT_TRUE(blueprint.ok);
        toml_array_t *pos = toml_array_in(entity_table, "pos");
        toml_datum_t pos_x = toml_int_at(pos, 0);
        toml_datum_t pos_y = toml_int_at(pos, 1);
        if (strcmp(blueprint.u.s, "rock") == 0) {
            rock_entity_count++;
            if ((int)pos_x.u.i == (int)expected_clone_rock_x && (int)pos_y.u.i == (int)expected_clone_rock_y) {
                toml_datum_t hp_datum = toml_int_in(entity_table, "hp");
                if (hp_datum.ok && hp_datum.u.i == 5) {
                    found_clone_rock_hp = true;
                }
            }
        } else if (strcmp(blueprint.u.s, "tall_tree") == 0) {
            tall_tree_entity_count++;
        }
        free(blueprint.u.s);
    }
    TEST_ASSERT_EQUAL_INT(2, rock_entity_count);
    TEST_ASSERT_EQUAL_INT(2, tall_tree_entity_count);
    TEST_ASSERT_TRUE_MESSAGE(found_clone_rock_hp, "pasted rock clone should round-trip at its offset with hp=5");

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.2b: "+ NEW LEVEL" creation. Drives the same Tools-radial-into-
 * EDITOR_TOP_LEVEL dance as the switch test above, then walks the list
 * cursor onto the "+ NEW LEVEL" sentinel (index 2 of [field(current),
 * cave, "+ NEW LEVEL"]), opens the word builder via CONFIRM, and picks
 * the first builtin vocabulary word ("chest", word_builder_scroll 1 —
 * see word_builder_builtin in editor/widgets.c) as the new level's
 * name: NAV_DOWN to scroll it into view, CONFIRM to append it, NAV_UP
 * back to scroll 0 ("[ DONE ]"), CONFIRM to finalize. create_new_level
 * (editor/level.c) then builds the level with the S5.2b defaults and
 * activates it via level_activate, landing back in Scene mode.
 *
 * The test then wires the recording gamedata-save fake (not part of
 * TestGame's default setup — see test_recording_gamedata_save's doc
 * comment) and drives a real menu Save (F3 -> DOWN to SAVE -> CONFIRM),
 * then reparses the emitted TOML to confirm the new level's name and
 * default 640x360 size survived the round trip. */
void test_integration_editor_level_create_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));
    game.frame_ctx.save_fn = test_recording_gamedata_save;

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);

    /* Aim the stick at the "Levels" sector and confirm — same helper call
     * as test_integration_editor_level_switch_round_trip above. */
    test_radial_select_item(&game, EDITOR_TOOLS_LEVELS_INDEX);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, game.editor_state.top_mode);

    InputState down = {0};
    input_state_press_key(&down, KEY_DOWN);
    test_advance_frame(&game, down); /* scroll 1: "cave" */
    test_advance_frame(&game, down); /* scroll 2: "+ NEW LEVEL" sentinel */
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.level_list_scroll);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_TRUE(game.editor_state.creating_level);

    InputState wb_down = {0};
    input_state_press_key(&wb_down, KEY_DOWN);
    test_advance_frame(&game, wb_down); /* word_builder_scroll 1: "chest" */
    test_advance_frame(&game, confirm); /* append "chest" to the buffer */

    InputState wb_up = {0};
    input_state_press_key(&wb_up, KEY_UP);
    test_advance_frame(&game, wb_up);   /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm); /* finalize: create_new_level(..., "chest") */

    TEST_ASSERT_EQUAL_STRING("chest", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(640, game.state.gamedata.current_level.width);
    TEST_ASSERT_EQUAL_INT(360, game.state.gamedata.current_level.height);
    TEST_ASSERT_EQUAL_INT(640, game.state.gamedata.current_level.floor_width);
    TEST_ASSERT_EQUAL_INT(360, game.state.gamedata.current_level.floor_height);
    TEST_ASSERT_EQUAL_INT(0, game.state.gamedata.current_level.entities.count);
    TEST_ASSERT_EQUAL_STRING("grass.png", game.state.gamedata.current_level.background_tile.ptr);
    TEST_ASSERT_EQUAL_INT(2, game.state.gamedata.other_levels.count); /* "field" + "cave" */
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_SCENE, game.editor_state.top_mode);

    /* Save through the real pause-menu path. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    TEST_ASSERT_TRUE(game.menu.open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *levels = toml_array_in(root, "level");
    TEST_ASSERT_NOT_NULL(levels);
    toml_table_t *chest_table = test_find_level_table_by_name(levels, "chest");
    TEST_ASSERT_NOT_NULL_MESSAGE(chest_table, "new level 'chest' missing from saved TOML");

    toml_array_t *size = toml_array_in(chest_table, "size");
    TEST_ASSERT_NOT_NULL(size);
    TEST_ASSERT_EQUAL_INT(640, (int)toml_int_at(size, 0).u.i);
    TEST_ASSERT_EQUAL_INT(360, (int)toml_int_at(size, 1).u.i);

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.2b: level-detail-row editing. Opens Level mode and CONFIRMs row 0
 * (the current level, "field") to enter the detail view instead of the
 * former switch-to-self no-op. Bumps width by +10 via ACTION_ATTR_INC_10
 * (KEY_RIGHT_BRACKET) while the WIDTH row is focused — no separate
 * "enter edit mode" step, the press applies and commits immediately
 * (see apply_level_detail_delta, editor/level.c). Then navigates to the
 * BACKGROUND_TILE row, CONFIRMs to open the word builder pre-filled with
 * the parse-time default "grass.png", clears it (CANCEL pops the whole
 * buffer since it has no underscore word boundary), and picks "chest"
 * the same way the create-round-trip test does. Saves via the real menu
 * path and reparses to confirm both edits survived. */
void test_integration_editor_level_edit_detail_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));
    game.frame_ctx.save_fn = test_recording_gamedata_save;
    TEST_ASSERT_EQUAL_INT(320, game.state.gamedata.current_level.width);

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);

    test_radial_select_item(&game, EDITOR_TOOLS_LEVELS_INDEX);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.level_list_scroll);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_TRUE(game.editor_state.level_detail_open);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.level_detail_row); /* WIDTH */

    InputState inc_10 = {0};
    input_state_press_key(&inc_10, KEY_RIGHT_BRACKET);
    test_advance_frame(&game, inc_10);
    TEST_ASSERT_EQUAL_INT(330, game.state.gamedata.current_level.width);

    /* Navigate WIDTH -> HEIGHT -> FLOOR_WIDTH -> FLOOR_HEIGHT -> BACKGROUND_TILE. */
    InputState down = {0};
    input_state_press_key(&down, KEY_DOWN);
    for (int step = 0; step < 4; step++) {
        test_advance_frame(&game, down);
    }
    TEST_ASSERT_EQUAL_INT(4, game.editor_state.level_detail_row);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(LEVEL_STRING_FIELD_BACKGROUND_TILE, game.editor_state.editing_level_string_field);
    TEST_ASSERT_EQUAL_STRING("grass.png", game.editor_state.word_builder_buf);

    InputState cancel = {0};
    input_state_press_key(&cancel, KEY_ESCAPE);
    test_advance_frame(&game, cancel); /* pop clears "grass.png" (no underscore) */
    TEST_ASSERT_EQUAL_STRING("", game.editor_state.word_builder_buf);

    InputState wb_down = {0};
    input_state_press_key(&wb_down, KEY_DOWN);
    test_advance_frame(&game, wb_down); /* word_builder_scroll 1: "chest" */
    test_advance_frame(&game, confirm); /* append "chest" */

    InputState wb_up = {0};
    input_state_press_key(&wb_up, KEY_UP);
    test_advance_frame(&game, wb_up);   /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm); /* finalize: confirm_level_string_edit */

    TEST_ASSERT_EQUAL_STRING("chest", game.state.gamedata.current_level.background_tile.ptr);
    TEST_ASSERT_EQUAL_INT(LEVEL_STRING_FIELD_NONE, game.editor_state.editing_level_string_field);
    TEST_ASSERT_TRUE(game.editor_state.level_detail_open); /* still in the detail view */

    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *levels = toml_array_in(root, "level");
    TEST_ASSERT_NOT_NULL(levels);
    toml_table_t *field_table = test_find_level_table_by_name(levels, "field");
    TEST_ASSERT_NOT_NULL_MESSAGE(field_table, "'field' missing from saved TOML");

    toml_array_t *size = toml_array_in(field_table, "size");
    TEST_ASSERT_NOT_NULL(size);
    TEST_ASSERT_EQUAL_INT(330, (int)toml_int_at(size, 0).u.i);
    TEST_ASSERT_EQUAL_INT(240, (int)toml_int_at(size, 1).u.i);

    toml_datum_t background_tile = toml_string_in(field_table, "background_tile");
    TEST_ASSERT_TRUE(background_tile.ok);
    TEST_ASSERT_EQUAL_STRING("chest", background_tile.u.s);
    free(background_tile.u.s);

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.3b: fixture with a tileset for the Tile-mode paint round-trip test
 * below. "field" is 32x32, a small 2x2 tile grid at TILE_SIZE=16 — big
 * enough to move the cursor off (0,0) but small enough to keep the input
 * sequence short. */
static const char *fixture_gamedata_tileset = "[[blueprint]]\n"
                                              "name = \"player\"\n"
                                              "texture = \"player.png\"\n"
                                              "src = [0, 0, 32, 32]\n"
                                              "collision_offset = [-5, 6]\n"
                                              "collision_size = [10, 10]\n"
                                              "behavior = \"player\"\n"
                                              "speed = 80\n"
                                              "\n"
                                              "[[tileset]]\n"
                                              "id = 1\n"
                                              "texture = \"grass.png\"\n"
                                              "src = [0, 0, 16, 16]\n"
                                              "\n"
                                              "[[tileset]]\n"
                                              "id = 2\n"
                                              "texture = \"floor.png\"\n"
                                              "src = [0, 0, 16, 16]\n"
                                              "\n"
                                              "[[level]]\n"
                                              "name = \"field\"\n"
                                              "size = [32, 32]\n"
                                              "\n"
                                              "[[level.entity]]\n"
                                              "blueprint = \"player\"\n"
                                              "pos = [16, 16]\n";

/* S5.3b: TILE mode paint round trip. Drives entirely through the real
 * input layer: F5 into editor, TAB opens the Tools radial,
 * test_radial_select_item aims the stick at the "Tiles" sector
 * (EDITOR_TOOLS_TILE_INDEX) and confirms, one more frame lets BROWSE
 * dispatch the pending radial choice and enter EDITOR_TOP_TILE /
 * EDITOR_SUB_TILE_PAINT with the cursor at (0, 0). Opens the palette (real
 * P / ACTION_EDITOR_PLACE binding), NAV_DOWN focuses tile id 2
 * ("floor.png", the second tileset entry) and CONFIRM adopts it as the
 * paint tile. NAV_RIGHT + NAV_DOWN move the cursor to (1, 1); CONFIRM
 * paints. Asserts the ground layer cell at (1, 1) holds tile id 2 while
 * the untouched (0, 0) cell stays 0 (empty). Saves through the real
 * pause-menu path (wiring the recording gamedata-save fake, same as the
 * Level-mode round-trip tests above) and reparses the emitted TOML to
 * confirm the painted cell survives as a [[tiles_ground]] row-array
 * entry. */
void test_integration_editor_tile_paint_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata_tileset));
    game.frame_ctx.save_fn = test_recording_gamedata_save;
    TEST_ASSERT_EQUAL_INT(2, game.state.gamedata.current_level.tiles_wide);
    TEST_ASSERT_EQUAL_INT(2, game.state.gamedata.current_level.tiles_high);

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    /* Aim the stick at the "Tiles" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_TILE_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_TILE, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_TILE_PAINT, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.tile_cursor_col);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.tile_cursor_row);

    /* Open the palette (real P / EDITOR_PLACE binding), focus tile id 2. */
    InputState open_palette = {0};
    input_state_press_key(&open_palette, KEY_P);
    test_advance_frame(&game, open_palette);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_TILE_PALETTE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.tile_palette_scroll);

    InputState palette_down = {0};
    input_state_press_key(&palette_down, KEY_DOWN);
    test_advance_frame(&game, palette_down);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.tile_palette_scroll);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_TILE_PAINT, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.selected_tile_id);

    /* Move the cursor to (1, 1) and paint. */
    InputState nav_right = {0};
    input_state_press_key(&nav_right, KEY_RIGHT);
    test_advance_frame(&game, nav_right);
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.tile_cursor_col);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.tile_cursor_row);

    test_advance_frame(&game, confirm);

    const Level *level = &game.state.gamedata.current_level;
    TEST_ASSERT_EQUAL_INT(4, level->tiles_ground.count);
    TEST_ASSERT_EQUAL_INT(2, level->tiles_ground.data[level_tile_index(1, 1, level->tiles_wide)]);
    TEST_ASSERT_EQUAL_INT(0, level->tiles_ground.data[level_tile_index(0, 0, level->tiles_wide)]);

    /* Save through the real pause-menu path (F3 -> DOWN to SAVE -> CONFIRM). */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *levels = toml_array_in(root, "level");
    TEST_ASSERT_NOT_NULL(levels);
    toml_table_t *field_table = test_find_level_table_by_name(levels, "field");
    TEST_ASSERT_NOT_NULL_MESSAGE(field_table, "'field' missing from saved TOML");

    toml_array_t *tiles_ground = toml_array_in(field_table, "tiles_ground");
    TEST_ASSERT_NOT_NULL_MESSAGE(tiles_ground, "tiles_ground missing from saved TOML");
    toml_array_t *painted_row = toml_array_at(tiles_ground, 1);
    TEST_ASSERT_NOT_NULL(painted_row);
    TEST_ASSERT_EQUAL_INT(2, (int)toml_int_at(painted_row, 1).u.i);

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.4b/D37: Atlas mode's texture picker browses state->assets.textures --
 * the runtime registry main.c populates from embedded PNGs at startup.
 * main.c isn't linked into the test binary (see test_dummy_texture_lookup
 * above), so the registry stays empty after test_game_setup. Seed one
 * entry directly, the same shape main.c's texture_registry_add builds (a
 * TextureEntry with a filename and a Texture2D), so the round-trip test
 * below has a texture to pick. The Texture2D is never sampled (headless,
 * no GL context) -- only its width/height feed the region-src clamp math,
 * so a plausible size is set without ever calling LoadTexture. */
static void test_seed_atlas_texture(GameState *state, const char *filename, int width, int height)
{
    state->assets.textures.alloc = allocator_arena(&state->gamedata_arena);
    TextureEntry entry = {0};
    (void)snprintf(entry.filename, sizeof(entry.filename), "%s", filename);
    entry.texture = (Texture2D){.id = 1, .width = width, .height = height, .mipmaps = 1, .format = 1};
    TEST_ASSERT_TRUE(vec_texture_entry_push(&state->assets.textures, entry));
}

/* S5.4b/D37: Atlas mode region create + drag-to-src + commit + undo +
 * save/reparse round trip. Drives entirely through the real input layer:
 * F5 into editor, TAB opens the Tools radial, test_radial_select_item aims
 * the stick at the "Atlas" sector (EDITOR_TOOLS_ATLAS_INDEX) and confirms,
 * one more frame lets BROWSE dispatch the pending radial choice into
 * EDITOR_TOP_ATLAS / EDITOR_SUB_ATLAS_BROWSE with the texture picker showing
 * (atlas_texture_index == -1). CONFIRM picks the one seeded texture; CONFIRM
 * again on the (empty) region list lands on its "+ NEW REGION" sentinel and
 * opens the word builder; picking the builtin word "chest" then "[ DONE ]"
 * names the region and enters EDITOR_SUB_ATLAS_REGION_EDIT.
 *
 * The src rect is set via the same dual-stick drag EDITOR_SUB_HANDLES uses
 * for collision boxes (left stick: offset, right stick: size, both real
 * ACTION/AXIS bindings) -- first a modest move on each of the four
 * components, then an extreme left-stick push to drive x into the 64x64
 * texture's clamp (x clamps to 63, which forces width down to 1), proving
 * the ">= 1 size, clamped to texture bounds" invariant. CONFIRM commits into
 * gamedata.atlas_regions and pushes an undo entry.
 *
 * Saves through the real pause-menu path (wiring the recording
 * gamedata-save fake) and reparses the emitted TOML via the production
 * atlas_load/atlas_find_region to confirm the region survives as a
 * [[atlas.region]] entry with the same name/texture/src. */
void test_integration_editor_atlas_region_create_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));
    game.frame_ctx.save_fn = test_recording_gamedata_save;
    test_seed_atlas_texture(&game.state, "sprites.png", 64, 64);
    TEST_ASSERT_EQUAL_INT(0, game.state.gamedata.atlas_regions.count);

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    /* Aim the stick at the "Atlas" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_ATLAS_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_ATLAS, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ATLAS_BROWSE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.atlas_texture_index);

    /* Pick the one seeded texture (real Enter / ACTION_CONFIRM binding). */
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.atlas_texture_index);

    /* Region list for "sprites.png" is empty -- scroll 0 is already the
     * "+ NEW REGION" sentinel. Confirm opens the word builder. */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);

    /* Down to the builtin word "chest" (word_builder_builtin[0]), confirm to
     * append it, back up to "[ DONE ]" (scroll 0), confirm to commit the name. */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_STRING("chest", game.editor_state.word_builder_buf);

    InputState nav_up = {0};
    input_state_press_key(&nav_up, KEY_UP);
    test_advance_frame(&game, nav_up);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ATLAS_REGION_EDIT, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.atlas_region_index);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, game.editor_state.atlas_edit_src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 32.0F, game.editor_state.atlas_edit_src.width);

    /* Drag the src rect: left stick moves the offset, right stick grows the
     * size, one axis at a time (same HANDLES-style dual-stick math, reused
     * verbatim -- see handle_atlas_region_edit_input). 5 frames at
     * EDITOR_HANDLE_SPEED (60 px/s) and 1/60s per frame moves ~1px/frame. */
    InputState left_x = {0};
    input_state_set_gp_axis(&left_x, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, left_x, 5);
    InputState left_y = {0};
    input_state_set_gp_axis(&left_y, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    test_advance_frames(&game, left_y, 5);
    InputState right_x = {0};
    input_state_set_gp_axis(&right_x, GAMEPAD_AXIS_RIGHT_X, 1.0F);
    test_advance_frames(&game, right_x, 5);
    InputState right_y = {0};
    input_state_set_gp_axis(&right_y, GAMEPAD_AXIS_RIGHT_Y, 1.0F);
    test_advance_frames(&game, right_y, 5);

    TEST_ASSERT_FLOAT_WITHIN(0.5F, 5.0F, game.editor_state.atlas_edit_src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 5.0F, game.editor_state.atlas_edit_src.y);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 37.0F, game.editor_state.atlas_edit_src.width);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 37.0F, game.editor_state.atlas_edit_src.height);

    /* Push x hard against the texture's right edge: clamp holds x at
     * texture_width - 1 (63) and shrinks width to fit (1px) -- the ">= 1
     * size, clamped to texture bounds" invariant (S5.4b deliverable 1). */
    test_advance_frames(&game, left_x, 200);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 63.0F, game.editor_state.atlas_edit_src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.0F, game.editor_state.atlas_edit_src.width);

    Rectangle expected_src = game.editor_state.atlas_edit_src;

    /* Commit: CONFIRM creates the AtlasRegion and pushes undo. */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ATLAS_BROWSE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(1, game.state.gamedata.atlas_regions.count);
    const AtlasRegion *region = &game.state.gamedata.atlas_regions.data[0];
    TEST_ASSERT_EQUAL_STRING("chest", region->name.ptr);
    TEST_ASSERT_EQUAL_STRING("sprites.png", region->texture.ptr);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, expected_src.x, region->src.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, expected_src.y, region->src.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, expected_src.width, region->src.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, expected_src.height, region->src.height);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Create atlas region"));

    /* Save through the real pause-menu path (F3 -> DOWN to SAVE -> CONFIRM). */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    /* Reparse via the production atlas_load/atlas_find_region and confirm
     * the region survived as a [[atlas.region]] entry. Truncate the
     * expected src the same way the TOML emitter does ((int) cast) so the
     * comparison is exact regardless of the stick-drive's float noise. */
    ErrorState reparse_err = {0};
    Arena arena2;
    TEST_ASSERT_TRUE(arena_init(&reparse_err, &arena2));
    vec_atlas_region regions2 = {0};
    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);
    TEST_ASSERT_TRUE(atlas_load(&game.diag, &regions2, root, &arena2) >= 0);
    toml_free(root);

    const AtlasRegion *region2 = atlas_find_region(&regions2, "chest");
    TEST_ASSERT_NOT_NULL(region2);
    TEST_ASSERT_EQUAL_STRING("sprites.png", region2->texture.ptr);
    TEST_ASSERT_EQUAL_INT((int)expected_src.x, (int)region2->src.x);
    TEST_ASSERT_EQUAL_INT((int)expected_src.y, (int)region2->src.y);
    TEST_ASSERT_EQUAL_INT((int)expected_src.width, (int)region2->src.width);
    TEST_ASSERT_EQUAL_INT((int)expected_src.height, (int)region2->src.height);

    arena_free(&arena2);
    test_game_teardown(&game);
}

/* Find a [[blueprint]] table by name in a re-parsed TOML root. Standalone
 * helper (mirrors test_find_level_table_by_name above) for the S5.5
 * round-trip test below. */
static toml_table_t *test_find_blueprint_table_by_name(toml_array_t *blueprints, const char *name)
{
    int count = toml_array_nelem(blueprints);
    for (int index = 0; index < count; index++) {
        toml_table_t *candidate = toml_table_at(blueprints, index);
        toml_datum_t table_name = toml_string_in(candidate, "name");
        if (!table_name.ok) {
            continue;
        }
        bool match = strcmp(table_name.u.s, name) == 0;
        free(table_name.u.s);
        if (match) {
            return candidate;
        }
    }
    return nullptr;
}

/* S5.5/D20: Animation mode edit + frame-scrub + save/reparse round trip.
 * Drives entirely through the real input layer: F5 into editor, TAB opens
 * the Tools radial, test_radial_select_item aims the stick at the
 * "Animation" sector (EDITOR_TOOLS_ANIM_INDEX) and confirms, one more frame
 * lets BROWSE dispatch the pending radial choice into EDITOR_TOP_ANIM /
 * EDITOR_SUB_ANIM_EDIT. No scene entity is selected at that point (fresh
 * test_game_setup leaves selected_entity_id == -1), so enter_anim_mode
 * cannot preselect a blueprint and anim_blueprint_index lands on -1 -- the
 * blueprint list picker (EDITOR_SUB_ANIM_EDIT's dual duty, editor/anim.c).
 * CONFIRM on scroll 0 ("player", first blueprint in fixture_gamedata)
 * enters the params-edit rows.
 *
 * "player" has no anim_* attrs yet (S3.4 plumbing only touches blueprints
 * that author `animation = {...}`), so the FRAMES row reads the S5.5
 * default (1) until bumped. ACTION_ATTR_INC_1 (real KEY_RIGHT binding)
 * bumps it to 2, creating the anim_frames attr and pushing an undo entry.
 * TAB then switches to EDITOR_SUB_ANIM_FRAMES (frame scrubber); NAV_RIGHT
 * (real KEY_RIGHT/ACTION_NAV_RIGHT binding) is pressed 5 times to drive
 * anim_frame_index past the [0, anim_frames) bound (frames == 2), proving
 * the clamp holds at index 1.
 *
 * Saves through the real pause-menu path (wiring the recording
 * gamedata-save fake) and reparses the emitted TOML to confirm the bumped
 * anim_frames survives inside the player blueprint's `animation = {...}`
 * sugar (S3.4's round-trip, now driven by an editor session instead of a
 * hand-authored fixture). */
void test_integration_editor_animation_edit_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));
    game.frame_ctx.save_fn = test_recording_gamedata_save;
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.selected_entity_id);

    const Blueprint *player_blueprint = blueprint_find(&game.state.gamedata.blueprints, "player");
    TEST_ASSERT_NOT_NULL(player_blueprint);
    TEST_ASSERT_EQUAL_INT(-1, attr_get_int(&player_blueprint->attrs, "anim_frames", -1));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    /* Aim the stick at the "Animation" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_ANIM_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_ANIM, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ANIM_EDIT, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.anim_blueprint_index);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.anim_blueprint_scroll);

    /* Pick "player" (scroll 0, the first blueprint in fixture_gamedata). */
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.anim_blueprint_index);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.anim_edit_row); /* FRAMES row focused */

    /* Bump FRAMES from the unset-attr default (1) to 2. */
    InputState inc_1 = {0};
    input_state_press_key(&inc_1, KEY_RIGHT);
    test_advance_frame(&game, inc_1);
    TEST_ASSERT_EQUAL_INT(2, attr_get_int(&player_blueprint->attrs, "anim_frames", -1));
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Edit animation"));

    /* TAB (real ACTION_TAB_NEXT binding) switches to the frame scrubber. */
    InputState tab = {0};
    input_state_press_key(&tab, KEY_TAB);
    test_advance_frame(&game, tab);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ANIM_FRAMES, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.anim_frame_index);

    /* Drive NAV_RIGHT past anim_frames (2): index must clamp to 1. */
    InputState nav_right = {0};
    input_state_press_key(&nav_right, KEY_RIGHT);
    test_advance_frames(&game, nav_right, 5);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.anim_frame_index);

    /* Save through the real pause-menu path (F3 -> DOWN to SAVE -> CONFIRM),
     * driven straight from EDITOR_SUB_ANIM_FRAMES: ACTION_MENU_TOGGLE is
     * read unconditionally in frame_update, ahead of any editor sub_mode
     * dispatch, so no need to back out of Animation mode first. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *blueprints = toml_array_in(root, "blueprint");
    TEST_ASSERT_NOT_NULL(blueprints);
    toml_table_t *player_table = test_find_blueprint_table_by_name(blueprints, "player");
    TEST_ASSERT_NOT_NULL_MESSAGE(player_table, "'player' missing from saved TOML");

    toml_table_t *animation = toml_table_in(player_table, "animation");
    TEST_ASSERT_NOT_NULL_MESSAGE(animation, "animation sugar missing from saved 'player' blueprint");
    toml_datum_t frames = toml_int_in(animation, "frames");
    TEST_ASSERT_TRUE(frames.ok);
    TEST_ASSERT_EQUAL_INT(2, (int)frames.u.i);

    toml_free(root);
    test_game_teardown(&game);
}

/* Fixture blueprint with a rule whose action tree has depth: a root
 * set_flag, followed by an if/else with two "then" actions and one "else"
 * action (S5.6a rule tree navigation test below). Hand-derived flattened
 * row count: trigger(1) + rule conditions(1) + action_tree.nodes(5:
 * set_flag:used, if_else, destroy, set_flag:opened, set_flag:blocked) = 7
 * rows, indices 0..6. */
static const char *fixture_rule_tree =
    "[[blueprint]]\n"
    "name = \"switch\"\n"
    "texture = \"switch.png\"\n"
    "src = [0, 0, 16, 16]\n"
    "collision_offset = [0, 0]\n"
    "collision_size = [16, 16]\n"
    "\n"
    "[[blueprint.rule]]\n"
    "trigger = \"interact\"\n"
    "conditions = [\"flag:powered\"]\n"
    "actions = [\"set_flag:used\", { if = [\"flag:test_flag\"], then = [\"destroy\", \"set_flag:opened\"], else = "
    "[\"set_flag:blocked\"] }]\n"
    "\n"
    "[[level]]\n"
    "name = \"test\"\n"
    "size = [320, 240]\n"
    "\n"
    "[[level.entity]]\n"
    "blueprint = \"switch\"\n"
    "pos = [100, 100]\n";

/* S5.6a: Rule mode's read-only tree view + navigation. Drives entirely
 * through the real input layer: F5 into editor, TAB opens the Tools radial,
 * test_radial_select_item aims the stick at the "Rules" sector
 * (EDITOR_TOOLS_RULE_INDEX) and confirms, one more frame lets BROWSE
 * dispatch the pending radial choice into EDITOR_TOP_RULE /
 * EDITOR_SUB_RULE_LIST. No scene entity is selected at that point (fresh
 * test_game_setup leaves selected_entity_id == -1), so enter_rule_mode
 * cannot preselect a blueprint and rule_blueprint_index lands on -1 -- the
 * blueprint list picker (EDITOR_SUB_RULE_LIST's dual duty, editor/rule.c).
 * CONFIRM on scroll 0 ("switch", the only blueprint in fixture_rule_tree)
 * opens its rule list; CONFIRM again on its only rule opens the tree.
 *
 * NAV_DOWN driven 10 times (more than the fixture's 7-row tree) must clamp
 * the cursor at row 6, not wrap or overshoot; NAV_UP driven 10 times from
 * there must clamp back at row 0. This indirectly proves the flattened row
 * count matches the fixture's full tree (trigger + condition + all 5 action
 * nodes, including the if/else's nested children and else_children) -- a
 * flatten that forgot to recurse into either branch would clamp at a lower
 * row. CANCEL is then driven three times to prove the full back-out chain:
 * tree -> rule list -> blueprint picker -> scene. */
void test_integration_editor_rule_tree_navigation(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_rule_tree));
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.selected_entity_id);

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    /* Aim the stick at the "Rules" sector and confirm in the same frame. */
    test_radial_select_item(&game, EDITOR_TOOLS_RULE_INDEX);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_RULE, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_LIST, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.rule_blueprint_index);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_blueprint_scroll);

    /* Pick "switch" (scroll 0, the only blueprint in the fixture). */
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_blueprint_index);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_list_scroll);

    /* Open the blueprint's only rule. */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_tree_row);

    /* Overshoot NAV_DOWN past the 7-row tree: must clamp at row 6. */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frames(&game, nav_down, 10);
    TEST_ASSERT_EQUAL_INT(6, game.editor_state.rule_tree_row);

    /* Overshoot NAV_UP back past row 0: must clamp at 0. */
    InputState nav_up = {0};
    input_state_press_key(&nav_up, KEY_UP);
    test_advance_frames(&game, nav_up, 10);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_tree_row);

    /* CANCEL walks back out: tree -> rule list -> blueprint picker -> scene. */
    InputState cancel = {0};
    input_state_press_key(&cancel, KEY_ESCAPE);
    test_advance_frame(&game, cancel);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_LIST, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_blueprint_index);

    test_advance_frame(&game, cancel);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.rule_blueprint_index);

    test_advance_frame(&game, cancel);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_SCENE, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    test_game_teardown(&game);
}

/* Local helper: mirrors test_radial_select_item (test_helpers.c) but reads
 * the item count from the currently-open radial's own
 * editor_state.radial_item_count instead of the fixed
 * EDITOR_TOOLS_ITEM_COUNT that helper targets -- Rule mode's leaf-editing
 * radials (S5.6b) come in three different sizes (trigger/condition/action
 * type counts), all opened via the same EDITOR_SUB_RADIAL submode. */
static void test_select_rule_radial_item(TestGame *game, int item_index)
{
    float sector_deg = RADIAL_FULL_CIRCLE_DEG / (float)game->editor_state.radial_item_count;
    float mid_deg = (((float)item_index + 0.5F) * sector_deg) - RADIAL_NORTH_OFFSET_DEG;
    float mid_rad = mid_deg * RADIAL_DEG_TO_RAD;
    InputState radial_confirm = {0};
    input_state_set_gp_axis(&radial_confirm, GAMEPAD_AXIS_LEFT_X, cosf(mid_rad));
    input_state_set_gp_axis(&radial_confirm, GAMEPAD_AXIS_LEFT_Y, sinf(mid_rad));
    input_state_press_key(&radial_confirm, KEY_ENTER);
    test_advance_frame(game, radial_confirm);
}

/* S5.6b: Rule mode leaf editing. Reuses fixture_rule_tree (S5.6a test
 * above): blueprint "switch"'s only rule has a top-level `set_flag:used`
 * action at flattened row 2 (trigger=row0, condition=row1, actions start
 * at row2). Opens Rule mode and focuses row 2 the same way the navigation
 * test does, CONFIRMs to open the ACTION_TYPE radial, picks ACTION_SET_FLAG
 * (same type, index 0, since ActionType's non-control-flow values map
 * directly onto the radial index -- see RULE_ACTION_TYPE_COUNT's doc
 * comment, editor/internal.h) to reach the argument step, clears the
 * prefilled "used" text via the word builder and types "chest" instead
 * (same CANCEL-then-append idiom
 * test_integration_editor_level_edit_detail_round_trip uses), then
 * confirms to commit.
 *
 * Then re-opens the same row's edit, picks a DIFFERENT type
 * (ACTION_CLEAR_FLAG) via the radial, and CANCELs from the follow-up fuzzy
 * finder step instead of finishing it -- proving the staged type change is
 * discarded and the node is left exactly as the first edit committed it.
 * This is the load-bearing assertion for S5.6b's "CANCEL at any point in a
 * multi-step gesture mutates nothing", since a bug that committed on
 * radial-confirm instead of on the gesture's last step would leave the
 * node's type changed here even though CANCEL was the last thing pressed.
 *
 * Finally saves through the real pause-menu path and reparses the emitted
 * TOML to confirm "set_flag:chest" round-tripped as the blueprint's first
 * action. */
void test_integration_editor_rule_leaf_edit_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_rule_tree));
    game.frame_ctx.save_fn = test_recording_gamedata_save;

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    test_radial_select_item(&game, EDITOR_TOOLS_RULE_INDEX);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_RULE, game.editor_state.top_mode);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm); /* pick "switch" */
    test_advance_frame(&game, confirm); /* open its only rule */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);

    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frames(&game, nav_down, 2); /* row 2: set_flag:used */
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.rule_tree_row);

    /* CONFIRM opens the ACTION_TYPE radial. */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RADIAL_CTX_ACTION_TYPE, game.editor_state.radial_context);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_TYPE, game.editor_state.rule_edit_field);

    /* Pick ACTION_SET_FLAG (same type the row already has). */
    test_select_rule_radial_item(&game, ACTION_SET_FLAG);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);

    /* RULE_TREE dispatches the pending radial confirm on the next frame:
     * SET_FLAG takes an argument, so this opens the fuzzy finder. */
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_ARGUMENT, game.editor_state.rule_edit_field);

    /* Scroll 0 is "[ NEW... ]" -> word builder, prefilled with the row's
     * current (still-uncommitted) argument "used". */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_STRING("used", game.editor_state.word_builder_buf);

    InputState cancel = {0};
    input_state_press_key(&cancel, KEY_ESCAPE);
    test_advance_frame(&game, cancel); /* pop clears "used" (no underscore) */
    TEST_ASSERT_EQUAL_STRING("", game.editor_state.word_builder_buf);

    InputState wb_down = {0};
    input_state_press_key(&wb_down, KEY_DOWN);
    test_advance_frame(&game, wb_down); /* word_builder_scroll 1: "chest" */
    test_advance_frame(&game, confirm); /* append "chest" */

    InputState wb_up = {0};
    input_state_press_key(&wb_up, KEY_UP);
    test_advance_frame(&game, wb_up);   /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm); /* finalize: commit to the rule */

    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_NONE, game.editor_state.rule_edit_field);

    Blueprint *switch_bp = &game.state.gamedata.blueprints.entries.data[0];
    ActionNode *set_flag_node = &switch_bp->rules.data[0].action_tree.nodes.data[0];
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, set_flag_node->type);
    TEST_ASSERT_EQUAL_STRING("chest", set_flag_node->argument.ptr);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Edit rule action"));

    /* Re-open the same row, pick a DIFFERENT type this time
     * (ACTION_CLEAR_FLAG), then CANCEL from the follow-up fuzzy finder
     * step -- the staged type change must never reach the real node. */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    test_select_rule_radial_item(&game, ACTION_CLEAR_FLAG);
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, game.editor_state.sub_mode);

    test_advance_frame(&game, cancel);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_NONE, game.editor_state.rule_edit_field);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, set_flag_node->type);
    TEST_ASSERT_EQUAL_STRING("chest", set_flag_node->argument.ptr);

    /* Save through the real pause-menu path. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *blueprints = toml_array_in(root, "blueprint");
    TEST_ASSERT_NOT_NULL(blueprints);
    toml_table_t *switch_table = test_find_blueprint_table_by_name(blueprints, "switch");
    TEST_ASSERT_NOT_NULL_MESSAGE(switch_table, "'switch' missing from saved TOML");

    toml_array_t *rule_array = toml_array_in(switch_table, "rule");
    TEST_ASSERT_NOT_NULL(rule_array);
    toml_table_t *rule_table = toml_table_at(rule_array, 0);
    TEST_ASSERT_NOT_NULL(rule_table);
    toml_array_t *actions = toml_array_in(rule_table, "actions");
    TEST_ASSERT_NOT_NULL(actions);
    toml_datum_t first_action = toml_string_at(actions, 0);
    TEST_ASSERT_TRUE(first_action.ok);
    TEST_ASSERT_EQUAL_STRING("set_flag:chest", first_action.u.s);
    free(first_action.u.s);

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.6c: Rule mode structural editing (insert/delete/reorder action nodes).
 * Reuses fixture_rule_tree's "switch" blueprint and its S5.6a-documented
 * pool layout: roots=[0,1], node0=set_flag:used, node1=if_else
 * (children=[2,3]=[destroy, set_flag:opened], else_children=[4]=
 * [set_flag:blocked]).
 *
 * Drives all three ops through the real input layer, in sequence, on the
 * same tree, then confirms all three landed via a single save+reparse:
 *
 *  1. INSERT: focus row 2 (set_flag:used) and press ACTION_EDITOR_PLACE
 *     (P key). A 6th pool node is spliced into roots right after node 0,
 *     the cursor follows it to its new row, and the ACTION_TYPE radial
 *     opens immediately (S5.6b's picker, reused) -- picking ACTION_SET_FLAG
 *     and typing "chest" via the fuzzy-finder/word-builder chain (same
 *     idiom as test_integration_editor_rule_leaf_edit_round_trip) gives the
 *     new node a real type/argument.
 *  2. DELETE: focus the "destroy" row (nested inside the if_else's "then"
 *     branch) and press ACTION_EDITOR_DELETE. Its index is dropped from
 *     node1.children -- nodes.count does NOT shrink (no compaction; see
 *     CLAUDE.md's action-tree delete model), proving the node merely
 *     becomes unreachable rather than freed.
 *  3. MOVE: focus the new "chest" node and press ACTION_EDITOR_MOVE_DOWN
 *     (Ctrl+Down chord) to swap it past the if_else in the root list.
 *
 * The final save+reparse confirms all three: "destroy" is gone from the
 * emitted "then" array, "chest" appears as a top-level action, and it sits
 * AFTER the if/else table (the move). */
void test_integration_editor_rule_structural_edit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_rule_tree));
    game.frame_ctx.save_fn = test_recording_gamedata_save;

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    test_radial_select_item(&game, EDITOR_TOOLS_RULE_INDEX);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_RULE, game.editor_state.top_mode);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm); /* pick "switch" */
    test_advance_frame(&game, confirm); /* open its only rule */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);

    Blueprint *switch_bp = &game.state.gamedata.blueprints.entries.data[0];
    Rule *rule = &switch_bp->rules.data[0];
    TEST_ASSERT_EQUAL_INT(5, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.roots.count);

    /* --- INSERT: focus row 2 (set_flag:used) --- */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frames(&game, nav_down, 2);
    TEST_ASSERT_EQUAL_INT(2, game.editor_state.rule_tree_row);

    InputState place_input = {0};
    input_state_press_key(&place_input, KEY_P);
    test_advance_frame(&game, place_input);

    /* A new pool node (index 5) is spliced into roots right after node 0:
     * roots become [0, 5, 1]. */
    TEST_ASSERT_EQUAL_INT(6, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(3, rule->action_tree.roots.count);
    TEST_ASSERT_EQUAL_INT(0, rule->action_tree.roots.data[0]);
    TEST_ASSERT_EQUAL_INT(5, rule->action_tree.roots.data[1]);
    TEST_ASSERT_EQUAL_INT(1, rule->action_tree.roots.data[2]);
    TEST_ASSERT_EQUAL_INT(3, game.editor_state.rule_tree_row); /* cursor follows the new row */
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Insert rule action"));

    /* The insert immediately opens the ACTION_TYPE radial (S5.6b's picker,
     * reused) so the new node gets a real type/argument. */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RADIAL_CTX_ACTION_TYPE, game.editor_state.radial_context);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_TYPE, game.editor_state.rule_edit_field);

    test_select_rule_radial_item(&game, ACTION_SET_FLAG);
    test_advance_frame(&game, no_input); /* RULE_TREE dispatches the radial confirm -> fuzzy finder */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, game.editor_state.sub_mode);

    test_advance_frame(&game, confirm); /* scroll 0 "[ NEW... ]" -> word builder, prefilled empty */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_STRING("", game.editor_state.word_builder_buf);

    InputState wb_down = {0};
    input_state_press_key(&wb_down, KEY_DOWN);
    test_advance_frame(&game, wb_down); /* word_builder_scroll 1: "chest" */
    test_advance_frame(&game, confirm); /* append "chest" */

    InputState wb_up = {0};
    input_state_press_key(&wb_up, KEY_UP);
    test_advance_frame(&game, wb_up);   /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm); /* finalize: commit type+argument */

    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_NONE, game.editor_state.rule_edit_field);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, rule->action_tree.nodes.data[5].type);
    TEST_ASSERT_EQUAL_STRING("chest", rule->action_tree.nodes.data[5].argument.ptr);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Edit rule action"));

    /* --- DELETE: focus "destroy" (row 5: trigger, condition, node0, node5,
     * if_else, THEN destroy) and remove it --- */
    test_advance_frames(&game, nav_down, 2);
    TEST_ASSERT_EQUAL_INT(5, game.editor_state.rule_tree_row);

    InputState delete_input = {0};
    input_state_press_key(&delete_input, KEY_DELETE);
    test_advance_frame(&game, delete_input);

    /* No compaction: the pool still holds 6 nodes (the removed "destroy" is
     * simply no longer reachable), only node1's children list shrank. */
    TEST_ASSERT_EQUAL_INT(6, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(1, rule->action_tree.nodes.data[1].children.count);
    TEST_ASSERT_EQUAL_INT(3, rule->action_tree.nodes.data[1].children.data[0]);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Delete rule action"));

    /* --- MOVE: focus the "chest" node (node 5) and move it down past the
     * if_else --- */
    InputState nav_up = {0};
    input_state_press_key(&nav_up, KEY_UP);
    test_advance_frames(&game, nav_up, 2);
    TEST_ASSERT_EQUAL_INT(3, game.editor_state.rule_tree_row);

    InputState move_down_input = {0};
    input_state_hold_key(&move_down_input, KEY_LEFT_CONTROL);
    input_state_press_key(&move_down_input, KEY_DOWN);
    test_advance_frame(&game, move_down_input);

    TEST_ASSERT_EQUAL_INT(0, rule->action_tree.roots.data[0]);
    TEST_ASSERT_EQUAL_INT(1, rule->action_tree.roots.data[1]);
    TEST_ASSERT_EQUAL_INT(5, rule->action_tree.roots.data[2]);
    TEST_ASSERT_EQUAL_INT(6, game.editor_state.rule_tree_row); /* cursor follows node 5 to its new row */
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Move rule action"));

    /* --- Save through the real pause-menu path and reparse --- */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *blueprints = toml_array_in(root, "blueprint");
    TEST_ASSERT_NOT_NULL(blueprints);
    toml_table_t *switch_table = test_find_blueprint_table_by_name(blueprints, "switch");
    TEST_ASSERT_NOT_NULL_MESSAGE(switch_table, "'switch' missing from saved TOML");

    toml_array_t *rule_array = toml_array_in(switch_table, "rule");
    TEST_ASSERT_NOT_NULL(rule_array);
    toml_table_t *rule_table = toml_table_at(rule_array, 0);
    TEST_ASSERT_NOT_NULL(rule_table);
    toml_array_t *actions = toml_array_in(rule_table, "actions");
    TEST_ASSERT_NOT_NULL(actions);
    TEST_ASSERT_EQUAL_INT(3, toml_array_nelem(actions));

    toml_datum_t action0 = toml_string_at(actions, 0);
    TEST_ASSERT_TRUE(action0.ok);
    TEST_ASSERT_EQUAL_STRING("set_flag:used", action0.u.s);
    free(action0.u.s);

    toml_table_t *if_table = toml_table_at(actions, 1);
    TEST_ASSERT_NOT_NULL_MESSAGE(if_table, "if/else missing at actions[1] -- move landed in the wrong slot");
    toml_array_t *then_array = toml_array_in(if_table, "then");
    TEST_ASSERT_NOT_NULL(then_array);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, toml_array_nelem(then_array), "deleted 'destroy' still present in 'then'");
    toml_datum_t then0 = toml_string_at(then_array, 0);
    TEST_ASSERT_TRUE(then0.ok);
    TEST_ASSERT_EQUAL_STRING("set_flag:opened", then0.u.s);
    free(then0.u.s);

    toml_datum_t action2 = toml_string_at(actions, 2);
    TEST_ASSERT_TRUE_MESSAGE(action2.ok, "moved 'chest' action not found at actions[2]");
    TEST_ASSERT_EQUAL_STRING("set_flag:chest", action2.u.s);
    free(action2.u.s);

    toml_free(root);
    test_game_teardown(&game);
}

/* S5.6d: Rule mode subroutine authoring -- the final Rule mode slice.
 * Subroutines are gamedata-level (rule.h's vec_subroutine), not per-blueprint,
 * so they get their own list reachable from the blueprint picker's trailing
 * "Subroutines" row (handle_rule_blueprint_list_input, editor/rule.c) rather
 * than nesting under any one blueprint. Reuses fixture_rule_tree purely for
 * its one blueprint (so the blueprint list has a real row before the
 * Subroutines sentinel); the fixture's existing rule/tree is untouched.
 *
 * Drives, in sequence, entirely through the real input layer:
 *  1. Enter Rule mode (test_radial_select_item, Rules sector), landing on
 *     the blueprint list (rule_blueprint_index == -1). NAV_DOWN once reaches
 *     the trailing "Subroutines" row (index == blueprint count, here 1);
 *     CONFIRM enters the (empty) subroutine list without touching
 *     rule_blueprint_index -- proving CANCEL later returns to this same
 *     blueprint-picker view rather than some other state.
 *  2. The subroutine list has one row -- "+ NEW SUBROUTINE" -- since there
 *     are no subroutines yet; CONFIRM opens the word builder. Same
 *     append-then-DONE idiom as
 *     test_integration_editor_rule_leaf_edit_round_trip: NAV_DOWN once
 *     ("chest"), CONFIRM (append), NAV_UP (back to "[ DONE ]"), CONFIRM
 *     (create_new_subroutine("chest")). This lands directly in
 *     EDITOR_SUB_RULE_TREE for the fresh, empty subroutine.
 *  3. The subroutine's action tree is empty (0 flattened rows, since a
 *     Subroutine has no trigger/conditions -- see RuleTreeTarget,
 *     editor/internal.h), so ACTION_EDITOR_PLACE (S5.6c's insert, reused
 *     verbatim) appends straight into action_tree.roots -- proving
 *     insert_rule_action_node's "no action row focused yet" fallback still
 *     works when there is no trigger/condition row ahead of it either. Same
 *     ACTION_TYPE radial + fuzzy finder + word builder chain as S5.6c's own
 *     insert test gives the new node a real type/argument (set_flag:locked).
 *  4. Save through the real pause-menu path and reparse: the emitted
 *     [[subroutine]] block must carry name="chest" and
 *     actions=["set_flag:locked"].
 *  5. Back out to the subroutine list (still showing "chest", since CANCEL
 *     from RULE_TREE only resets sub_mode, not rule_viewing_subroutines) and
 *     ACTION_EDITOR_DELETE it; save+reparse again must show no
 *     [[subroutine]] block at all (emit_subroutines emits nothing for an
 *     empty vec_subroutine). */
void test_integration_editor_subroutine_edit_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_rule_tree));
    game.frame_ctx.save_fn = test_recording_gamedata_save;

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    test_radial_select_item(&game, EDITOR_TOOLS_RULE_INDEX);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_RULE, game.editor_state.top_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.rule_blueprint_index);

    /* NAV_DOWN once: scroll 0 ("switch") -> scroll 1 (the trailing
     * "Subroutines" row -- fixture_rule_tree has exactly one blueprint). */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.rule_blueprint_scroll);

    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_TRUE(game.editor_state.rule_viewing_subroutines);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.rule_blueprint_index);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_subroutine_scroll);

    /* Subroutine list is empty: scroll 0 is already "+ NEW SUBROUTINE". */
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_STRING("", game.editor_state.word_builder_buf);

    test_advance_frame(&game, nav_down); /* word_builder_scroll 1: "chest" */
    test_advance_frame(&game, confirm);  /* append "chest" */

    InputState wb_up = {0};
    input_state_press_key(&wb_up, KEY_UP);
    test_advance_frame(&game, wb_up);   /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm); /* create_new_subroutine("chest") */

    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(1, game.state.gamedata.subroutines.count);
    TEST_ASSERT_EQUAL_STRING("chest", game.state.gamedata.subroutines.data[0].name.ptr);
    TEST_ASSERT_EQUAL_INT(0, game.state.gamedata.subroutines.data[0].action_tree.roots.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Create subroutine"));

    /* --- INSERT into the fresh, empty subroutine tree (S5.6c's insert, reused) --- */
    InputState place_input = {0};
    input_state_press_key(&place_input, KEY_P);
    test_advance_frame(&game, place_input);

    Subroutine *subroutine = &game.state.gamedata.subroutines.data[0];
    TEST_ASSERT_EQUAL_INT(1, subroutine->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(1, subroutine->action_tree.roots.count);
    TEST_ASSERT_EQUAL_INT(0, subroutine->action_tree.roots.data[0]);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RADIAL_CTX_ACTION_TYPE, game.editor_state.radial_context);

    test_select_rule_radial_item(&game, ACTION_SET_FLAG);
    test_advance_frame(&game, no_input); /* RULE_TREE dispatches the radial confirm -> fuzzy finder */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, game.editor_state.sub_mode);

    test_advance_frame(&game, confirm); /* scroll 0 "[ NEW... ]" -> word builder, prefilled empty */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_STRING("", game.editor_state.word_builder_buf);

    test_advance_frames(&game, nav_down, 2); /* word_builder_scroll 2: "locked" */
    test_advance_frame(&game, confirm);      /* append "locked" */

    test_advance_frames(&game, wb_up, 2); /* word_builder_scroll 0: "[ DONE ]" */
    test_advance_frame(&game, confirm);   /* finalize: commit type+argument */

    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_TREE, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(RULE_EDIT_FIELD_NONE, game.editor_state.rule_edit_field);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, subroutine->action_tree.nodes.data[0].type);
    TEST_ASSERT_EQUAL_STRING("locked", subroutine->action_tree.nodes.data[0].argument.ptr);

    /* --- Save through the real pause-menu path and reparse --- */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);

    InputState menu_down = {0};
    input_state_press_key(&menu_down, KEY_DOWN);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);

    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(1, game.gamedata_save_count);

    char errbuf[200];
    char *parse_buf = strdup(game.saved_gamedata_buf);
    toml_table_t *root = toml_parse(parse_buf, errbuf, (int)sizeof(errbuf));
    free(parse_buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, errbuf);

    toml_array_t *sub_array = toml_array_in(root, "subroutine");
    TEST_ASSERT_NOT_NULL_MESSAGE(sub_array, "[[subroutine]] missing from saved TOML");
    TEST_ASSERT_EQUAL_INT(1, toml_array_nelem(sub_array));
    toml_table_t *sub_table = toml_table_at(sub_array, 0);
    TEST_ASSERT_NOT_NULL(sub_table);
    toml_datum_t sub_name = toml_string_in(sub_table, "name");
    TEST_ASSERT_TRUE(sub_name.ok);
    TEST_ASSERT_EQUAL_STRING("chest", sub_name.u.s);
    free(sub_name.u.s);
    toml_array_t *sub_actions = toml_array_in(sub_table, "actions");
    TEST_ASSERT_NOT_NULL(sub_actions);
    toml_datum_t sub_action0 = toml_string_at(sub_actions, 0);
    TEST_ASSERT_TRUE(sub_action0.ok);
    TEST_ASSERT_EQUAL_STRING("set_flag:locked", sub_action0.u.s);
    free(sub_action0.u.s);
    toml_free(root);

    /* --- DELETE the subroutine and confirm it's gone after save+reparse --- */
    InputState cancel = {0};
    input_state_press_key(&cancel, KEY_ESCAPE);
    test_advance_frame(&game, cancel); /* RULE_TREE -> RULE_LIST (still viewing subroutines) */
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RULE_LIST, game.editor_state.sub_mode);
    TEST_ASSERT_TRUE(game.editor_state.rule_viewing_subroutines);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.rule_subroutine_scroll);

    InputState delete_input = {0};
    input_state_press_key(&delete_input, KEY_DELETE);
    test_advance_frame(&game, delete_input);
    TEST_ASSERT_EQUAL_INT(0, game.state.gamedata.subroutines.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&game.undo_history), "Delete subroutine"));

    test_advance_frame(&game, menu_open);
    test_advance_frame(&game, menu_down);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, game.menu.selected);
    test_advance_frame(&game, confirm);
    TEST_ASSERT_EQUAL_INT(2, game.gamedata_save_count);

    char errbuf2[200];
    char *parse_buf2 = strdup(game.saved_gamedata_buf);
    toml_table_t *root2 = toml_parse(parse_buf2, errbuf2, (int)sizeof(errbuf2));
    free(parse_buf2);
    TEST_ASSERT_NOT_NULL_MESSAGE(root2, errbuf2);
    TEST_ASSERT_NULL_MESSAGE(toml_array_in(root2, "subroutine"), "deleted subroutine still present after save+reparse");
    toml_free(root2);

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

    /* Walk to QUIT. The menu has seven entries: RESUME, SAVE, RESTORE,
     * INVENTORY, SETTINGS, TOGGLE_DEBUG_OVERLAY, QUIT — 6 down-presses
     * from RESUME. One tap per frame. */
    for (int step = 0; step < 6; step++) {
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

    /* Open menu (F3) → walk to SETTINGS (4 down-presses from RESUME) → confirm. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    TEST_ASSERT_TRUE(game.menu.open);

    for (int step = 0; step < 4; step++) {
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
 * The TestGame wires a recording preferences_save_fn fake (see
 * test_helpers.c) instead of a null one, so the test also asserts the
 * dispatcher was actually invoked, with the committed data_dir. */
void test_integration_settings_path_edit_commit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Open menu → SETTINGS (4 down-presses) → confirm. */
    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 4; step++) {
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

    /* The browse list seeds with browse_index = 0, which is the
     * synthesized "<USE THIS DIRECTORY>" row. CONFIRM commits the
     * current buffer (seeded from preferences.data_dir) and exits. */
    TEST_ASSERT_EQUAL_INT(0, game.settings.path_edit.browse_index);
    InputState commit = {0};
    input_state_press_key(&commit, KEY_ENTER);
    test_advance_frame(&game, commit);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_LIST, (int)game.settings.screen);
    TEST_ASSERT_FALSE(game.settings.save_preferences_requested);
    TEST_ASSERT_NOT_NULL(game.state.preferences.data_dir.ptr);
    TEST_ASSERT_EQUAL_INT(1, game.preferences_save_count);
    TEST_ASSERT_EQUAL_STRING(game.state.preferences.data_dir.ptr, game.saved_data_dir);

    test_game_teardown(&game);
}

/* Open path-edit, walk one folder up via the synthesized ".." row, and
 * verify the browse list re-populates instead of falling off the end of
 * the filesystem.
 *
 * The earlier failure mode: the default seed "data/" was relative, so
 * raylib's GetPrevDirectoryPath stripped it down to "data", then "" on a
 * second press. LoadDirectoryFilesEx("") returns zero entries silently
 * and the user lands on a screen with nothing to navigate. The fix
 * resolves the seed to absolute on entry and re-normalizes on every
 * refresh, so the parent walk now traverses real filesystem ancestors. */
void test_integration_settings_path_edit_parent_lists_contents(void)
{
    /* Build a known directory tree under tmp so the assertion is not
     * tied to the developer's home layout. */
    char base[512];
    (void)snprintf(base, sizeof(base), "%s/sleipner_path_edit_XXXXXX", "/tmp");
    TEST_ASSERT_NOT_NULL(mkdtemp(base));
    char child[640];
    (void)snprintf(child, sizeof(child), "%s/child", base);
    TEST_ASSERT_EQUAL_INT(0, mkdir(child, 0755));

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    /* Reseed preferences.data_dir to the child directory so the path-edit
     * screen opens already inside our fixture. */
    str_clear(&game.state.preferences.data_dir);
    (void)str_append_cstr(&game.state.preferences.data_dir, child);

    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 4; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);

    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);

    InputState confirm_path = {0};
    input_state_press_key(&confirm_path, KEY_ENTER);
    test_advance_frame(&game, confirm_path);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_PATH_EDIT, (int)game.settings.screen);

    /* Cancel pops up one level (the screen-handler maps CANCEL to
     * "go to parent" while at_root is false). After the press, the
     * dir_list must list at least one entry — our `child` subfolder. */
    InputState back = {0};
    input_state_press_key(&back, KEY_ESCAPE);
    test_advance_frame(&game, back);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_PATH_EDIT, (int)game.settings.screen);
    TEST_ASSERT_TRUE(game.settings.path_edit.dir_list_loaded);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)game.settings.path_edit.dir_list.count);

    test_game_teardown(&game);

    (void)rmdir(child);
    (void)rmdir(base);
}

/* The synthesized "<SELECT DRIVE>" row at index 1 in the browse list
 * must transition path-edit into DRIVE_SELECT mode. Picking the first
 * drive (which on Linux is always "/") returns to BROWSE rooted there.
 *
 * The test drives the screen as a black box: no internal mode helpers
 * are called — only input frames and observable state assertions. */
void test_integration_settings_path_edit_drive_select_round_trip(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 4; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);

    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);

    InputState confirm_path = {0};
    input_state_press_key(&confirm_path, KEY_ENTER);
    test_advance_frame(&game, confirm_path);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_PATH_EDIT, (int)game.settings.screen);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_BROWSE, (int)game.settings.path_edit.mode);

    /* Move down once: row 0 is USE THIS, row 1 is SELECT DRIVE. */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(1, game.settings.path_edit.browse_index);

    /* Confirm enters DRIVE_SELECT mode. The drive list is populated. */
    InputState enter_drive = {0};
    input_state_press_key(&enter_drive, KEY_ENTER);
    test_advance_frame(&game, enter_drive);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_DRIVE_SELECT, (int)game.settings.path_edit.mode);
    TEST_ASSERT_GREATER_THAN_INT(0, game.settings.path_edit.drive_count);

    /* Picking the first drive sets the buffer and flips back to BROWSE.
     * On Linux the drive is "/", so we land at filesystem root. */
    InputState pick = {0};
    input_state_press_key(&pick, KEY_ENTER);
    test_advance_frame(&game, pick);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_BROWSE, (int)game.settings.path_edit.mode);
    TEST_ASSERT_TRUE(game.settings.path_edit.at_root);

    test_game_teardown(&game);
}

/* Commit a path that contains backslashes (as raylib's _WIN32 path-join
 * code can produce when the Proton build joins "/" with "data") and
 * verify the saved data_dir is normalized to forward slashes with no
 * duplicate separators. The keyboard widget cannot type a backslash
 * directly, so the test seeds the buffer through preferences.data_dir
 * before opening path-edit, mirroring how the bug surfaced in practice
 * (raylib's directory listing populated buf with the bad separator). */
void test_integration_settings_path_edit_commit_normalizes_separators(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    str_clear(&game.state.preferences.data_dir);
    (void)str_append_cstr(&game.state.preferences.data_dir, "/\\data\\");

    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 4; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);

    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);

    InputState confirm_path = {0};
    input_state_press_key(&confirm_path, KEY_ENTER);
    test_advance_frame(&game, confirm_path);

    InputState commit = {0};
    input_state_press_key(&commit, KEY_ENTER);
    test_advance_frame(&game, commit);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_LIST, (int)game.settings.screen);
    TEST_ASSERT_NOT_NULL(game.state.preferences.data_dir.ptr);
    TEST_ASSERT_NULL(strchr(game.state.preferences.data_dir.ptr, '\\'));
    TEST_ASSERT_NULL(strstr(game.state.preferences.data_dir.ptr, "//"));
    TEST_ASSERT_EQUAL_STRING("/data/", game.state.preferences.data_dir.ptr);

    test_game_teardown(&game);
}

/* S5.7/D38: the Tools radial's stick-angle input has no good keyboard
 * equivalent (TODO.md: "the radial menu is hard to use using a keyboard"),
 * so handle_radial_input (editor/widgets.c) now also reads
 * ACTION_NAV_LEFT/RIGHT (arrow keys / d-pad) to rotate the highlighted
 * wedge and digit keys 1-9 (read as raw keys via input_key_pressed, not
 * routed through the BindingStore) to jump straight to and confirm a wedge
 * by position. Drives entirely through the real input layer: F5 into
 * editor, TAB opens the Tools radial, NAV_RIGHT/LEFT rotate radial_selected
 * with wraparound from "nothing highlighted", then KEY_SEVEN (digit 7,
 * zero-based index 6 == EDITOR_TOOLS_LEVELS_INDEX) direct-selects and
 * confirms "Levels" in the same frame -- the same sector
 * test_radial_select_item's stick-angle math reaches in
 * test_integration_editor_level_switch_round_trip, proving the digit path
 * lands on the identical dispatch through a different input method. */
void test_integration_editor_radial_keyboard_nav(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.radial_selected);

    /* NAV_RIGHT from "nothing highlighted" lands on wedge 0; a second press
     * advances to wedge 1; NAV_LEFT then steps back to wedge 0. */
    InputState nav_right = {0};
    input_state_press_key(&nav_right, KEY_RIGHT);
    test_advance_frame(&game, nav_right);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.radial_selected);
    test_advance_frame(&game, nav_right);
    TEST_ASSERT_EQUAL_INT(1, game.editor_state.radial_selected);
    InputState nav_left = {0};
    input_state_press_key(&nav_left, KEY_LEFT);
    test_advance_frame(&game, nav_left);
    TEST_ASSERT_EQUAL_INT(0, game.editor_state.radial_selected);

    /* Digit '7' direct-selects and confirms "Levels" in the same frame. */
    InputState digit_seven = {0};
    input_state_press_key(&digit_seven, KEY_SEVEN);
    test_advance_frame(&game, digit_seven);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOOLS_LEVELS_INDEX, game.editor_state.radial_selected);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_TOP_LEVEL, (int)game.editor_state.top_mode);

    test_game_teardown(&game);
}

/* D38 "Handles gets surfaced in the Tools radial label": the tools[] label
 * table (editor/widgets.c's radial_label) and dispatch_radial_confirm
 * (editor/core.c) already carried a "Handles" entry at index 2 from when
 * Handles mode was first added -- this test is the regression guard that
 * was missing for it. Confirming that wedge with an entity selected enters
 * EDITOR_SUB_HANDLES for the selected entity. Mirrors
 * test_integration_editor_watch_list_removes_focused_entry's shape: F5 into
 * editor, CONFIRM selects the nearest root entity (tall_tree), TAB opens
 * the Tools radial, test_radial_select_item aims at index 2 and confirms. */
void test_integration_editor_radial_handles_entry(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);

    InputState select_input = {0};
    input_state_press_key(&select_input, KEY_ENTER);
    test_advance_frame(&game, select_input);
    Entity *tall_tree = test_find_entity_by_blueprint(&game.state, "tall_tree");
    TEST_ASSERT_NOT_NULL(tall_tree);
    TEST_ASSERT_EQUAL_INT(tall_tree->id, game.editor_state.selected_entity_id);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    test_radial_select_item(&game, 2);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);

    /* BROWSE dispatches the pending radial confirmation on the next frame. */
    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_HANDLES, game.editor_state.sub_mode);

    test_game_teardown(&game);
}

/* Companion case for the Handles wedge above: confirming it with no entity
 * selected must not silently do nothing (the pre-existing "Grab"/"Place"
 * wedges do, on the same confirmed<0-guard shape) -- it now raises a toast
 * telling the user to select an entity first and stays in BROWSE. */
void test_integration_editor_radial_handles_requires_selection(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState editor_toggle = {0};
    input_state_press_key(&editor_toggle, KEY_F5);
    test_advance_frame(&game, editor_toggle);
    TEST_ASSERT_TRUE(game.state.editor_mode);
    TEST_ASSERT_EQUAL_INT(-1, game.editor_state.selected_entity_id);

    InputState open_tools = {0};
    input_state_press_key(&open_tools, KEY_TAB);
    test_advance_frame(&game, open_tools);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_RADIAL, game.editor_state.sub_mode);

    test_radial_select_item(&game, 2);

    InputState no_input = {0};
    test_advance_frame(&game, no_input);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_BROWSE, game.editor_state.sub_mode);
    TEST_ASSERT_TRUE(game.editor_state.toast_timer > 0.0F);
    TEST_ASSERT_TRUE(strv_eq_cstr(game.editor_state.toast_text, "Select an entity first"));

    test_game_teardown(&game);
}

/* D38 "Path picker has no manual edit field on top": NAV_UP from the top
 * row (0, the synthesized "<USE THIS DIRECTORY>" row) now focuses the
 * buffer-display line drawn above the browse list -- a sentinel
 * browse_index of -1 (PATH_EDIT_ROW_BUFFER, engine/src/settings.c; a
 * private constant, so this test uses the literal like the existing
 * drive-select test above uses literal 1 for PATH_EDIT_ROW_SELECT_DRIVE).
 * CONFIRM there enters KEYBOARD mode positioned on the existing buf/len
 * (keyboard_widget_reset does not clear them), the same state
 * ACTION_WB_KEYBOARD_MODE already reached from anywhere in BROWSE -- this
 * just gives it a focusable, discoverable entry point. Mirrors
 * test_integration_settings_path_edit_commit's navigation prologue. */
void test_integration_settings_path_edit_buffer_row_enters_keyboard_mode(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_gamedata));

    InputState menu_open = {0};
    input_state_press_key(&menu_open, KEY_F3);
    test_advance_frame(&game, menu_open);
    for (int step = 0; step < 4; step++) {
        InputState down = {0};
        input_state_press_key(&down, KEY_DOWN);
        test_advance_frame(&game, down);
    }
    InputState confirm = {0};
    input_state_press_key(&confirm, KEY_ENTER);
    test_advance_frame(&game, confirm);

    InputState tab_next = {0};
    input_state_press_key(&tab_next, KEY_TAB);
    test_advance_frame(&game, tab_next);

    InputState confirm_path = {0};
    input_state_press_key(&confirm_path, KEY_ENTER);
    test_advance_frame(&game, confirm_path);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_PATH_EDIT, (int)game.settings.screen);
    TEST_ASSERT_EQUAL_INT(0, game.settings.path_edit.browse_index);

    char seeded_buf[PATH_EDIT_BUF_SIZE];
    (void)snprintf(seeded_buf, sizeof(seeded_buf), "%s", game.settings.path_edit.buf);
    int seeded_len = game.settings.path_edit.len;
    TEST_ASSERT_TRUE(seeded_len > 0);

    /* NAV_UP from the top row focuses the buffer-display line. */
    InputState nav_up = {0};
    input_state_press_key(&nav_up, KEY_UP);
    test_advance_frame(&game, nav_up);
    TEST_ASSERT_EQUAL_INT(-1, game.settings.path_edit.browse_index);

    /* NAV_DOWN returns focus to the list without entering KEYBOARD mode. */
    InputState nav_down = {0};
    input_state_press_key(&nav_down, KEY_DOWN);
    test_advance_frame(&game, nav_down);
    TEST_ASSERT_EQUAL_INT(0, game.settings.path_edit.browse_index);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_BROWSE, (int)game.settings.path_edit.mode);

    /* Back up to the buffer row and CONFIRM: enters KEYBOARD mode
     * positioned on the existing buffer, not a cleared one. */
    test_advance_frame(&game, nav_up);
    TEST_ASSERT_EQUAL_INT(-1, game.settings.path_edit.browse_index);
    InputState enter_edit = {0};
    input_state_press_key(&enter_edit, KEY_ENTER);
    test_advance_frame(&game, enter_edit);
    TEST_ASSERT_EQUAL_INT(PATH_EDIT_KEYBOARD, (int)game.settings.path_edit.mode);
    TEST_ASSERT_EQUAL_INT(seeded_len, game.settings.path_edit.len);
    TEST_ASSERT_EQUAL_STRING(seeded_buf, game.settings.path_edit.buf);

    test_game_teardown(&game);
}

/* ---- Integration: `wait:` + suspendable rule continuations (S6.7b, D24) ----
 *
 * Every test here drives real frames through game_update (via
 * test_advance_frame(s)) and asserts only on observable game state
 * (flags, attrs) -- never on continuation/ExecFrame internals. Frame
 * counts are chosen with a several-frame margin away from the exact
 * wait-duration boundary so float rounding in the per-frame delta_time
 * subtraction can never flip an assertion. */

void test_integration_wait_delays_subsequent_actions(void)
{
    /* on_spawn: set_flag:a fires immediately; wait:0.5 (30 frames @
     * 1/60s) defers set_flag:b until roughly half a second of frames
     * has elapsed. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"waiter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"set_flag:a\", \"wait:0.5\", \"set_flag:b\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"waiter\"\n"
                                  "pos = [10, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "a"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "b"));

    InputState idle = {0};
    test_advance_frames(&game, idle, 20); /* 0.333s -- well short of 0.5s */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "b"));

    test_advance_frames(&game, idle, 20); /* 40 frames total = 0.667s -- well past 0.5s */
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "b"));

    test_game_teardown(&game);
}

void test_integration_wait_inside_if_else(void)
{
    /* The if-condition is always true (not_flag:never_set), so the
     * `then` branch's wait:0.2 (12 frames) must delay post_then, while
     * post_else (the untaken branch) must never fire at all. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"waiter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [{ if = [\"not_flag:never_set\"], then = [\"set_flag:pre\", \"wait:0.2\", "
                                  "\"set_flag:post_then\"], else = [\"set_flag:post_else\"] }]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"waiter\"\n"
                                  "pos = [10, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "pre"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "post_then"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "post_else"));

    InputState idle = {0};
    test_advance_frames(&game, idle, 6); /* 0.1s -- well short of 0.2s */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "post_then"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "post_else"));

    test_advance_frames(&game, idle, 14); /* 20 frames total = 0.333s -- well past 0.2s */
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "post_then"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "post_else"));

    test_game_teardown(&game);
}

static int sum_target_hit_counts(GameState *state)
{
    int total = 0;
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *entity = &state->gamedata.current_level.entities.data[index];
        /* is_target/hit_count start as blueprint-only defaults -- never
         * copied onto the instance attrs -- so a scoped lookup (falling
         * back from instance to blueprint) is required, not a plain
         * attr_get, which would only ever see the instance override
         * add_attr writes once an entity has actually been hit. */
        const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
        if (attr_get_scoped_bool(&entity->attrs, defaults, "is_target", false)) {
            total += attr_get_scoped_int(&entity->attrs, defaults, "hit_count", 0);
        }
    }
    return total;
}

void test_integration_wait_inside_for_each(void)
{
    /* A single for_each (bind="target", 3 matches) whose body increments
     * the bound entity's hit_count then waits 0.2s (12 frames) staggers
     * one hit per ~12 frames -- proving the for_each's bound entity and
     * scan cursor survive suspend/resume rather than restarting or
     * losing the loop. */
    static const char *gamedata =
        "[[blueprint]]\n"
        "name = \"owner\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "\n"
        "[[blueprint.rule]]\n"
        "trigger = \"on_spawn\"\n"
        "actions = [{ for_each = \"entities\", bind = \"target\", conditions = [\"attr:is_target\"], do = "
        "[\"add_attr:target.hit_count,1\", \"wait:0.2\"] }]\n"
        "\n"
        "[[blueprint]]\n"
        "name = \"target_entity\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "is_target = true\n"
        "hit_count = 0\n"
        "\n"
        "[[level]]\n"
        "name = \"test\"\n"
        "size = [320, 240]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"owner\"\n"
        "pos = [10, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"target_entity\"\n"
        "pos = [20, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"target_entity\"\n"
        "pos = [30, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"target_entity\"\n"
        "pos = [40, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    /* First iteration runs synchronously before the first suspend. */
    TEST_ASSERT_EQUAL_INT(1, sum_target_hit_counts(&game.state));

    InputState idle = {0};
    test_advance_frames(&game, idle, 4); /* well short of the 12-frame wait */
    TEST_ASSERT_EQUAL_INT(1, sum_target_hit_counts(&game.state));

    test_advance_frames(&game, idle, 12); /* 16 total -- past the first wait, short of the second (due ~24) */
    TEST_ASSERT_EQUAL_INT(2, sum_target_hit_counts(&game.state));

    test_advance_frames(&game, idle, 12); /* 28 total -- past the second wait (due ~24) */
    TEST_ASSERT_EQUAL_INT(3, sum_target_hit_counts(&game.state));

    test_game_teardown(&game);
}

void test_integration_two_entities_wait_independently(void)
{
    /* Two entities each start their own on_spawn wait of a different
     * duration; each must complete only at its own time, proving
     * continuations resolve independently by entity id/rule index
     * rather than sharing or crossing state. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"waiter_short\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"wait:0.1\", \"set_flag:short_done\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"waiter_long\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"wait:0.3\", \"set_flag:long_done\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"waiter_short\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"waiter_long\"\n"
                                  "pos = [20, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "short_done"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "long_done"));

    InputState idle = {0};
    test_advance_frames(&game, idle, 3); /* 0.05s -- short of both waits (6 and 18 frames) */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "short_done"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "long_done"));

    test_advance_frames(&game, idle, 7); /* 10 total -- past the short wait (6), short of the long one (18) */
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "short_done"));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "long_done"));

    test_advance_frames(&game, idle, 12); /* 22 total -- past the long wait (18) */
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "short_done"));
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "long_done"));

    test_game_teardown(&game);
}

void test_integration_wait_entity_destroyed_drops_continuation(void)
{
    /* Two independent on_spawn rules on the same entity: one waits
     * briefly then destroys it, the other waits much longer and would
     * set a flag -- but only if the entity is still there when it
     * wakes. Must not crash, and the post-wait flag must never fire. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"doomed\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"wait:0.5\", \"set_flag:after_wait\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"wait:0.1\", \"destroy\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"doomed\"\n"
                                  "pos = [10, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after_wait"));

    InputState idle = {0};
    test_advance_frames(&game, idle, 10); /* past the 6-frame destroy wait, short of the 30-frame long wait */
    const Entity *doomed = test_find_entity_by_blueprint(&game.state, "doomed");
    TEST_ASSERT_NOT_NULL(doomed);
    TEST_ASSERT_FALSE(attr_get_bool(&doomed->attrs, "active", true));
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after_wait"));

    test_advance_frames(&game, idle, 24); /* 34 total -- well past the long wait's 30-frame due point */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after_wait"));

    test_game_teardown(&game);
}

/* ---- Integration: blocking `dialogue:` (S6.7c, D24) ----
 *
 * Same discipline as the `wait:` suite above: every test drives real
 * frames through frame_update (via test_advance_frame(s)) and asserts
 * only on observable game state -- flags, entity attrs/position, and
 * DialogueState's own fields (active/current_page/pages), which are what
 * a player would perceive as "which page is showing" -- never on
 * ExecFrame/continuation internals. */

void test_integration_dialogue_blocks_until_closed(void)
{
    /* on_spawn: set_flag:before fires immediately; dialogue: opens the
     * box and suspends the rule, so set_flag:after must not run until the
     * player closes it. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"narrator\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"set_flag:before\", \"dialogue:Hello world\", \"set_flag:after\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"narrator\"\n"
                                  "pos = [10, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "before"));
    TEST_ASSERT_TRUE(game.state.dialogue.active);
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after"));

    InputState confirm_skip = {0};
    input_state_press_key(&confirm_skip, KEY_ENTER);
    test_advance_frame(&game, confirm_skip); /* mid-reveal -- skips to full */
    TEST_ASSERT_TRUE(game.state.dialogue.active);
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after"));

    InputState confirm_close = {0};
    input_state_press_key(&confirm_close, KEY_ENTER);
    test_advance_frame(&game, confirm_close); /* only page, fully revealed -- closes */
    TEST_ASSERT_FALSE(game.state.dialogue.active);
    /* The D24 blocking guarantee: the post-dialogue action does not run
     * on the very frame the dialogue closes -- the continuation only
     * becomes due starting the NEXT frame's resume pass (see
     * rules_resume_continuations' doc comment, rule.h). */
    TEST_ASSERT_FALSE(flag_get(&game.state.progression.flags, "after"));

    InputState idle = {0};
    test_advance_frame(&game, idle);
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "after"));

    test_game_teardown(&game);
}

void test_integration_dialogue_world_frozen(void)
{
    /* A periodic timer (already created before the dialogue opens, in the
     * same on_spawn rule) and player movement input both prove the world
     * is frozen: neither the timer's rule nor update_player's movement
     * may advance while DialogueState.active is true, since frame_update
     * skips game_update entirely in that case. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"player\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"narrator\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer_periodic:pulse,0.05\", \"dialogue:Hi there\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer_periodic:pulse\"\n"
                                  "actions = [\"add_attr:self.pulse_count,1\"]\n"
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
                                  "blueprint = \"narrator\"\n"
                                  "pos = [10, 10]\n"
                                  "pulse_count = 0\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));
    TEST_ASSERT_TRUE(game.state.dialogue.active);

    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    Vector2 start_position = player->position;

    InputState move_right = {0};
    input_state_hold_key(&move_right, KEY_RIGHT);
    test_advance_frames(&game, move_right, 20); /* 0.333s -- several periodic-timer fires' worth, if not frozen */

    TEST_ASSERT_TRUE(game.state.dialogue.active);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_FLOAT(start_position.x, player->position.x);
    TEST_ASSERT_EQUAL_FLOAT(start_position.y, player->position.y);

    const Entity *narrator = test_find_entity_by_blueprint(&game.state, "narrator");
    TEST_ASSERT_NOT_NULL(narrator);
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&narrator->attrs, nullptr, "pulse_count", 0.0F));

    test_game_teardown(&game);
}

void test_integration_dialogue_pages_and_typewriter(void)
{
    /* Two pages: CONFIRM mid-reveal skips to full, the next CONFIRM
     * advances to page 2, and the reveal count grows with elapsed time. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"narrator\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"dialogue:Page one|Page two\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"narrator\"\n"
                                  "pos = [10, 10]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));
    TEST_ASSERT_TRUE(game.state.dialogue.active);
    TEST_ASSERT_EQUAL_INT(0, game.state.dialogue.current_page);
    TEST_ASSERT_EQUAL_INT(2, game.state.dialogue.pages.count);

    /* Reveal count grows with elapsed time while no CONFIRM is pressed. */
    InputState idle = {0};
    test_advance_frame(&game, idle);
    int page_zero_length = (int)game.state.dialogue.pages.data[0].len;
    int revealed_after_one_frame =
        dialogue_revealed_char_count(game.state.dialogue.reveal_elapsed, DIALOGUE_CHARS_PER_SECOND, page_zero_length);
    test_advance_frames(&game, idle, 5); /* 6 frames total = 0.1s */
    int revealed_after_more_frames =
        dialogue_revealed_char_count(game.state.dialogue.reveal_elapsed, DIALOGUE_CHARS_PER_SECOND, page_zero_length);
    TEST_ASSERT_TRUE(revealed_after_more_frames >= revealed_after_one_frame);
    TEST_ASSERT_TRUE(revealed_after_more_frames > 0);
    TEST_ASSERT_TRUE(revealed_after_more_frames < page_zero_length); /* still mid-reveal */

    /* CONFIRM while mid-reveal skips to the full first page -- still page 0. */
    InputState confirm_skip = {0};
    input_state_press_key(&confirm_skip, KEY_ENTER);
    test_advance_frame(&game, confirm_skip);
    TEST_ASSERT_TRUE(game.state.dialogue.active);
    TEST_ASSERT_EQUAL_INT(0, game.state.dialogue.current_page);
    TEST_ASSERT_EQUAL_INT(page_zero_length, dialogue_revealed_char_count(game.state.dialogue.reveal_elapsed,
                                                                         DIALOGUE_CHARS_PER_SECOND, page_zero_length));

    /* Next CONFIRM (page 0 now fully revealed) advances to page 2, typewriter reset. */
    InputState confirm_advance = {0};
    input_state_press_key(&confirm_advance, KEY_ENTER);
    test_advance_frame(&game, confirm_advance);
    TEST_ASSERT_TRUE(game.state.dialogue.active);
    TEST_ASSERT_EQUAL_INT(1, game.state.dialogue.current_page);

    /* Skip page 2 to full, then CONFIRM closes (last page). */
    InputState confirm_skip2 = {0};
    input_state_press_key(&confirm_skip2, KEY_ENTER);
    test_advance_frame(&game, confirm_skip2);
    TEST_ASSERT_TRUE(game.state.dialogue.active);

    InputState confirm_close = {0};
    input_state_press_key(&confirm_close, KEY_ENTER);
    test_advance_frame(&game, confirm_close);
    TEST_ASSERT_FALSE(game.state.dialogue.active);

    test_game_teardown(&game);
}

/* ---- Integration: behavior dispatch table + input-source seam (S6.9a, D30/D39) ----
 *
 * Same black-box discipline as the suites above: drive real frames through
 * frame_update with a real InputState, assert only on observable entity
 * state (position, moving). No internal symbol (behavior_lookup,
 * BehaviorContext, input_for_entity) appears in a test body. */

void test_integration_static_entity_does_not_move(void)
{
    /* An entity with an explicit "static" behavior, and another with no
     * behavior attr at all (S6.9a/D30's fallback also resolves to static),
     * must both stay exactly where they started while the player moves
     * under held input -- proving the dispatch loop only moves entities
     * whose behavior actually does something. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"statue\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"static\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"rock\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"hero\"\n"
                                  "pos = [100, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"statue\"\n"
                                  "pos = [200, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"rock\"\n"
                                  "pos = [220, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    Entity *statue = test_find_entity_by_blueprint(&game.state, "statue");
    Entity *rock = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_NOT_NULL(statue);
    TEST_ASSERT_NOT_NULL(rock);
    Vector2 statue_start = statue->position;
    Vector2 rock_start = rock->position;

    InputState move_right = {0};
    input_state_hold_key(&move_right, KEY_RIGHT);
    test_advance_frames(&game, move_right, 20);

    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_TRUE(player->position.x > 100.0F); /* the mover actually moved */

    statue = test_find_entity_by_blueprint(&game.state, "statue");
    rock = test_find_entity_by_blueprint(&game.state, "rock");
    TEST_ASSERT_EQUAL_FLOAT(statue_start.x, statue->position.x);
    TEST_ASSERT_EQUAL_FLOAT(statue_start.y, statue->position.y);
    TEST_ASSERT_EQUAL_FLOAT(rock_start.x, rock->position.x);
    TEST_ASSERT_EQUAL_FLOAT(rock_start.y, rock->position.y);

    test_game_teardown(&game);
}

void test_integration_input_source_seam(void)
{
    /* Two entities share the "player" behavior. The first has no
     * input_source attr (defaults to "local:0"); the second sets
     * input_source = "network:1". Both entities are exposed to the same
     * held movement key through frame_update's real InputState, but only
     * the local:0 entity should move -- proving behavior_player routes
     * input through the input_source provider seam (D39) rather than
     * reading the frame's InputState directly. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"remote_hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "input_source = \"network:1\"\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"hero\"\n"
                                  "pos = [100, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"remote_hero\"\n"
                                  "pos = [200, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_EQUAL_STRING("hero", player->blueprint_name.ptr);

    Entity *remote = test_find_entity_by_blueprint(&game.state, "remote_hero");
    TEST_ASSERT_NOT_NULL(remote);
    Vector2 remote_start = remote->position;

    InputState move_right = {0};
    input_state_hold_key(&move_right, KEY_RIGHT);
    test_advance_frames(&game, move_right, 20);

    player = game_get_player_const(&game.state);
    TEST_ASSERT_TRUE(player->position.x > 100.0F);
    TEST_ASSERT_TRUE(player->moving);

    remote = test_find_entity_by_blueprint(&game.state, "remote_hero");
    TEST_ASSERT_EQUAL_FLOAT(remote_start.x, remote->position.x);
    TEST_ASSERT_EQUAL_FLOAT(remote_start.y, remote->position.y);
    TEST_ASSERT_FALSE(remote->moving);

    test_game_teardown(&game);
}

/* ---- Integration: npc_patrol and chase behaviors (S6.9b, D30) ----
 *
 * Same black-box discipline as the dispatch-table suite above: every test
 * drives real frame_update frames and asserts only on entity.position/
 * .moving. No internal symbol (behavior_npc_patrol, chase_step_toward,
 * patrol_phase) appears in a test body. */

void test_integration_npc_patrol_oscillates(void)
{
    /* A patrol entity with no player in the level at all (idle/absent, so
     * nothing else can move it) oscillates along patrol_dx over a 2.0s
     * patrol_period: +x for the first 1.0s half, -x for the second half,
     * landing back near its start position after one full 120-frame cycle
     * at the fixture's 1/60s frame step. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"guard\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"npc_patrol\"\n"
                                  "patrol_dx = 40\n"
                                  "patrol_dy = 0\n"
                                  "patrol_period = 2.0\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"guard\"\n"
                                  "pos = [100, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    Entity *guard = test_find_entity_by_blueprint(&game.state, "guard");
    TEST_ASSERT_NOT_NULL(guard);
    float start_x = guard->position.x;

    InputState idle = {0};

    /* 0.5s in (well within the first 1.0s half): moving in +patrol_dx. */
    test_advance_frames(&game, idle, 30);
    guard = test_find_entity_by_blueprint(&game.state, "guard");
    TEST_ASSERT_TRUE(guard->position.x > start_x);

    /* 1.0s in: the reversal point, near peak displacement. */
    test_advance_frames(&game, idle, 30);
    guard = test_find_entity_by_blueprint(&game.state, "guard");
    float peak_x = guard->position.x;

    /* 1.5s in (well within the second half): clearly reversed. */
    test_advance_frames(&game, idle, 30);
    guard = test_find_entity_by_blueprint(&game.state, "guard");
    TEST_ASSERT_TRUE(guard->position.x < peak_x);

    /* 2.0s in: one full period elapsed, back near the start. */
    test_advance_frames(&game, idle, 30);
    guard = test_find_entity_by_blueprint(&game.state, "guard");
    TEST_ASSERT_FLOAT_WITHIN(2.0F, start_x, guard->position.x);

    test_game_teardown(&game);
}

void test_integration_chase_within_aggro_moves_toward_player(void)
{
    /* Goblin at x=100, player at x=260 (distance 160), aggro_radius=200 --
     * the player is inside range, so the goblin should steer toward it. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"goblin\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"chase\"\n"
                                  "aggro_radius = 200\n"
                                  "speed = 60\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [400, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"hero\"\n"
                                  "pos = [260, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"goblin\"\n"
                                  "pos = [100, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    const Entity *player = game_get_player_const(&game.state);
    Entity *goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_NOT_NULL(goblin);
    float start_distance = fabsf(player->position.x - goblin->position.x);

    InputState idle = {0};
    test_advance_frames(&game, idle, 30);

    player = game_get_player_const(&game.state);
    goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    float end_distance = fabsf(player->position.x - goblin->position.x);
    TEST_ASSERT_TRUE(end_distance < start_distance);
    TEST_ASSERT_TRUE(goblin->moving);

    test_game_teardown(&game);
}

void test_integration_chase_outside_aggro_idle(void)
{
    /* Goblin at x=100, player at x=340 (distance 240), aggro_radius=50 --
     * the player is well outside range, so the goblin must stay put. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"goblin\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "behavior = \"chase\"\n"
                                  "aggro_radius = 50\n"
                                  "speed = 60\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [400, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"hero\"\n"
                                  "pos = [340, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"goblin\"\n"
                                  "pos = [100, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    Entity *goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    TEST_ASSERT_NOT_NULL(goblin);
    Vector2 start = goblin->position;

    InputState idle = {0};
    test_advance_frames(&game, idle, 30);

    goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    TEST_ASSERT_EQUAL_FLOAT(start.x, goblin->position.x);
    TEST_ASSERT_EQUAL_FLOAT(start.y, goblin->position.y);
    TEST_ASSERT_FALSE(goblin->moving);

    test_game_teardown(&game);
}

void test_integration_chase_respects_collision(void)
{
    /* Goblin at x=50, a solid wall at x=200 (32x32 collision), player at
     * x=340 -- well inside a generous aggro_radius. The goblin chases
     * right toward the player but the wall sits directly in its path, so
     * resolve_entity_obstacles (the same push-out the player itself uses)
     * must stop it at the wall's left edge instead of letting it pass
     * through to the player, exactly mirroring
     * test_integration_walk_and_collide's player-vs-rock assertion. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"hero\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"goblin\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [10, 10]\n"
                                  "behavior = \"chase\"\n"
                                  "aggro_radius = 300\n"
                                  "speed = 300\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"wall\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 32, 32]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [32, 32]\n"
                                  "solid = true\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [400, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"hero\"\n"
                                  "pos = [340, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"goblin\"\n"
                                  "pos = [50, 100]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"wall\"\n"
                                  "pos = [200, 100]\n";

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, gamedata));

    InputState idle = {0};
    test_advance_frames(&game, idle, 120);

    Entity *goblin = test_find_entity_by_blueprint(&game.state, "goblin");
    const Entity *wall = test_find_entity_by_blueprint(&game.state, "wall");
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_NOT_NULL(goblin);
    TEST_ASSERT_NOT_NULL(wall);
    TEST_ASSERT_NOT_NULL(player);

    Rectangle goblin_col = test_entity_collision_rect(&game.state, goblin);
    Rectangle wall_col = test_entity_collision_rect(&game.state, wall);
    TEST_ASSERT_TRUE(goblin_col.x + goblin_col.width <= wall_col.x + 0.1F);
    TEST_ASSERT_TRUE(goblin->position.x < player->position.x);

    test_game_teardown(&game);
}

/* ---- Integration: S6.10a combat damage core (hitbox/hurtbox, i-frames,
 * defeat). ACTION_ATTACK itself is S6.10b -- these tests activate a
 * hitbox directly by setting attack_state_timer as scenario setup (like
 * placing an entity), then drive real frames and assert only on
 * observable attrs (health, a defeat-triggered flag, position). */

static const char *combat_fixture_gamedata = "[[blueprint]]\n"
                                             "name = \"attacker\"\n"
                                             "texture = \"t.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "hitbox_offset_x = 0\n"
                                             "hitbox_offset_y = 0\n"
                                             "hitbox_w = 16\n"
                                             "hitbox_h = 16\n"
                                             "damage = 5\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"target\"\n"
                                             "texture = \"t.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "health = [10, 10]\n"
                                             "defense = 2\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"tanky_target\"\n"
                                             "texture = \"t.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "health = [10, 10]\n"
                                             "defense = 10\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"fragile_target\"\n"
                                             "texture = \"t.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "health = [3, 3]\n"
                                             "defense = 0\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"defeat\"\n"
                                             "actions = [\"add_attr:self.defeat_count,1\"]\n"
                                             "\n"
                                             "[[level]]\n"
                                             "name = \"test\"\n"
                                             "size = [320, 240]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"attacker\"\n"
                                             "pos = [100, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"target\"\n"
                                             "pos = [100, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"attacker\"\n"
                                             "pos = [300, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"tanky_target\"\n"
                                             "pos = [300, 100]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"attacker\"\n"
                                             "pos = [100, 300]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"fragile_target\"\n"
                                             "pos = [100, 300]\n";

/* Two independent attacker/target pairs, 200px apart so their hitboxes
 * never cross-contaminate the other pair's hurtbox: (100,100) has
 * damage=5 vs defense=2 (3 dealt, above the floor); (300,100) has
 * damage=5 vs defense=10 (would be negative, clamped to the max(1, ...)
 * floor). Both attacker/target pairs sit at the exact same position so
 * the hitbox and hurtbox rects (both centered on their own entity's
 * position) trivially overlap without needing separate distance math. */
void test_integration_damage_formula(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, combat_fixture_gamedata));

    Entity *attacker = test_find_entity_by_blueprint(&game.state, "attacker");
    TEST_ASSERT_NOT_NULL(attacker);
    attacker->attack_state_timer = 1.0F;

    Entity *tanky_attacker = &game.state.gamedata.current_level.entities.data[2];
    TEST_ASSERT_EQUAL_STRING("attacker", tanky_attacker->blueprint_name.ptr);
    tanky_attacker->attack_state_timer = 1.0F;

    InputState idle = {0};
    test_advance_frame(&game, idle);

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    const Entity *tanky_target = test_find_entity_by_blueprint(&game.state, "tanky_target");
    TEST_ASSERT_NOT_NULL(target);
    TEST_ASSERT_NOT_NULL(tanky_target);

    /* max(1, 5 - 2) = 3 dealt -> 10 - 3 = 7 */
    TEST_ASSERT_EQUAL_INT(7, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));
    /* max(1, 5 - 10) floors to 1 dealt -> 10 - 1 = 9 */
    TEST_ASSERT_EQUAL_INT(9, (int)attr_get_scoped_float(&tanky_target->attrs, nullptr, "health", -1.0F));

    test_game_teardown(&game);
}

/* Verified this test fails without the damage pass: with detect_melee_damage
 * temporarily no-op'd, both health reads above stay at their starting 10
 * instead of dropping to 7/9. */

void test_integration_iframes_block_repeat_damage(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, combat_fixture_gamedata));

    Entity *attacker = test_find_entity_by_blueprint(&game.state, "attacker");
    TEST_ASSERT_NOT_NULL(attacker);
    /* Comfortably longer than the whole test (about 1.2s of frames below)
     * so the hitbox never goes inactive on its own -- only i-frames are
     * under test here. */
    attacker->attack_state_timer = 10.0F;

    InputState idle = {0};
    test_advance_frame(&game, idle);

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    TEST_ASSERT_EQUAL_INT(7, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));

    /* Still well inside the default 0.8s i-frame window -- health must not
     * drop again even though the hitbox keeps overlapping the hurtbox. */
    test_advance_frames(&game, idle, 10);
    TEST_ASSERT_EQUAL_INT(7, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));

    /* Comfortably past the 0.8s i-frame window (60 more frames at 1/60s
     * is a full extra second) -- the hitbox is still active, so a second
     * hit must land. */
    test_advance_frames(&game, idle, 60);
    TEST_ASSERT_EQUAL_INT(4, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));

    test_game_teardown(&game);
}

void test_integration_defeat_fires_once(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, combat_fixture_gamedata));

    Entity *fragile_attacker = &game.state.gamedata.current_level.entities.data[4];
    TEST_ASSERT_EQUAL_STRING("attacker", fragile_attacker->blueprint_name.ptr);
    /* Stays active well past the whole test, same rationale as the
     * i-frames test above. fragile_target has no `death` clip, so once it
     * is defeated below, begin_death_state (S6.11b, D31) marks it `dying`
     * and tick_death_state soft-destroys it (`active` false) starting the
     * VERY NEXT frame -- deliberately not the same frame defeat fires, so
     * this test's own `defeat` rule still gets to run first (rules_
     * evaluate_batch, rule.c, skips inactive entities). So the 60-frame
     * run below now also relies on detect_melee_damage's own `active`
     * filter (not just the old_health > 0 gate) to keep defeat_count from
     * re-firing once fragile_target goes inactive. Either guard alone
     * would hold this invariant; exercising both is a strictly stronger
     * check than before S6.11b landed. See test_integration_death_state_
     * on_defeat for a dedicated check of the active-flag timing itself. */
    fragile_attacker->attack_state_timer = 10.0F;

    InputState idle = {0};
    test_advance_frame(&game, idle);

    Entity *fragile_target = test_find_entity_by_blueprint(&game.state, "fragile_target");
    TEST_ASSERT_NOT_NULL(fragile_target);
    /* max(1, 5 - 0) = 5 dealt -> 3 - 5 = -2, crossing zero this frame */
    TEST_ASSERT_EQUAL_INT(-2, (int)attr_get_scoped_float(&fragile_target->attrs, nullptr, "health", 1.0F));
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&fragile_target->attrs, nullptr, "defeat_count", 0.0F));

    /* Run well past the i-frame window (so the attacker's hitbox would
     * otherwise land on the target again) and confirm defeat_count stays
     * at exactly 1. */
    test_advance_frames(&game, idle, 60);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&fragile_target->attrs, nullptr, "defeat_count", 0.0F));

    test_game_teardown(&game);
}

/* ---- Integration: S6.10b ACTION_ATTACK input + directional melee hitbox
 * (Entity.facing, ATTACK_ACTIVE_SECONDS, ENTITY_HITBOX_REACH). Unlike
 * S6.10a's combat_fixture_gamedata above (attack_state_timer set
 * directly as scenario setup), these tests drive the real input layer:
 * one frame of held left-stick movement establishes player->facing (the
 * same mechanic update_player already uses for anim_row/flip), and a
 * fresh ACTION_ATTACK press in that same frame activates the hitbox --
 * update_player runs before update_player_attack inside behavior_player,
 * so the freshly-set facing is what the attack reads. S6.10a's unmodified
 * detect_melee_damage then applies (or doesn't apply) damage depending on
 * whether a given target sits inside the hitbox's facing-shifted rect.
 * hero's hitbox and both enemies' collision boxes are authored with a
 * centered offset (offset = -half_size) so their rects sit exactly on the
 * entity position before any reach shift, keeping the geometry exact:
 * hero at (160,120) facing left after one frame lands its hitbox center
 * at roughly (150.7,120) (ENTITY_HITBOX_REACH = 8px short of a full
 * frame of drift) -- enemy_front at (152,120) is well inside that box
 * (32x32 half-extents on both sides), enemy_behind at (200,120) is 40px
 * further right and well outside it. */
static const char *attack_fixture_gamedata = "[[blueprint]]\n"
                                             "name = \"hero\"\n"
                                             "texture = \"hero.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [-8, -8]\n"
                                             "collision_size = [16, 16]\n"
                                             "behavior = \"player\"\n"
                                             "speed = 80\n"
                                             "hitbox_offset_x = -16\n"
                                             "hitbox_offset_y = -16\n"
                                             "hitbox_w = 32\n"
                                             "hitbox_h = 32\n"
                                             "damage = 5\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"enemy_front\"\n"
                                             "texture = \"enemy.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [-16, -16]\n"
                                             "collision_size = [32, 32]\n"
                                             "health = [10, 10]\n"
                                             "defense = 0\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"enemy_behind\"\n"
                                             "texture = \"enemy.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [-16, -16]\n"
                                             "collision_size = [32, 32]\n"
                                             "health = [10, 10]\n"
                                             "defense = 0\n"
                                             "\n"
                                             "[[level]]\n"
                                             "name = \"test\"\n"
                                             "size = [320, 240]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"hero\"\n"
                                             "pos = [160, 120]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"enemy_front\"\n"
                                             "pos = [152, 120]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"enemy_behind\"\n"
                                             "pos = [200, 120]\n";

/* Facing LEFT (held one frame via the left stick) plus a fresh
 * ACTION_ATTACK press (gamepad west face / GAMEPAD_BUTTON_RIGHT_FACE_LEFT),
 * both in the same frame. enemy_front sits 8px to the hero's left, inside
 * the hitbox's facing-shifted reach -- max(1, 5-0) = 5 dealt, 10 -> 5.
 * Verified this test fails (health stays 10) with update_player_attack's
 * body temporarily no-op'd, i.e. ACTION_ATTACK never activating the
 * hitbox. */
void test_integration_attack_hits_enemy_in_front(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, attack_fixture_gamedata));

    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, -1.0F);
    input_state_press_gp_button(&input, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    test_advance_frame(&game, input);

    const Entity *enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    TEST_ASSERT_NOT_NULL(enemy_front);
    TEST_ASSERT_EQUAL_INT(5, (int)attr_get_scoped_float(&enemy_front->attrs, nullptr, "health", -1.0F));

    test_game_teardown(&game);
}

/* Same frame, same attack as above -- enemy_behind sits 40px to the
 * hero's RIGHT, opposite the LEFT facing the attack swings into, so the
 * shifted hitbox never reaches it. Health must stay at its starting 10.
 * Never hit, so "health" was never written onto the instance attrs by
 * entity_apply_damage -- read through entity_resolve_defaults so the
 * scoped lookup falls back to the blueprint's authored [10, 10]. */
void test_integration_attack_misses_out_of_arc(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, attack_fixture_gamedata));

    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, -1.0F);
    input_state_press_gp_button(&input, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    test_advance_frame(&game, input);

    const Entity *enemy_behind = test_find_entity_by_blueprint(&game.state, "enemy_behind");
    TEST_ASSERT_NOT_NULL(enemy_behind);
    const AttrSet *defaults = entity_resolve_defaults(&game.state, enemy_behind->id);
    TEST_ASSERT_EQUAL_INT(10, (int)attr_get_scoped_float(&enemy_behind->attrs, defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* Same LEFT-facing movement, no ACTION_ATTACK press: attack_state_timer
 * never leaves 0, so detect_melee_damage's own gate (S6.10a) skips the
 * hero entirely -- enemy_front, sitting exactly where the two tests
 * above land a swing, must take no damage at all. Same
 * entity_resolve_defaults rationale as the miss test above: never hit,
 * so "health" only resolves through the blueprint default. */
void test_integration_attack_requires_press(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, attack_fixture_gamedata));

    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, -1.0F);
    test_advance_frames(&game, input, 5);

    const Entity *enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    TEST_ASSERT_NOT_NULL(enemy_front);
    const AttrSet *defaults = entity_resolve_defaults(&game.state, enemy_front->id);
    TEST_ASSERT_EQUAL_INT(10, (int)attr_get_scoped_float(&enemy_front->attrs, defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* ---- Integration: S6.10c knockback + contact damage (D26). Knockback
 * reuses combat_fixture_gamedata's shape (attack_state_timer set
 * directly as scenario setup, same as the S6.10a tests) but with distinct
 * attacker/target positions -- S6.10a co-located its pairs on purpose to
 * keep the hitbox/hurtbox overlap math trivial, but a well-defined
 * attacker->target direction is the whole point here. attacker's hitbox
 * (32x32, offset 0) spans its own position to position+32 on both axes;
 * target's collision box (16x16, offset 0) at (110,100) sits entirely
 * inside that span, so the two overlap immediately without any movement. */
static const char *knockback_fixture_gamedata = "[[blueprint]]\n"
                                                "name = \"attacker\"\n"
                                                "texture = \"t.png\"\n"
                                                "src = [0, 0, 16, 16]\n"
                                                "hitbox_offset_x = 0\n"
                                                "hitbox_offset_y = 0\n"
                                                "hitbox_w = 32\n"
                                                "hitbox_h = 32\n"
                                                "damage = 5\n"
                                                "knockback = 40\n"
                                                "\n"
                                                "[[blueprint]]\n"
                                                "name = \"target\"\n"
                                                "texture = \"t.png\"\n"
                                                "src = [0, 0, 16, 16]\n"
                                                "collision_offset = [0, 0]\n"
                                                "collision_size = [16, 16]\n"
                                                "health = [20, 20]\n"
                                                "defense = 0\n"
                                                "\n"
                                                "[[level]]\n"
                                                "name = \"test\"\n"
                                                "size = [320, 240]\n"
                                                "\n"
                                                "[[level.entity]]\n"
                                                "blueprint = \"attacker\"\n"
                                                "pos = [100, 100]\n"
                                                "\n"
                                                "[[level.entity]]\n"
                                                "blueprint = \"target\"\n"
                                                "pos = [110, 100]\n";

/* A landed hit sets target->knockback_timer/knockback_velocity
 * (entity_apply_knockback) pointed straight along attacker->target
 * (+x here, since attacker sits at x=100 and target at x=110); the
 * knockback tick pass (game.c) then walks that impulse to a stop over
 * KNOCKBACK_SECONDS (0.15s). 20 frames at 1/60s is about 0.33s, well past
 * the decay window, so the read below is the impulse's final resting
 * position, not a mid-flight snapshot. Verified this test fails (target.x
 * unchanged) with entity_apply_knockback's body temporarily no-op'd. */
void test_integration_knockback_pushes_target_away(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, knockback_fixture_gamedata));

    Entity *attacker = test_find_entity_by_blueprint(&game.state, "attacker");
    TEST_ASSERT_NOT_NULL(attacker);
    attacker->attack_state_timer = 1.0F;

    const Entity *target_before = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target_before);
    Vector2 start = target_before->position;

    InputState idle = {0};
    test_advance_frames(&game, idle, 20);

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    TEST_ASSERT_TRUE(target->position.x - start.x > 5.0F);
    TEST_ASSERT_TRUE(fabsf(target->position.y - start.y) < 1.0F);

    test_game_teardown(&game);
}

/* Same attacker/target pair as above, plus a solid wall directly behind
 * the target (away from the attacker, i.e. further along +x): the wall's
 * left edge sits just 2px past the target's resting right edge, so an
 * unresolved 40px knockback would tunnel the target well past it, but
 * resolve_entity_obstacles (called every knockback tick, same as every
 * other mover in game.c) must stop the target right at the wall instead.
 * Verified this test fails (target's collision box ends up well past the
 * wall's left edge) with the knockback tick pass's resolve_entity_obstacles
 * call temporarily removed. */
static const char *knockback_wall_fixture_gamedata = "[[blueprint]]\n"
                                                     "name = \"attacker\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "hitbox_offset_x = 0\n"
                                                     "hitbox_offset_y = 0\n"
                                                     "hitbox_w = 32\n"
                                                     "hitbox_h = 32\n"
                                                     "damage = 5\n"
                                                     "knockback = 40\n"
                                                     "\n"
                                                     "[[blueprint]]\n"
                                                     "name = \"target\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 16]\n"
                                                     "health = [20, 20]\n"
                                                     "defense = 0\n"
                                                     "\n"
                                                     "[[blueprint]]\n"
                                                     "name = \"wall\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 48]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 48]\n"
                                                     "solid = true\n"
                                                     "\n"
                                                     "[[level]]\n"
                                                     "name = \"test\"\n"
                                                     "size = [320, 240]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"attacker\"\n"
                                                     "pos = [100, 100]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"target\"\n"
                                                     "pos = [110, 100]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"wall\"\n"
                                                     "pos = [128, 84]\n";

void test_integration_knockback_respects_wall(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, knockback_wall_fixture_gamedata));

    Entity *attacker = test_find_entity_by_blueprint(&game.state, "attacker");
    TEST_ASSERT_NOT_NULL(attacker);
    attacker->attack_state_timer = 1.0F;

    InputState idle = {0};
    test_advance_frames(&game, idle, 20);

    Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    const Entity *wall = test_find_entity_by_blueprint(&game.state, "wall");
    TEST_ASSERT_NOT_NULL(target);
    TEST_ASSERT_NOT_NULL(wall);

    Rectangle target_col = test_entity_collision_rect(&game.state, target);
    Rectangle wall_col = test_entity_collision_rect(&game.state, wall);
    TEST_ASSERT_TRUE(target_col.x + target_col.width <= wall_col.x + 0.1F);

    test_game_teardown(&game);
}

/* ---- Integration: S6.10c contact damage (D26). Two independent
 * hazard/victim pairs 200px apart, mirroring combat_fixture_gamedata's
 * layout above: (100,100) pairs a contact_damage hazard with
 * victim_touched to prove contact damage lands and respects i-frames;
 * (300,100) pairs a plain solid (no contact_damage attr) with victim_safe
 * to prove ordinary solids don't hurt anything they overlap. Both hazard
 * and victim share the exact same collision rect (offset [0,0], size
 * [16,16]) at the exact same position, so the body-vs-body overlap
 * detect_contact_damage tests is trivially true without any movement. */
static const char *contact_damage_fixture_gamedata = "[[blueprint]]\n"
                                                     "name = \"hazard\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 16]\n"
                                                     "contact_damage = true\n"
                                                     "damage = 3\n"
                                                     "\n"
                                                     "[[blueprint]]\n"
                                                     "name = \"innocent_wall\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 16]\n"
                                                     "solid = true\n"
                                                     "\n"
                                                     "[[blueprint]]\n"
                                                     "name = \"victim_touched\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 16]\n"
                                                     "health = [10, 10]\n"
                                                     "defense = 0\n"
                                                     "\n"
                                                     "[[blueprint]]\n"
                                                     "name = \"victim_safe\"\n"
                                                     "texture = \"t.png\"\n"
                                                     "src = [0, 0, 16, 16]\n"
                                                     "collision_offset = [0, 0]\n"
                                                     "collision_size = [16, 16]\n"
                                                     "health = [10, 10]\n"
                                                     "defense = 0\n"
                                                     "\n"
                                                     "[[level]]\n"
                                                     "name = \"test\"\n"
                                                     "size = [320, 240]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"hazard\"\n"
                                                     "pos = [100, 100]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"victim_touched\"\n"
                                                     "pos = [100, 100]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"innocent_wall\"\n"
                                                     "pos = [300, 100]\n"
                                                     "\n"
                                                     "[[level.entity]]\n"
                                                     "blueprint = \"victim_safe\"\n"
                                                     "pos = [300, 100]\n";

/* Same three-phase timing as test_integration_iframes_block_repeat_damage
 * above (1 frame, then 10 more inside the i-frame window, then 60 more
 * past it), proving detect_contact_damage's per-hit behavior matches
 * detect_melee_damage's exactly: max(1, 3-0) = 3 dealt per landed hit,
 * i-frames block the repeat while the hazard and victim stay overlapped
 * every single frame (no hitbox timer to expire -- contact_damage is a
 * static attr, so the source is live for the whole test). Verified this
 * test fails (health stays 10) with detect_contact_damage temporarily
 * no-op'd. */
void test_integration_contact_damage_hurts_on_touch(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, contact_damage_fixture_gamedata));

    InputState idle = {0};
    test_advance_frame(&game, idle);

    const Entity *victim = test_find_entity_by_blueprint(&game.state, "victim_touched");
    TEST_ASSERT_NOT_NULL(victim);
    /* max(1, 3 - 0) = 3 dealt -> 10 - 3 = 7 */
    TEST_ASSERT_EQUAL_INT(7, (int)attr_get_scoped_float(&victim->attrs, nullptr, "health", -1.0F));

    /* Still well inside the default 0.8s i-frame window -- health must not
     * drop again even though the hazard and victim stay overlapped every
     * frame. */
    test_advance_frames(&game, idle, 10);
    TEST_ASSERT_EQUAL_INT(7, (int)attr_get_scoped_float(&victim->attrs, nullptr, "health", -1.0F));

    /* Comfortably past the i-frame window -- contact is still live, so a
     * second hit must land. */
    test_advance_frames(&game, idle, 60);
    TEST_ASSERT_EQUAL_INT(4, (int)attr_get_scoped_float(&victim->attrs, nullptr, "health", -1.0F));

    test_game_teardown(&game);
}

/* innocent_wall has no contact_damage attr (just solid = true), so
 * detect_contact_damage's own gate must skip it entirely even though it
 * overlaps victim_safe for the whole test -- guards against a regression
 * where every solid entity accidentally hurts whatever it touches. */
void test_integration_no_contact_damage_without_attr(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, contact_damage_fixture_gamedata));

    InputState idle = {0};
    test_advance_frames(&game, idle, 30);

    const Entity *victim_safe = test_find_entity_by_blueprint(&game.state, "victim_safe");
    TEST_ASSERT_NOT_NULL(victim_safe);
    const AttrSet *defaults = entity_resolve_defaults(&game.state, victim_safe->id);
    TEST_ASSERT_EQUAL_INT(10, (int)attr_get_scoped_float(&victim_safe->attrs, defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* ---- Integration: S6.10d projectile behavior + spawn-based ranged
 * attacks (D26), completing S6.10. The "player" blueprint is the shooter:
 * its own on_spawn rule is "wait:0.5, spawn:projectile,200,200" -- the
 * ACTION_SPAWN handler's context.entity is therefore the player itself
 * (rule.c's execute_spawn_action), so the spawned projectile inherits
 * whatever direction the player was last facing when the wait elapsed
 * (S6.10d's facing-inheriting spawn). Only behavior_player ever updates
 * entity->facing (S6.10b), so every test below drives a few frames of
 * real left-stick movement first to establish it before going idle for
 * the rest of the 0.5s wait. "target" sits 100px to the right of the
 * fixed spawn point (300,200 vs the spawn's 200,200), so only a
 * rightward-facing shot can ever reach it. */
static const char *projectile_fixture_gamedata = "[[blueprint]]\n"
                                                 "name = \"player\"\n"
                                                 "texture = \"player.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "behavior = \"player\"\n"
                                                 "speed = 80\n"
                                                 "\n"
                                                 "[[blueprint.rule]]\n"
                                                 "trigger = \"on_spawn\"\n"
                                                 "actions = [\"wait:0.5\", \"spawn:projectile,200,200\"]\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"target\"\n"
                                                 "texture = \"rock.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "health = [20, 20]\n"
                                                 "defense = 0\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"projectile\"\n"
                                                 "texture = \"rock.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "behavior = \"projectile\"\n"
                                                 "contact_damage = true\n"
                                                 "damage = 5\n"
                                                 "destroy_on_hit = true\n"
                                                 "speed = 300\n"
                                                 "projectile_lifetime = 0.5\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"test\"\n"
                                                 "size = [400, 400]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"player\"\n"
                                                 "pos = [200, 200]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"target\"\n"
                                                 "pos = [300, 200]\n";

/* Drives `facing_axis` on the left stick for 10 frames -- enough for
 * update_player (S6.10b) to set the player's entity->facing to a cardinal
 * unit vector -- then idles until either the "projectile" blueprint entity
 * appears (spawned by the player's own on_spawn rule once its 0.5s wait
 * elapses, S6.10d) or max_frames elapses. Returns the projectile, or
 * nullptr if it never spawned. Vector2 (not two adjacent floats) avoids
 * bugprone-easily-swappable-parameters, same rationale as walk_player_to's
 * own parameter-ordering comment above. */
static Entity *drive_facing_and_wait_for_projectile(TestGame *game, Vector2 facing_axis, int max_frames)
{
    InputState face = {0};
    input_state_set_gp_axis(&face, GAMEPAD_AXIS_LEFT_X, facing_axis.x);
    input_state_set_gp_axis(&face, GAMEPAD_AXIS_LEFT_Y, facing_axis.y);
    test_advance_frames(game, face, 10);

    InputState idle = {0};
    for (int frame = 0; frame < max_frames; frame++) {
        test_advance_frame(game, idle);
        Entity *projectile = test_find_entity_by_blueprint(&game->state, "projectile");
        if (projectile) {
            return projectile;
        }
    }
    return nullptr;
}

/* Faces the shooter right (toward the target, 100px away), waits for the
 * spawn, and asserts two things a no-op behavior_projectile would fail:
 * the projectile's x position increases over a few more frames (straight-
 * line movement along entity->facing), and the target's health eventually
 * drops (the projectile reaches it and lands a contact-damage hit through
 * the existing S6.10c pass). Verified to fail (no movement, no hit) with
 * behavior_projectile's body temporarily replaced by an early return. */
void test_integration_projectile_flies_and_hits(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, projectile_fixture_gamedata));

    Entity *projectile = drive_facing_and_wait_for_projectile(&game, (Vector2){1.0F, 0.0F}, 40);
    TEST_ASSERT_NOT_NULL_MESSAGE(projectile, "projectile never spawned");
    float spawn_x = projectile->position.x;

    InputState idle = {0};
    test_advance_frames(&game, idle, 5);
    projectile = test_find_entity_by_blueprint(&game.state, "projectile");
    TEST_ASSERT_NOT_NULL(projectile);
    TEST_ASSERT_TRUE_MESSAGE(projectile->position.x > spawn_x, "projectile did not move in its facing direction");

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    const AttrSet *target_defaults = entity_resolve_defaults(&game.state, target->id);

    bool hit = false;
    for (int frame = 0; frame < 60; frame++) {
        test_advance_frame(&game, idle);
        target = test_find_entity_by_blueprint(&game.state, "target");
        if (attr_get_scoped_float(&target->attrs, target_defaults, "health", 20.0F) < 20.0F) {
            hit = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(hit, "projectile never reached the target");
    /* max(1, 5 - 0) = 5 dealt -> 20 - 5 = 15 */
    TEST_ASSERT_EQUAL_INT(15, (int)attr_get_scoped_float(&target->attrs, target_defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* Same setup as the flies-and-hits test above, but continues well past the
 * default 0.8s (48-frame) i-frame window after the hit lands. Asserts the
 * projectile itself is inactive (destroy_on_hit soft-destroyed it) AND
 * that the target's health stays put -- if destroy_on_hit only set the
 * flag without detect_contact_damage's attacker-side active gate (game.c)
 * also honoring it, a projectile parked on top of the target would resume
 * dealing damage the instant i-frames lapse. */
void test_integration_projectile_destroyed_on_hit(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, projectile_fixture_gamedata));

    Entity *projectile = drive_facing_and_wait_for_projectile(&game, (Vector2){1.0F, 0.0F}, 40);
    TEST_ASSERT_NOT_NULL_MESSAGE(projectile, "projectile never spawned");

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    const AttrSet *target_defaults = entity_resolve_defaults(&game.state, target->id);

    InputState idle = {0};
    bool hit = false;
    for (int frame = 0; frame < 60; frame++) {
        test_advance_frame(&game, idle);
        target = test_find_entity_by_blueprint(&game.state, "target");
        if (attr_get_scoped_float(&target->attrs, target_defaults, "health", 20.0F) < 20.0F) {
            hit = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(hit, "projectile never reached the target");
    float health_after_hit = attr_get_scoped_float(&target->attrs, target_defaults, "health", -1.0F);

    projectile = test_find_entity_by_blueprint(&game.state, "projectile");
    TEST_ASSERT_NOT_NULL(projectile);
    const AttrSet *projectile_defaults = entity_resolve_defaults(&game.state, projectile->id);
    TEST_ASSERT_FALSE_MESSAGE(attr_get_scoped_bool(&projectile->attrs, projectile_defaults, "active", true),
                              "destroy_on_hit did not soft-destroy the projectile");

    test_advance_frames(&game, idle, 60);
    target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_EQUAL_FLOAT(health_after_hit, attr_get_scoped_float(&target->attrs, target_defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* Firing AWAY from the target (left stick left, facing = (-1, 0)) sends
 * the projectile from (200,200) off to the left -- the target's
 * collision box (300..316, 200..216) is never in its path, at any frame
 * count, so its health must never move off the blueprint's default 20. */
void test_integration_projectile_misses(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, projectile_fixture_gamedata));

    Entity *projectile = drive_facing_and_wait_for_projectile(&game, (Vector2){-1.0F, 0.0F}, 40);
    TEST_ASSERT_NOT_NULL_MESSAGE(projectile, "projectile never spawned");

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    const AttrSet *target_defaults = entity_resolve_defaults(&game.state, target->id);

    InputState idle = {0};
    test_advance_frames(&game, idle, 90);
    target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_EQUAL_INT(20, (int)attr_get_scoped_float(&target->attrs, target_defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* Fired into empty space (facing left, same as the miss test above, but
 * asserted on the projectile's own state instead of the target's): past
 * the fixture's 0.5s (30-frame) projectile_lifetime counted from the
 * projectile's own first update, it must have soft-destroyed itself
 * (active = false) and then stayed frozen in place on every later frame --
 * not just flagged inactive while still sliding along under some other
 * code path. */
void test_integration_projectile_expires(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, projectile_fixture_gamedata));

    Entity *projectile = drive_facing_and_wait_for_projectile(&game, (Vector2){-1.0F, 0.0F}, 40);
    TEST_ASSERT_NOT_NULL_MESSAGE(projectile, "projectile never spawned");

    InputState idle = {0};
    test_advance_frames(&game, idle, 45);
    projectile = test_find_entity_by_blueprint(&game.state, "projectile");
    TEST_ASSERT_NOT_NULL(projectile);
    const AttrSet *projectile_defaults = entity_resolve_defaults(&game.state, projectile->id);
    TEST_ASSERT_FALSE_MESSAGE(attr_get_scoped_bool(&projectile->attrs, projectile_defaults, "active", true),
                              "projectile did not expire after its lifetime elapsed");

    float position_after_expiry = projectile->position.x;
    test_advance_frames(&game, idle, 20);
    projectile = test_find_entity_by_blueprint(&game.state, "projectile");
    TEST_ASSERT_NOT_NULL(projectile);
    TEST_ASSERT_EQUAL_FLOAT(position_after_expiry, projectile->position.x);

    test_game_teardown(&game);
}

/* ---- Integration: S6.11a/D31 data-driven animation state machine. Two
 * [[blueprint.animation]] clips on the "player" blueprint reproduce the
 * pre-D31 hardcoded ANIM_WALK_DOWN/SIDE/UP layout: a walk clip (row = 3,
 * frames = 6, speed = 10) and an idle clip (row = 3, frames = 1, speed = 0,
 * i.e. holds the standing frame of whichever row the walk clip left
 * `direction` on). Behaviors set the built-in `state`/`direction` attrs;
 * advance_entity_animation (game.c) reads them every frame to derive
 * anim_row/flip/frame_index -- these tests assert on that render-consumed
 * state, not on any internal plumbing (state/direction attrs, anim_row,
 * frame_index are all values draw_animated_entity, main.c, reads
 * directly). */
static const char *anim_fixture_gamedata = "[[blueprint]]\n"
                                           "name = \"player\"\n"
                                           "texture = \"player.png\"\n"
                                           "src = [0, 0, 32, 32]\n"
                                           "collision_offset = [-5, 6]\n"
                                           "collision_size = [10, 10]\n"
                                           "behavior = \"player\"\n"
                                           "speed = 80\n"
                                           "\n"
                                           "[[blueprint.animation]]\n"
                                           "state = \"walk\"\n"
                                           "row = 3\n"
                                           "frames = 6\n"
                                           "speed = 10\n"
                                           "\n"
                                           "[[blueprint.animation]]\n"
                                           "state = \"idle\"\n"
                                           "row = 3\n"
                                           "frames = 1\n"
                                           "speed = 0\n"
                                           "\n"
                                           "[[level]]\n"
                                           "name = \"test\"\n"
                                           "size = [320, 240]\n"
                                           "\n"
                                           "[[level.entity]]\n"
                                           "blueprint = \"player\"\n"
                                           "pos = [160, 120]\n";

/* Drives one frame of each cardinal direction and asserts anim_row =
 * clip.row (3) + the direction's row offset (down = 0, side = 1, up = 2),
 * with flip only for "left". Fails (anim_row stuck at its zero default) if
 * advance_entity_animation is ever a no-op -- verified by temporarily
 * commenting out its call site in game_update. */
void test_integration_anim_walk_direction_rows(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, anim_fixture_gamedata));

    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frame(&game, right);
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("right", attr_get_string(&player->attrs, "direction"));
    TEST_ASSERT_EQUAL_INT(4, player->anim_row);
    TEST_ASSERT_FALSE(player->flip);

    InputState left = {0};
    input_state_set_gp_axis(&left, GAMEPAD_AXIS_LEFT_X, -1.0F);
    test_advance_frame(&game, left);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("left", attr_get_string(&player->attrs, "direction"));
    TEST_ASSERT_EQUAL_INT(4, player->anim_row);
    TEST_ASSERT_TRUE(player->flip);

    InputState face_up = {0};
    input_state_set_gp_axis(&face_up, GAMEPAD_AXIS_LEFT_Y, -1.0F);
    test_advance_frame(&game, face_up);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("up", attr_get_string(&player->attrs, "direction"));
    TEST_ASSERT_EQUAL_INT(5, player->anim_row);
    TEST_ASSERT_FALSE(player->flip);

    InputState down = {0};
    input_state_set_gp_axis(&down, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    test_advance_frame(&game, down);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("down", attr_get_string(&player->attrs, "direction"));
    TEST_ASSERT_EQUAL_INT(3, player->anim_row);
    TEST_ASSERT_FALSE(player->flip);

    test_game_teardown(&game);
}

/* Walk clip speed = 10 frames/sec at the fixed 1/60s test delta_time =>
 * frame_timer wraps (and frame_index advances) every 6 frames. Checkpoints
 * at 1 (not yet wrapped), 6 (wrapped once), and 36 (six full wraps, back to
 * 0) prove frame_index cycles 0..5..0 rather than jumping or free-running.
 * Switching to no input afterward resolves to the idle clip (frames = 1),
 * which must hold frame 0 regardless of where the walk cycle left off. */
void test_integration_anim_frame_progression(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, anim_fixture_gamedata));

    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);

    test_advance_frame(&game, right);
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_INT(0, player->frame_index);

    test_advance_frames(&game, right, 5);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_INT(1, player->frame_index);

    test_advance_frames(&game, right, 30);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_INT(0, player->frame_index);

    InputState idle = {0};
    test_advance_frame(&game, idle);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_INT(0, player->frame_index);

    test_game_teardown(&game);
}

/* Moving sets state = "walk"; releasing input the very next frame sets
 * state = "idle" and holds the standing frame (frame_index resets to 0,
 * matching the idle clip's frames = 1 authoring) instead of freezing
 * mid-stride. */
void test_integration_anim_state_switch(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, anim_fixture_gamedata));

    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    test_advance_frames(&game, right, 6);
    const Entity *player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("walk", attr_get_string(&player->attrs, "state"));
    TEST_ASSERT_TRUE(player->moving);
    TEST_ASSERT_EQUAL_INT(1, player->frame_index);

    InputState idle = {0};
    test_advance_frame(&game, idle);
    player = game_get_player_const(&game.state);
    TEST_ASSERT_EQUAL_STRING("idle", attr_get_string(&player->attrs, "state"));
    TEST_ASSERT_FALSE(player->moving);
    TEST_ASSERT_EQUAL_INT(0, player->frame_index);

    test_game_teardown(&game);
}

/* ---- Integration: S6.11b/D31 combat animation integration, completing
 * S6.11 -- the attack clip's own frame_index now drives the hitbox's
 * active window (replacing the fixed ATTACK_STATE_DEFAULT_SECONDS-only
 * gate), and damage/defeat drive hurt/death animation states with
 * priority over walk/idle (resolve_effective_anim_state, game.c). */

/* Same hero/enemy_front geometry as attack_fixture_gamedata above (hero at
 * (160,120), collision/hitbox centered on position, enemy_front 8px to the
 * hero's left once ENTITY_HITBOX_REACH shifts a LEFT-facing swing) --
 * see that fixture's own comment for the exact overlap math, unaffected by
 * this test's one extra frame of drift from the press-frame's movement.
 * hero's `attack` clip (frames = 3, speed = 6) plays over frames/speed =
 * 0.5s = 30 frames at the fixed 1/60s test delta_time, wrapping frame_index
 * every 10 of those frames (frame_timer accumulates delta_time * speed =
 * 0.1 per frame, wrapping at 1.0): frame_index == 0 for frames 1-9 (1-
 * indexed from the press), == 1 for frames 10-19, == 2 for frames 20-29,
 * back to 0 (and attack_state_timer back to 0, ending the attack) at frame
 * 30. `attack_hit_frame_start`/`_end` = [1, 1] means only frame_index == 1
 * -- a 10-frame window, comfortably shorter than the target's default
 * 48-frame i-frame window -- has an active hitbox, unlike the S6.10b
 * fixture above where the window defaults to the whole clip. The narrow
 * window (versus the long i-frame window) guarantees at most one hit can
 * ever land in this test, not just "no damage yet". */
static const char *attack_window_fixture_gamedata = "[[blueprint]]\n"
                                                    "name = \"hero\"\n"
                                                    "texture = \"hero.png\"\n"
                                                    "src = [0, 0, 32, 32]\n"
                                                    "collision_offset = [-8, -8]\n"
                                                    "collision_size = [16, 16]\n"
                                                    "behavior = \"player\"\n"
                                                    "speed = 80\n"
                                                    "hitbox_offset_x = -16\n"
                                                    "hitbox_offset_y = -16\n"
                                                    "hitbox_w = 32\n"
                                                    "hitbox_h = 32\n"
                                                    "damage = 5\n"
                                                    "attack_hit_frame_start = 1\n"
                                                    "attack_hit_frame_end = 1\n"
                                                    "\n"
                                                    "[[blueprint.animation]]\n"
                                                    "state = \"attack\"\n"
                                                    "row = 0\n"
                                                    "frames = 3\n"
                                                    "speed = 6\n"
                                                    "\n"
                                                    "[[blueprint]]\n"
                                                    "name = \"enemy_front\"\n"
                                                    "texture = \"enemy.png\"\n"
                                                    "src = [0, 0, 32, 32]\n"
                                                    "collision_offset = [-16, -16]\n"
                                                    "collision_size = [32, 32]\n"
                                                    "health = [10, 10]\n"
                                                    "defense = 0\n"
                                                    "\n"
                                                    "[[level]]\n"
                                                    "name = \"test\"\n"
                                                    "size = [320, 240]\n"
                                                    "\n"
                                                    "[[level.entity]]\n"
                                                    "blueprint = \"hero\"\n"
                                                    "pos = [160, 120]\n"
                                                    "\n"
                                                    "[[level.entity]]\n"
                                                    "blueprint = \"enemy_front\"\n"
                                                    "pos = [152, 120]\n";

/* Presses ACTION_ATTACK once (facing left, same as attack_fixture_gamedata's
 * own attack tests), then drives idle frames past each frame_index
 * checkpoint: no damage on the press frame itself (frame_index == 0, the
 * clip's very first frame, outside [1, 1]), still no damage right up to the
 * frame_index == 1 wrap at frame 10, damage lands once inside the
 * frame_index == 1 window, and no further damage once the attack's own
 * 30-frame duration has long elapsed (attack_state_timer back at 0).
 * Verified this test fails two ways: forcing entity_hitbox_is_active to
 * always return true makes the "no damage on the press frame" assertion
 * fail (health drops to 5 immediately instead of staying 10), and forcing
 * it to always return false makes the "damage lands" assertion fail
 * (health stays 10 forever). */
void test_integration_attack_hitbox_frame_window(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, attack_window_fixture_gamedata));

    InputState attack_input = {0};
    input_state_set_gp_axis(&attack_input, GAMEPAD_AXIS_LEFT_X, -1.0F);
    input_state_press_gp_button(&attack_input, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    test_advance_frame(&game, attack_input);

    const Entity *enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    TEST_ASSERT_NOT_NULL(enemy_front);
    const AttrSet *enemy_defaults = entity_resolve_defaults(&game.state, enemy_front->id);
    TEST_ASSERT_EQUAL_INT(10, (int)attr_get_scoped_float(&enemy_front->attrs, enemy_defaults, "health", -1.0F));

    InputState idle = {0};
    test_advance_frames(&game, idle, 8);
    enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    enemy_defaults = entity_resolve_defaults(&game.state, enemy_front->id);
    TEST_ASSERT_EQUAL_INT(10, (int)attr_get_scoped_float(&enemy_front->attrs, enemy_defaults, "health", -1.0F));

    test_advance_frames(&game, idle, 6);
    enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    enemy_defaults = entity_resolve_defaults(&game.state, enemy_front->id);
    /* max(1, 5 - 0) = 5 dealt -> 10 - 5 = 5 */
    TEST_ASSERT_EQUAL_INT(5, (int)attr_get_scoped_float(&enemy_front->attrs, enemy_defaults, "health", -1.0F));

    test_advance_frames(&game, idle, 60);
    enemy_front = test_find_entity_by_blueprint(&game.state, "enemy_front");
    enemy_defaults = entity_resolve_defaults(&game.state, enemy_front->id);
    TEST_ASSERT_EQUAL_INT(5, (int)attr_get_scoped_float(&enemy_front->attrs, enemy_defaults, "health", -1.0F));

    test_game_teardown(&game);
}

/* Co-located attacker/target pair (same trivial-overlap shape as
 * combat_fixture_gamedata above), neither authoring any
 * [[blueprint.animation]] clip -- exercises begin_hurt_state's
 * HURT_STATE_DEFAULT_SECONDS (0.3s = 18 frames) fallback since target has
 * no `hurt` clip. */
static const char *hurt_state_fixture_gamedata = "[[blueprint]]\n"
                                                 "name = \"attacker\"\n"
                                                 "texture = \"t.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "hitbox_offset_x = 0\n"
                                                 "hitbox_offset_y = 0\n"
                                                 "hitbox_w = 16\n"
                                                 "hitbox_h = 16\n"
                                                 "damage = 5\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"target\"\n"
                                                 "texture = \"t.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "health = [10, 10]\n"
                                                 "defense = 0\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"test\"\n"
                                                 "size = [320, 240]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"attacker\"\n"
                                                 "pos = [100, 100]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"target\"\n"
                                                 "pos = [100, 100]\n";

/* Activates the attacker's hitbox directly (scenario setup, same pattern
 * as the S6.10a tests above), lands one hit, then checks the target's
 * `hurt_state_timer` field across checkpoints -- resolve_effective_anim_
 * state (game.c) resolves to "hurt" if and only if `!dying &&
 * hurt_state_timer > 0`, and target's health never crosses zero here (10
 * -> 5), so `dying` stays false throughout and hurt_state_timer > 0 is an
 * exact proxy for "effective state is hurt", the same way existing tests
 * already assert on `moving`/`frame_index` directly rather than through
 * an attr. One frame after the hit lands, hurt_state_timer is still
 * positive (the hit landed AFTER that same frame's own tick_combat_timers
 * already ran, so it isn't ticked down until the NEXT frame); well past
 * the 18-frame default hurt duration (but still inside the target's
 * 48-frame default i-frame window, so no second hit muddies the picture)
 * it has decayed back to 0. Verified this test fails (hurt_state_timer
 * stays 0 throughout) with begin_hurt_state's body temporarily replaced
 * by a no-op. */
void test_integration_hurt_state_on_damage(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, hurt_state_fixture_gamedata));

    Entity *attacker = test_find_entity_by_blueprint(&game.state, "attacker");
    TEST_ASSERT_NOT_NULL(attacker);
    attacker->attack_state_timer = 10.0F;

    InputState idle = {0};
    test_advance_frame(&game, idle);

    const Entity *target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_NOT_NULL(target);
    /* max(1, 5 - 0) = 5 dealt -> 10 - 5 = 5 */
    TEST_ASSERT_EQUAL_INT(5, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));

    test_advance_frame(&game, idle);
    target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_TRUE_MESSAGE(target->hurt_state_timer > 0.0F,
                             "target should be in the hurt state right after taking a hit");

    test_advance_frames(&game, idle, 40);
    target = test_find_entity_by_blueprint(&game.state, "target");
    TEST_ASSERT_FALSE_MESSAGE(target->hurt_state_timer > 0.0F, "hurt state should have expired by now");
    TEST_ASSERT_EQUAL_INT(5, (int)attr_get_scoped_float(&target->attrs, nullptr, "health", -1.0F));

    test_game_teardown(&game);
}

/* Two independent attacker/target pairs 200px apart, mirroring
 * combat_fixture_gamedata's own layout: "dying_target" authors a `death`
 * clip (frames = 3, speed = 6 -> 0.5s = 30 frames), "vanish_target"
 * authors none. Both start at health = [3, 3] against damage = 5 so a
 * single hit defeats them outright (3 - 5 = -2). */
static const char *death_state_fixture_gamedata = "[[blueprint]]\n"
                                                  "name = \"attacker\"\n"
                                                  "texture = \"t.png\"\n"
                                                  "src = [0, 0, 16, 16]\n"
                                                  "hitbox_offset_x = 0\n"
                                                  "hitbox_offset_y = 0\n"
                                                  "hitbox_w = 16\n"
                                                  "hitbox_h = 16\n"
                                                  "damage = 5\n"
                                                  "\n"
                                                  "[[blueprint]]\n"
                                                  "name = \"dying_target\"\n"
                                                  "texture = \"t.png\"\n"
                                                  "src = [0, 0, 16, 16]\n"
                                                  "collision_offset = [0, 0]\n"
                                                  "collision_size = [16, 16]\n"
                                                  "health = [3, 3]\n"
                                                  "defense = 0\n"
                                                  "\n"
                                                  "[[blueprint.animation]]\n"
                                                  "state = \"death\"\n"
                                                  "row = 0\n"
                                                  "frames = 3\n"
                                                  "speed = 6\n"
                                                  "\n"
                                                  "[[blueprint]]\n"
                                                  "name = \"vanish_target\"\n"
                                                  "texture = \"t.png\"\n"
                                                  "src = [0, 0, 16, 16]\n"
                                                  "collision_offset = [0, 0]\n"
                                                  "collision_size = [16, 16]\n"
                                                  "health = [3, 3]\n"
                                                  "defense = 0\n"
                                                  "\n"
                                                  "[[level]]\n"
                                                  "name = \"test\"\n"
                                                  "size = [320, 240]\n"
                                                  "\n"
                                                  "[[level.entity]]\n"
                                                  "blueprint = \"attacker\"\n"
                                                  "pos = [100, 100]\n"
                                                  "\n"
                                                  "[[level.entity]]\n"
                                                  "blueprint = \"dying_target\"\n"
                                                  "pos = [100, 100]\n"
                                                  "\n"
                                                  "[[level.entity]]\n"
                                                  "blueprint = \"attacker\"\n"
                                                  "pos = [300, 100]\n"
                                                  "\n"
                                                  "[[level.entity]]\n"
                                                  "blueprint = \"vanish_target\"\n"
                                                  "pos = [300, 100]\n";

/* Defeats both targets in the same frame (both attackers' hitboxes
 * activated directly as scenario setup, mirroring test_integration_damage_
 * formula's use of raw entity-array indices to reach the second
 * "attacker" instance). Both targets must still be ACTIVE the very frame
 * they are defeated -- tick_death_state (game.c) deliberately deactivates
 * on the NEXT frame's tick, never synchronously in the defeat frame
 * itself, so that a target's own `defeat` rule (rules_evaluate_batch,
 * rule.c, skips inactive entities) still gets to run that same frame; see
 * test_integration_defeat_fires_once for that rule-still-fires guarantee.
 * From the frame after, dying_target (has a `death` clip) stays active
 * and shows state == "death" while its clip plays, whereas vanish_target
 * (no `death` clip) is already inactive -- its death_state_timer started
 * at 0, so the very next tick_death_state call deactivates it. dying_target
 * finally deactivates too once its clip's 30-frame duration elapses.
 * Verified this test fails (dying_target's active flag drops on the same
 * schedule as vanish_target's, one frame earlier than expected) with
 * begin_death_state's clip-lookup branch temporarily removed (always
 * taking the "no death clip" path). `dying` is asserted directly (same
 * footing as reading `moving`/`frame_index` in the existing anim tests)
 * rather than through resolve_effective_anim_state's derived "death"
 * label -- `dying` is exactly the flag that label keys off, with top
 * priority over every other combat/movement state. */
void test_integration_death_state_on_defeat(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, death_state_fixture_gamedata));

    Entity *attacker_near = &game.state.gamedata.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_STRING("attacker", attacker_near->blueprint_name.ptr);
    attacker_near->attack_state_timer = 10.0F;

    Entity *attacker_far = &game.state.gamedata.current_level.entities.data[2];
    TEST_ASSERT_EQUAL_STRING("attacker", attacker_far->blueprint_name.ptr);
    attacker_far->attack_state_timer = 10.0F;

    InputState idle = {0};
    test_advance_frame(&game, idle);

    const Entity *dying_target = test_find_entity_by_blueprint(&game.state, "dying_target");
    TEST_ASSERT_NOT_NULL(dying_target);
    const AttrSet *dying_defaults = entity_resolve_defaults(&game.state, dying_target->id);
    TEST_ASSERT_TRUE_MESSAGE(attr_get_scoped_bool(&dying_target->attrs, dying_defaults, "active", true),
                             "entity with a death clip should stay active the frame it is defeated");

    const Entity *vanish_target = test_find_entity_by_blueprint(&game.state, "vanish_target");
    TEST_ASSERT_NOT_NULL(vanish_target);
    const AttrSet *vanish_defaults = entity_resolve_defaults(&game.state, vanish_target->id);
    TEST_ASSERT_TRUE_MESSAGE(attr_get_scoped_bool(&vanish_target->attrs, vanish_defaults, "active", true),
                             "entity with no death clip should still stay active the frame it is defeated, so its "
                             "own defeat rule gets to run");

    test_advance_frame(&game, idle);
    dying_target = test_find_entity_by_blueprint(&game.state, "dying_target");
    dying_defaults = entity_resolve_defaults(&game.state, dying_target->id);
    TEST_ASSERT_TRUE_MESSAGE(dying_target->dying, "entity with a death clip should be marked dying");
    TEST_ASSERT_TRUE_MESSAGE(attr_get_scoped_bool(&dying_target->attrs, dying_defaults, "active", true),
                             "entity with a death clip should stay active while it plays");

    vanish_target = test_find_entity_by_blueprint(&game.state, "vanish_target");
    vanish_defaults = entity_resolve_defaults(&game.state, vanish_target->id);
    TEST_ASSERT_FALSE_MESSAGE(attr_get_scoped_bool(&vanish_target->attrs, vanish_defaults, "active", true),
                              "entity with no death clip should deactivate the frame after defeat");

    test_advance_frames(&game, idle, 40);
    dying_target = test_find_entity_by_blueprint(&game.state, "dying_target");
    dying_defaults = entity_resolve_defaults(&game.state, dying_target->id);
    TEST_ASSERT_FALSE_MESSAGE(attr_get_scoped_bool(&dying_target->attrs, dying_defaults, "active", true),
                              "entity should deactivate once its death clip finishes");

    test_game_teardown(&game);
}

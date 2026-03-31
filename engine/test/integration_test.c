#include "unity.h"
#include "engine_context.h"

static struct EngineContext ctx;

#include "entity.h"
#include "game.h"

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
                                      "\n"
                                      "[[blueprint]]\n"
                                      "name = \"tall_tree\"\n"
                                      "texture = \"tree.png\"\n"
                                      "src = [0, 0, 32, 48]\n"
                                      "collision_offset = [8, 32]\n"
                                      "collision_size = [16, 12]\n"
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

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

void test_integration_load_gamedata(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));

    bool loaded = game_load_gamedata(&ctx, &state,
                                     (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_TRUE(state.gamedata_loaded);
    TEST_ASSERT_EQUAL_STRING("field", state.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(3, state.current_level.entities.count);
    TEST_ASSERT_EQUAL_INT(3, state.blueprints.entries.count);
    TEST_ASSERT_TRUE(state.player_index >= 0);

    game_free(&ctx, &state);
}

void test_integration_load_specific_level(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){160, 120}));

    bool loaded = game_load_gamedata(
        &ctx, &state,
        (GamedataParams){.toml_string = fixture_gamedata, .level_name = "cave", .texture_lookup = dummy_lookup});
    TEST_ASSERT_TRUE(loaded);
    TEST_ASSERT_EQUAL_STRING("cave", state.current_level.name.ptr);
    TEST_ASSERT_EQUAL_INT(2, state.current_level.entities.count);
    TEST_ASSERT_TRUE(state.player_index >= 0);

    game_free(&ctx, &state);
}

void test_integration_walk_and_collide(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Player starts at (160, 120), rock at (200, 120) with 16x16 collision.
     * Walk right into the rock. */
    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 300; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }

    /* Player collision must not overlap the rock */
    const Entity *player = game_get_player_const(&state);
    /* Rock is entity index 1 (player is 0) */
    Rectangle rock = state.current_level.entities.data[1].collision;
    TEST_ASSERT_TRUE(player->collision.x + player->collision.width <= rock.x + 0.1F);

    game_free(&ctx, &state);
}

void test_integration_walk_freely(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    const Entity *player = game_get_player_const(&state);
    float start_x = player->position.x;
    float start_y = player->position.y;

    /* Walk down-left for 30 frames (away from obstacles) */
    InputState input = {0};
    input.left_stick.x = -0.5F;
    input.left_stick.y = 0.5F;

    for (int iteration = 0; iteration < 30; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }

    player = game_get_player_const(&state);
    TEST_ASSERT_TRUE(player->position.x < start_x);
    TEST_ASSERT_TRUE(player->position.y > start_y);
    TEST_ASSERT_EQUAL_INT(30, state.frame);

    game_free(&ctx, &state);
}

void test_integration_boundary_all_directions(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    float half = FRAME_SIZE / 2.0F;
    InputState input = {0};

    /* Push against each wall */
    input.left_stick.x = -1.0F;
    input.left_stick.y = 0.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&state)->position.x);

    input.left_stick.x = 0.0F;
    input.left_stick.y = -1.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, game_get_player_const(&state)->position.y);

    input.left_stick.x = 1.0F;
    input.left_stick.y = 0.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 320.0F - half, game_get_player_const(&state)->position.x);

    input.left_stick.x = 0.0F;
    input.left_stick.y = 1.0F;
    for (int iteration = 0; iteration < 500; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 240.0F - half, game_get_player_const(&state)->position.y);

    game_free(&ctx, &state);
}

void test_integration_player_entity_spawns(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){640, 360}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_gamedata, .texture_lookup = dummy_lookup}));

    /* Player entity must exist */
    TEST_ASSERT_TRUE(state.player_index >= 0);

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
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }
    TEST_ASSERT_TRUE(game_get_player_const(&state)->position.x > start_x);

    game_free(&ctx, &state);
}

void test_integration_on_spawn_trigger_fires_on_load(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_triggers, .texture_lookup = dummy_lookup}));

    /* beacon blueprint has on_spawn → set_flag:beacon_spawned.
     * No game_update needed — the flag must be set by game_load_gamedata. */
    TEST_ASSERT_TRUE(flag_get(&state.flags, "beacon_spawned"));

    game_free(&ctx, &state);
}

void test_integration_enter_trigger_fires_on_overlap(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &ctx, &state, (GamedataParams){.toml_string = fixture_triggers, .texture_lookup = dummy_lookup}));

    /* zone_entered must not be set before the player reaches the zone */
    TEST_ASSERT_FALSE(flag_get(&state.flags, "zone_entered"));

    /* Player at (100,100), collision [100,100,16,16]. Zone at (200,100), collision [200,100,32,32].
     * Player right edge starts at 116, zone left edge at 200. Gap = 84px.
     * Speed = 80 px/s → need ~63 frames at 1/60s. Run 80 to be safe. */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 80; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }

    TEST_ASSERT_TRUE(flag_get(&state.flags, "zone_entered"));

    game_free(&ctx, &state);
}

void test_integration_enter_trigger_fires_only_once(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&ctx, &state, (RectU32){320, 240}));

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
        &ctx, &state, (GamedataParams){.toml_string = fixture_enter_count, .texture_lookup = dummy_lookup}));

    /* Walk into zone and keep walking through it for 200 frames total */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    for (int iteration = 0; iteration < 200; iteration++) {
        game_update(&ctx, &state, input, 1.0F / 60.0F);
    }

    /* enter_count must be exactly 1 — edge-triggered, not level-triggered */
    const Entity *zone = &state.current_level.entities.data[1];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&zone->attrs, NULL, "enter_count", 0.0F));

    game_free(&ctx, &state);
}

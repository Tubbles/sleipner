#include "unity.h"
#include "game.h"
#include "diag.h"

static const char *game_test_gamedata = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [-5, 6]\n"
                                        "collision_size = [10, 10]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"test\"\n"
                                        "size = [320, 240]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [160, 120]\n";

static const char *game_test_gamedata_with_obstacle = "[[blueprint]]\n"
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
                                                      "[[level]]\n"
                                                      "name = \"test\"\n"
                                                      "size = [320, 240]\n"
                                                      "\n"
                                                      "[[level.entity]]\n"
                                                      "blueprint = \"player\"\n"
                                                      "pos = [160, 120]\n"
                                                      "\n"
                                                      "[[level.entity]]\n"
                                                      "blueprint = \"rock\"\n"
                                                      "pos = [170, 120]\n";

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

void test_game_init_defaults(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    RectU32 bounds = {320, 240};
    TEST_ASSERT_TRUE(game_init(&diag, &state, bounds));

    TEST_ASSERT_EQUAL_INT(320, state.game_bounds.width);
    TEST_ASSERT_EQUAL_INT(240, state.game_bounds.height);
    TEST_ASSERT_EQUAL_INT(-1, state.gamedata.player_index);
    TEST_ASSERT_EQUAL_INT(0, state.frame);
    TEST_ASSERT_FALSE(state.gamedata_loaded);
    TEST_ASSERT_TRUE(state.debug_enabled);

    game_free(&diag, &state);
}

void test_game_update_increments_frame(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    InputState input = {0};
    game_update(&diag, &state, input, 1.0F / 60.0F);
    TEST_ASSERT_EQUAL_INT(1, state.frame);

    game_update(&diag, &state, input, 1.0F / 60.0F);
    TEST_ASSERT_EQUAL_INT(2, state.frame);

    game_free(&diag, &state);
}

void test_game_update_accumulates_elapsed(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    InputState input = {0};
    game_update(&diag, &state, input, 0.5F);
    game_update(&diag, &state, input, 0.25F);

    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.75F, state.elapsed);

    game_free(&diag, &state);
}

void test_game_update_player_moves_right(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = game_test_gamedata, .texture_lookup = dummy_lookup}));

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);
    float start_x = player->position.x;

    InputState input = {0};
    input.left_stick.x = 1.0F;

    game_update(&diag, &state, input, 1.0F / 60.0F);

    player = game_get_player_const(&state);
    TEST_ASSERT_TRUE(player->position.x > start_x);
    TEST_ASSERT_TRUE(player->moving);
    TEST_ASSERT_EQUAL_INT(ANIM_WALK_SIDE, player->anim_row);
    TEST_ASSERT_FALSE(player->flip);

    game_free(&diag, &state);
}

void test_game_update_player_moves_left(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = game_test_gamedata, .texture_lookup = dummy_lookup}));

    InputState input = {0};
    input.left_stick.x = -1.0F;

    game_update(&diag, &state, input, 1.0F / 60.0F);

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_TRUE(player->moving);
    TEST_ASSERT_EQUAL_INT(ANIM_WALK_SIDE, player->anim_row);
    TEST_ASSERT_TRUE(player->flip);

    game_free(&diag, &state);
}

void test_game_update_no_input_no_movement(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = game_test_gamedata, .texture_lookup = dummy_lookup}));

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);
    float start_x = player->position.x;
    float start_y = player->position.y;

    InputState input = {0};
    game_update(&diag, &state, input, 1.0F / 60.0F);

    player = game_get_player_const(&state);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, start_x, player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, start_y, player->position.y);
    TEST_ASSERT_FALSE(player->moving);

    game_free(&diag, &state);
}

void test_game_player_clamps_to_bounds(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = game_test_gamedata, .texture_lookup = dummy_lookup}));

    InputState input = {0};
    input.left_stick.x = -1.0F;

    for (int iteration = 0; iteration < 1000; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    const Entity *player = game_get_player_const(&state);
    float half = FRAME_SIZE / 2.0F;
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, player->position.x);

    game_free(&diag, &state);
}

void test_game_player_collision_from_blueprint(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = game_test_gamedata, .texture_lookup = dummy_lookup}));

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);

    /* Player at (160, 120) with collision_offset [-5, 6] and size [10, 10] */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 155.0F, player->collision.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 126.0F, player->collision.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, player->collision.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, player->collision.height);

    game_free(&diag, &state);
}

void test_game_update_resolves_obstacle_collision(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state,
        (GamedataParams){.toml_string = game_test_gamedata_with_obstacle, .texture_lookup = dummy_lookup}));

    /* Push player into the obstacle */
    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 200; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* Player collision should not overlap the obstacle */
    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_TRUE(player->collision.x + player->collision.width <= 170.0F + 0.1F);

    game_free(&diag, &state);
}

/* Large level: 640x480, larger than the 320x240 viewport */
static const char *camera_large_level = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [-5, 6]\n"
                                        "collision_size = [10, 10]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"test\"\n"
                                        "size = [640, 480]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [320, 240]\n";

/* Small level: 160x128, smaller than the 320x240 viewport */
static const char *camera_small_level = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [-5, 6]\n"
                                        "collision_size = [10, 10]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"test\"\n"
                                        "size = [160, 128]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [80, 64]\n";

void test_camera_follows_player(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = camera_large_level, .texture_lookup = dummy_lookup}));

    float initial_x = state.gamedata.camera_target.x;

    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 60; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    TEST_ASSERT_TRUE(state.gamedata.camera_target.x > initial_x);

    game_free(&diag, &state);
}

void test_camera_clamped_to_level_bounds(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = camera_large_level, .texture_lookup = dummy_lookup}));

    /* Push player to bottom-right for many frames */
    InputState input = {0};
    input.left_stick.x = 1.0F;
    input.left_stick.y = 1.0F;

    for (int iteration = 0; iteration < 1000; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* Camera must not exceed level_width - viewport_width/2 on X */
    float max_camera_x = 640.0F - 320.0F / 2.0F;
    float max_camera_y = 480.0F - 240.0F / 2.0F;
    TEST_ASSERT_FLOAT_WITHIN(1.0F, max_camera_x, state.gamedata.camera_target.x);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, max_camera_y, state.gamedata.camera_target.y);

    game_free(&diag, &state);
}

void test_camera_centers_small_level(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = camera_small_level, .texture_lookup = dummy_lookup}));

    /* Move player around — camera should stay centered on the level */
    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 60; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* Level is 160x128, smaller than viewport 320x240 — camera locks to level center */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, state.gamedata.camera_target.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 64.0F, state.gamedata.camera_target.y);

    game_free(&diag, &state);
}

void test_camera_snaps_on_load(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = camera_large_level, .texture_lookup = dummy_lookup}));

    /* Player at (320, 240), camera should snap there immediately (clamped) */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 320.0F, state.gamedata.camera_target.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 240.0F, state.gamedata.camera_target.y);

    game_free(&diag, &state);
}

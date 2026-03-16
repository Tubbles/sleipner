#include "unity.h"
#include "game.h"

void test_game_init_defaults(void)
{
    GameState state;
    RectU32 bounds = {320, 240};
    game_init(&state, bounds);

    TEST_ASSERT_EQUAL_INT(320, state.game_bounds.width);
    TEST_ASSERT_EQUAL_INT(240, state.game_bounds.height);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 160.0F, state.player.position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, state.player.position.y);
    TEST_ASSERT_EQUAL_INT(ANIM_IDLE_DOWN, state.player.anim_row);
    TEST_ASSERT_EQUAL_INT(0, state.frame);
    TEST_ASSERT_FALSE(state.gamedata_loaded);
    TEST_ASSERT_TRUE(state.debug_enabled);

    game_free(&state);
}

void test_game_update_increments_frame(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    InputState input = {0};
    game_update(&state, input, 1.0F / 60.0F);
    TEST_ASSERT_EQUAL_INT(1, state.frame);

    game_update(&state, input, 1.0F / 60.0F);
    TEST_ASSERT_EQUAL_INT(2, state.frame);

    game_free(&state);
}

void test_game_update_accumulates_elapsed(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    InputState input = {0};
    game_update(&state, input, 0.5F);
    game_update(&state, input, 0.25F);

    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.75F, state.elapsed);

    game_free(&state);
}

void test_game_update_player_moves_right(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    float start_x = state.player.position.x;
    InputState input = {0};
    input.left_stick.x = 1.0F;

    game_update(&state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(state.player.position.x > start_x);
    TEST_ASSERT_TRUE(state.player.moving);
    TEST_ASSERT_EQUAL_INT(ANIM_WALK_SIDE, state.player.anim_row);
    TEST_ASSERT_FALSE(state.player.flip);

    game_free(&state);
}

void test_game_update_player_moves_left(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    InputState input = {0};
    input.left_stick.x = -1.0F;

    game_update(&state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(state.player.moving);
    TEST_ASSERT_EQUAL_INT(ANIM_WALK_SIDE, state.player.anim_row);
    TEST_ASSERT_TRUE(state.player.flip);

    game_free(&state);
}

void test_game_update_no_input_no_movement(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    float start_x = state.player.position.x;
    float start_y = state.player.position.y;
    InputState input = {0};

    game_update(&state, input, 1.0F / 60.0F);

    TEST_ASSERT_FLOAT_WITHIN(0.01F, start_x, state.player.position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, start_y, state.player.position.y);
    TEST_ASSERT_FALSE(state.player.moving);

    game_free(&state);
}

void test_game_player_clamps_to_bounds(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    InputState input = {0};
    input.left_stick.x = -1.0F;

    for (int iteration = 0; iteration < 1000; iteration++) {
        game_update(&state, input, 1.0F / 60.0F);
    }

    float half = FRAME_SIZE / 2.0F;
    TEST_ASSERT_FLOAT_WITHIN(0.1F, half, state.player.position.x);

    game_free(&state);
}

void test_game_player_hitbox_position(void)
{
    Player player = {.position = {100.0F, 100.0F}};
    Rectangle hitbox = game_player_hitbox(&player);

    TEST_ASSERT_FLOAT_WITHIN(0.1F, 95.0F, hitbox.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 106.0F, hitbox.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, hitbox.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, hitbox.height);
}

void test_game_update_resolves_obstacle_collision(void)
{
    GameState state;
    game_init(&state, (RectU32){320, 240});

    /* Place an obstacle directly to the right of the player */
    state.current_level.entity_count = 1;
    state.current_level.entities[0].collision = (Rectangle){170.0F, 100.0F, 32.0F, 32.0F};

    /* Push player into the obstacle */
    InputState input = {0};
    input.left_stick.x = 1.0F;

    for (int iteration = 0; iteration < 200; iteration++) {
        game_update(&state, input, 1.0F / 60.0F);
    }

    /* Player hitbox should not overlap the obstacle */
    Rectangle hitbox = game_player_hitbox(&state.player);
    TEST_ASSERT_TRUE(hitbox.x + hitbox.width <= 170.0F + 0.1F);

    game_free(&state);
}

#include "unity.h"
#include "game.h"
#include "diag.h"

/* game_test_gamedata's walk clip authors row = 3 (down); side (left/right)
 * is row + 1 -- the pre-D31 ANIM_WALK_SIDE constant's value, now derived
 * from blueprint data instead of a hardcoded enum (S6.11a, D31). */
#define TEST_PLAYER_WALK_ROW_SIDE 4

/* Walk row 3 (down), 4 (side), 5 (up), 6 frames at speed 10 (S6.11a, D31)
 * -- the pre-D31 ANIM_WALK_DOWN/SIDE/UP layout, now blueprint data instead
 * of a hardcoded constant. Idle holds the standing frame of the faced row
 * (frames = 1). */
static const char *game_test_gamedata = "[[blueprint]]\n"
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
                                                      "solid = true\n"
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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = 1.0F;

    game_update(&diag, &state, input, 1.0F / 60.0F);

    player = game_get_player_const(&state);
    TEST_ASSERT_TRUE(player->position.x > start_x);
    TEST_ASSERT_TRUE(player->moving);
    TEST_ASSERT_EQUAL_INT(TEST_PLAYER_WALK_ROW_SIDE, player->anim_row);
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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = -1.0F;

    game_update(&diag, &state, input, 1.0F / 60.0F);

    const Entity *player = game_get_player_const(&state);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_TRUE(player->moving);
    TEST_ASSERT_EQUAL_INT(TEST_PLAYER_WALK_ROW_SIDE, player->anim_row);
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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = -1.0F;

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
    const AttrSet *defaults = entity_resolve_defaults(&state, player->id);
    Rectangle col = entity_collision_rect(player, defaults);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 155.0F, col.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 126.0F, col.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, col.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, col.height);

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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = 1.0F;

    for (int iteration = 0; iteration < 200; iteration++) {
        game_update(&diag, &state, input, 1.0F / 60.0F);
    }

    /* Player collision should not overlap the obstacle */
    const Entity *player = game_get_player_const(&state);
    const AttrSet *player_defaults = entity_resolve_defaults(&state, player->id);
    Rectangle player_col = entity_collision_rect(player, player_defaults);
    TEST_ASSERT_TRUE(player_col.x + player_col.width <= 170.0F + 0.1F);

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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = 1.0F;

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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = 1.0F;
    input.gp_axis[GAMEPAD_AXIS_LEFT_Y] = 1.0F;

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
    input.gp_axis[GAMEPAD_AXIS_LEFT_X] = 1.0F;

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

/* --- camera_pan_position / camera_shake_magnitude (S6.5, D22/D26) ---
 * Pure math, no GameState needed -- these are the two helpers
 * camera_update_target's pan-override branch and game_update's shake-offset
 * computation call every frame (game.c). */

void test_camera_pan_position_at_start_is_from(void)
{
    Vector2 from = {100.0F, 50.0F};
    Vector2 target = {300.0F, 150.0F};
    Vector2 result = camera_pan_position(from, target, 0.0F, 1.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, from.x, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, from.y, result.y);
}

void test_camera_pan_position_at_half_duration_is_midpoint(void)
{
    Vector2 from = {100.0F, 50.0F};
    Vector2 target = {300.0F, 150.0F};
    Vector2 result = camera_pan_position(from, target, 0.5F, 1.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 200.0F, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 100.0F, result.y);
}

void test_camera_pan_position_at_duration_is_target(void)
{
    Vector2 from = {100.0F, 50.0F};
    Vector2 target = {300.0F, 150.0F};
    Vector2 result = camera_pan_position(from, target, 1.0F, 1.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, target.x, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, target.y, result.y);
}

void test_camera_pan_position_clamps_past_duration(void)
{
    Vector2 from = {100.0F, 50.0F};
    Vector2 target = {300.0F, 150.0F};
    Vector2 result = camera_pan_position(from, target, 5.0F, 1.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, target.x, result.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, target.y, result.y);
}

void test_camera_shake_magnitude_full_at_zero_elapsed(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 10.0F, camera_shake_magnitude(10.0F, 0.0F, 1.0F));
}

void test_camera_shake_magnitude_half_at_half_duration(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 5.0F, camera_shake_magnitude(10.0F, 0.5F, 1.0F));
}

void test_camera_shake_magnitude_zero_at_and_past_duration(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0F, camera_shake_magnitude(10.0F, 1.0F, 1.0F));
    TEST_ASSERT_EQUAL_FLOAT(0.0F, camera_shake_magnitude(10.0F, 5.0F, 1.0F));
}

void test_camera_shake_magnitude_never_negative(void)
{
    TEST_ASSERT_TRUE(camera_shake_magnitude(10.0F, 0.9F, 1.0F) >= 0.0F);
    TEST_ASSERT_TRUE(camera_shake_magnitude(10.0F, 100.0F, 1.0F) >= 0.0F);
}

/* --- transition_fade_tick / transition_fade_alpha (S6.14, D27) ---
 * Pure state machine, no GameState needed -- frame.c's handle_transition
 * is the only production caller. */

void test_transition_fade_tick_none_is_idle(void)
{
    TransitionFadeStep step =
        transition_fade_tick((TransitionFade){.phase = TRANSITION_FADE_NONE, .timer = 0.0F}, 0.1F);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_NONE, step.fade.phase);
    TEST_ASSERT_FALSE(step.do_swap);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, step.fade.timer);
}

void test_transition_fade_tick_out_counts_down(void)
{
    TransitionFadeStep step =
        transition_fade_tick((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = TRANSITION_FADE_SECONDS}, 0.1F);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_OUT, step.fade.phase);
    TEST_ASSERT_FALSE(step.do_swap);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, TRANSITION_FADE_SECONDS - 0.1F, step.fade.timer);
}

void test_transition_fade_tick_out_reaching_zero_swaps_into_fade_in(void)
{
    TransitionFadeStep step =
        transition_fade_tick((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = 0.05F}, 0.1F);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_IN, step.fade.phase);
    TEST_ASSERT_TRUE(step.do_swap);
    TEST_ASSERT_EQUAL_FLOAT(TRANSITION_FADE_SECONDS, step.fade.timer);
}

void test_transition_fade_tick_in_reaching_zero_returns_to_none(void)
{
    TransitionFadeStep step = transition_fade_tick((TransitionFade){.phase = TRANSITION_FADE_IN, .timer = 0.05F}, 0.1F);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_NONE, step.fade.phase);
    TEST_ASSERT_FALSE(step.do_swap);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, step.fade.timer);
}

/* Full round trip at a realistic 1/60s frame step: FADE_OUT -> exactly
 * one do_swap -> FADE_IN -> NONE. This is the D27 guarantee ("0.3s out,
 * swap, 0.3s in") expressed as a state-machine property rather than a
 * single sample. */
void test_transition_fade_tick_full_cycle_swaps_once_then_returns_to_none(void)
{
    TransitionFade fade = {.phase = TRANSITION_FADE_OUT, .timer = TRANSITION_FADE_SECONDS};
    float delta_time = 1.0F / 60.0F;
    int swap_count = 0;
    int max_frames = 120;
    for (int frame = 0; frame < max_frames && fade.phase != TRANSITION_FADE_NONE; frame++) {
        TransitionFadeStep step = transition_fade_tick(fade, delta_time);
        if (step.do_swap) {
            swap_count++;
        }
        fade = step.fade;
    }
    TEST_ASSERT_EQUAL_INT(1, swap_count);
    TEST_ASSERT_EQUAL_INT(TRANSITION_FADE_NONE, fade.phase);
}

void test_transition_fade_alpha_zero_when_none(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0F,
                            transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_NONE, .timer = 0.0F}));
}

void test_transition_fade_alpha_zero_at_fade_out_start(void)
{
    TEST_ASSERT_EQUAL_FLOAT(
        0.0F, transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = TRANSITION_FADE_SECONDS}));
}

void test_transition_fade_alpha_one_at_fade_out_end(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0F, transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = 0.0F}));
}

/* The swap instant is FADE_IN's very first tick: do_swap fires with the
 * timer reset to TRANSITION_FADE_SECONDS -- alpha must read fully black
 * at that exact (phase, timer) pair, matching the do_swap-instant
 * guarantee above. */
void test_transition_fade_alpha_one_at_swap_instant(void)
{
    TEST_ASSERT_EQUAL_FLOAT(
        1.0F, transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_IN, .timer = TRANSITION_FADE_SECONDS}));
}

void test_transition_fade_alpha_zero_at_fade_in_end(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0F, transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_IN, .timer = 0.0F}));
}

/* step_count integer-driven steps (rather than a float loop counter) to
 * satisfy bugprone-float-loop-counter/clang-analyzer FloatLoopCounter --
 * the float timer value is derived inside the loop body instead. */
void test_transition_fade_alpha_monotonic_across_fade_out(void)
{
    float previous =
        transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = TRANSITION_FADE_SECONDS});
    int step_count = 6;
    for (int step = 1; step <= step_count; step++) {
        float timer = TRANSITION_FADE_SECONDS - ((float)step * 0.05F);
        float alpha = transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = timer});
        TEST_ASSERT_TRUE(alpha >= previous);
        previous = alpha;
    }
}

void test_transition_fade_alpha_monotonic_across_fade_in(void)
{
    float previous =
        transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_IN, .timer = TRANSITION_FADE_SECONDS});
    int step_count = 6;
    for (int step = 1; step <= step_count; step++) {
        float timer = TRANSITION_FADE_SECONDS - ((float)step * 0.05F);
        float alpha = transition_fade_alpha((TransitionFade){.phase = TRANSITION_FADE_IN, .timer = timer});
        TEST_ASSERT_TRUE(alpha <= previous);
        previous = alpha;
    }
}

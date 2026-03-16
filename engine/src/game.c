#include "game.h"

#include "raylib.h"

#include <math.h>
#include <string.h>

#define GAMEDATA_ARENA_SIZE (64 * 1024)

void game_init(GameState *state, RectU32 game_bounds)
{
    memset(state, 0, sizeof(*state));
    state->game_bounds = game_bounds;
    state->player.position = (Vector2){(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F};
    state->player.anim_row = ANIM_IDLE_DOWN;
    state->debug_enabled = true;
    arena_init(&state->gamedata_arena, GAMEDATA_ARENA_SIZE);
}

Rectangle game_player_hitbox(const Player *player)
{
    return (Rectangle){player->position.x - 5, player->position.y + 6, 10, 10};
}

static void update_player(Player *player, InputState input, float delta_time, RectU32 bounds)
{
    player->moving = false;

    if (input.left_stick.x != 0.0F || input.left_stick.y != 0.0F) {
        player->position.x += input.left_stick.x * PLAYER_SPEED * delta_time;
        player->position.y += input.left_stick.y * PLAYER_SPEED * delta_time;
        player->moving = true;

        if (fabsf(input.left_stick.x) > fabsf(input.left_stick.y)) {
            player->anim_row = ANIM_WALK_SIDE;
            player->flip = input.left_stick.x < 0.0F;
        } else if (input.left_stick.y > 0.0F) {
            player->anim_row = ANIM_WALK_DOWN;
        } else {
            player->anim_row = ANIM_WALK_UP;
        }
    }

    /* Clamp to game bounds */
    float half = FRAME_SIZE / 2.0F;
    if (player->position.x < half) {
        player->position.x = half;
    }
    if (player->position.y < half) {
        player->position.y = half;
    }
    if (player->position.x > (float)bounds.width - half) {
        player->position.x = (float)bounds.width - half;
    }
    if (player->position.y > (float)bounds.height - half) {
        player->position.y = (float)bounds.height - half;
    }

    /* Animate walk cycle */
    if (player->moving) {
        player->frame_timer += delta_time * ANIM_SPEED;
        if (player->frame_timer >= 1.0F) {
            player->frame_timer -= 1.0F;
            player->frame_index = (player->frame_index + 1) % WALK_FRAMES;
        }
    } else {
        player->frame_index = 0;
        player->frame_timer = 0.0F;
    }
}

static void resolve_player_obstacles(Player *player, const LevelEntity *obstacles, int count)
{
    for (int index = 0; index < count; index++) {
        Rectangle hitbox = game_player_hitbox(player);
        Rectangle obstacle = obstacles[index].collision;
        if (!CheckCollisionRecs(hitbox, obstacle)) {
            continue;
        }
        float push_left = (hitbox.x + hitbox.width) - obstacle.x;
        float push_right = (obstacle.x + obstacle.width) - hitbox.x;
        float push_up = (hitbox.y + hitbox.height) - obstacle.y;
        float push_down = (obstacle.y + obstacle.height) - hitbox.y;

        float min_push = push_left;
        int direction = 0;
        if (push_right < min_push) {
            min_push = push_right;
            direction = 1;
        }
        if (push_up < min_push) {
            min_push = push_up;
            direction = 2;
        }
        if (push_down < min_push) {
            min_push = push_down;
            direction = 3;
        }

        if (direction == 0) {
            player->position.x -= push_left;
        } else if (direction == 1) {
            player->position.x += push_right;
        } else if (direction == 2) {
            player->position.y -= push_up;
        } else {
            player->position.y += push_down;
        }
    }
}

void game_update(GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;

    update_player(&state->player, input, delta_time, state->game_bounds);
    resolve_player_obstacles(&state->player, state->current_level.entities, state->current_level.entity_count);
}

void game_free(GameState *state)
{
    arena_free(&state->gamedata_arena);
}

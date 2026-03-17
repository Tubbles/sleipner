#include "game.h"
#include "arena.h"
#include "blueprint.h"
#include "entity.h"
#include "input.h"
#include "level.h"
#include "rect.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GAMEDATA_ARENA_SIZE (64 * 1024)

void game_init(GameState *state, RectU32 game_bounds)
{
    memset(state, 0, sizeof(*state));
    state->game_bounds = game_bounds;
    state->player_index = -1;
    state->debug_enabled = true;
    arena_init(&state->gamedata_arena, GAMEDATA_ARENA_SIZE);
}

static int find_player_entity(const Level *level)
{
    for (int index = 0; index < level->entity_count; index++) {
        const char *behavior = entity_get_string(&level->entities[index], "behavior", "");
        if (strcmp(behavior, "player") == 0) {
            return index;
        }
    }
    return -1;
}

bool game_load_gamedata(GameState *state,
                        const char *toml_string,
                        const char *level_name,
                        TextureLookupFn texture_lookup,
                        void *texture_user_data)
{
    size_t length = strlen(toml_string);
    char *buffer = malloc(length + 1);
    if (!buffer) {
        return false;
    }
    memcpy(buffer, toml_string, length + 1);

    char errbuf[200];
    toml_table_t *root = toml_parse(buffer, errbuf, (int)sizeof(errbuf));
    free(buffer);

    if (!root) {
        return false;
    }

    arena_reset(&state->gamedata_arena);
    blueprints_load(&state->blueprints, root, &state->gamedata_arena);

    bool level_ok =
        level_load(&state->current_level, root, level_name, &state->blueprints, texture_lookup, texture_user_data);

    toml_free(root);
    state->gamedata_loaded = level_ok;

    if (level_ok) {
        state->player_index = find_player_entity(&state->current_level);
    }

    return level_ok;
}

Entity *game_get_player(GameState *state)
{
    if (state->player_index < 0 || state->player_index >= state->current_level.entity_count) {
        return NULL;
    }
    return &state->current_level.entities[state->player_index];
}

const Entity *game_get_player_const(const GameState *state)
{
    if (state->player_index < 0 || state->player_index >= state->current_level.entity_count) {
        return NULL;
    }
    return &state->current_level.entities[state->player_index];
}

static void update_player(Entity *player, InputState input, float delta_time, RectU32 bounds)
{
    player->moving = false;

    float speed = entity_get_float(player, "speed", DEFAULT_PLAYER_SPEED);

    if (input.left_stick.x != 0.0F || input.left_stick.y != 0.0F) {
        player->position.x += input.left_stick.x * speed * delta_time;
        player->position.y += input.left_stick.y * speed * delta_time;
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

    entity_update_collision(player);
}

static void resolve_player_obstacles(Entity *player, Entity *entities, int count, int player_index)
{
    for (int index = 0; index < count; index++) {
        if (index == player_index || !entities[index].solid) {
            continue;
        }
        Rectangle hitbox = player->collision;
        Rectangle obstacle = entities[index].collision;
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

        entity_update_collision(player);
    }
}

void game_update(GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;

    Entity *player = game_get_player(state);
    if (player) {
        update_player(player, input, delta_time, state->game_bounds);
        resolve_player_obstacles(player, state->current_level.entities, state->current_level.entity_count,
                                 state->player_index);
    }
}

void game_free(GameState *state)
{
    arena_free(&state->gamedata_arena);
}

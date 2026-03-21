#include "game.h"
#include "arena.h"
#include "blueprint.h"
#include "debug.h"
#include "error.h"
#include "entity.h"
#include "input.h"
#include "level.h"
#include "rect.h"
#include "rule.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GAMEDATA_ARENA_SIZE (64UL * 1024)
#define TOML_ERRBUF_SIZE 200
#define MAX_COLLECT_EVENTS 32

bool game_init(struct EngineContext *ctx, GameState *state, RectU32 game_bounds)
{
    memset(state, 0, sizeof(*state));
    state->game_bounds = game_bounds;
    state->player_index = -1;
    state->debug_enabled = true;
    if (!arena_init(ctx, &state->gamedata_arena, GAMEDATA_ARENA_SIZE)) {
        error_wrap(ctx, "game_init");
        return false;
    }
    return true;
}

static int find_player_entity(const Level *level)
{
    for (int index = 0; index < level->entity_count; index++) {
        const char *behavior = entity_get_string(&level->entities[index], "behavior");
        if (behavior && strcmp(behavior, "player") == 0) {
            return index;
        }
    }
    return -1;
}

bool game_load_gamedata(struct EngineContext *ctx, GameState *state, GamedataParams params)
{
    size_t length = strlen(params.toml_string);
    char *buffer = malloc(length + 1);
    if (!buffer) {
        error_set(ctx, "malloc failed for TOML buffer (%zu bytes)", length + 1);
        error_wrap(ctx, "game_load_gamedata");
        return false;
    }
    memcpy(buffer, params.toml_string, length + 1);

    char errbuf[TOML_ERRBUF_SIZE];
    toml_table_t *root = toml_parse(buffer, errbuf, (int)sizeof(errbuf));
    free(buffer);

    if (!root) {
        error_set(ctx, "toml_parse: %s", errbuf);
        error_wrap(ctx, "game_load_gamedata");
        return false;
    }

    arena_reset(&state->gamedata_arena);
    blueprints_load(ctx, &state->blueprints, root, &state->gamedata_arena);

    bool level_ok = level_load(ctx, &state->current_level, root, params.level_name, &state->blueprints,
                               params.texture_lookup, params.texture_user_data);

    toml_free(root);
    state->gamedata_loaded = level_ok;

    if (level_ok) {
        state->player_index = find_player_entity(&state->current_level);
    } else {
        error_wrap(ctx, "game_load_gamedata");
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

static void resolve_player_obstacles(Level *level, int player_index)
{
    Entity *player = &level->entities[player_index];
    for (int index = 0; index < level->entity_count; index++) {
        if (index == player_index || !level->entities[index].solid) {
            continue;
        }
        Rectangle hitbox = player->collision;
        Rectangle obstacle = level->entities[index].collision;
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

static void update_child_positions(Level *level)
{
    for (int index = 0; index < level->entity_count; index++) {
        Entity *entity = &level->entities[index];
        if (entity->parent_index < 0) {
            continue;
        }
        const Entity *parent = &level->entities[entity->parent_index];
        entity->position.x = parent->position.x + entity->offset.x;
        entity->position.y = parent->position.y + entity->offset.y;
        entity_update_collision(entity);
    }
}

static int detect_interact_targets(struct EngineContext *ctx,
                                   const Entity *player,
                                   int player_index,
                                   const Entity *entities,
                                   int entity_count,
                                   TriggerEvent *out_events,
                                   int max_events)
{
    int count = 0;
    for (int index = 0; index < entity_count && count < max_events; index++) {
        if (index == player_index || !entities[index].active) {
            continue;
        }
        float delta_x = entities[index].position.x - player->position.x;
        float delta_y = entities[index].position.y - player->position.y;
        float distance_sq = (delta_x * delta_x) + (delta_y * delta_y);
        if (distance_sq <= INTERACT_RANGE * INTERACT_RANGE) {
            debug_log(ctx, "Player within interact range of entity %d (type: %s)", index,
                      entities[index].blueprint->name);
            out_events[count] = (TriggerEvent){
                .type = TRIGGER_INTERACT,
                .entity_index = index,
            };
            count++;
        }
    }
    return count;
}

static int collect_trigger_events(
    struct EngineContext *ctx, GameState *state, InputState input, TriggerEvent *out_events, int max_events)
{
    int count = 0;

    bool interact_pressed = input.buttons[0];
    bool interact_edge = false;
    if (interact_pressed) {
        if (!state->prev_interact) {
            interact_edge = true;
            debug_log(ctx, "Interact button pressed");
        }
    }
    state->prev_interact = interact_pressed;

    if (interact_edge) {
        debug_log(ctx, "Processing interact edge");
        if (state->player_index >= 0) {
            const Entity *player = game_get_player_const(state);
            if (player) {
                count +=
                    detect_interact_targets(ctx, player, state->player_index, state->current_level.entities,
                                            state->current_level.entity_count, out_events + count, max_events - count);
            }
        }
    }

    return count;
}

void game_update(struct EngineContext *ctx, GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;

    Entity *player = game_get_player(state);
    if (player) {
        update_player(player, input, delta_time, state->game_bounds);
        resolve_player_obstacles(&state->current_level, state->player_index);
    }

    update_child_positions(&state->current_level);

    TriggerEvent trigger_events[MAX_COLLECT_EVENTS];
    int trigger_count = collect_trigger_events(ctx, state, input, trigger_events, MAX_COLLECT_EVENTS);

    if (trigger_count > 0) {
        rules_evaluate_batch(ctx, state->current_level.entities, state->current_level.entity_count, trigger_events,
                             trigger_count, &state->flags, &state->vars);
    }
}

void game_free(GameState *state)
{
    for (int index = 0; index < state->blueprints.entries.count; index++) {
        vec_blueprint_child_free(&state->blueprints.entries.data[index].children);
        vec_attribute_free(&state->blueprints.entries.data[index].attrs.entries);
    }
    vec_blueprint_free(&state->blueprints.entries);
    for (int index = 0; index < state->current_level.entity_count; index++) {
        vec_attribute_free(&state->current_level.entities[index].attrs.entries);
    }
    vec_flag_name_free(&state->flags.names);
    vec_attribute_free(&state->vars.entries);
    arena_free(&state->gamedata_arena);
}

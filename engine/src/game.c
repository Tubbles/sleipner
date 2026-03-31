#include "game.h"
#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "error.h"
#include "entity.h"
#include "input.h"
#include "level.h"
#include "rect.h"
#include "rule.h"
#include "str.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <string.h>

#define TOML_ERRBUF_SIZE 200

bool game_init(struct EngineContext *ctx, GameState *state, RectU32 game_bounds)
{
    memset(state, 0, sizeof(*state));
    state->game_bounds = game_bounds;
    state->player_index = -1;
    state->debug_enabled = true;
    if (!arena_init(ctx, &state->gamedata_arena)) {
        error_wrap(ctx, "game_init");
        return false;
    }
    if (!arena_init(ctx, &state->scratch_arena)) {
        arena_free(&state->gamedata_arena);
        error_wrap(ctx, "game_init");
        return false;
    }
    return true;
}

const AttrSet *entity_resolve_defaults(const GameState *state, int entity_id)
{
    const Str *name = map_int_str_get(&state->entity_blueprints, entity_id);
    if (!name) {
        return nullptr;
    }
    const Blueprint *blueprint = blueprint_find(&state->blueprints, name->ptr);
    return blueprint ? &blueprint->attrs : nullptr;
}

static int find_player_entity(const GameState *state)
{
    const Level *level = &state->current_level;
    for (int index = 0; index < level->entities.count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, level->entities.data[index].id);
        const char *behavior = attr_get_scoped_string(&level->entities.data[index].attrs, defaults, "behavior");
        if (behavior && strcmp(behavior, "player") == 0) {
            return index;
        }
    }
    return -1;
}

bool game_load_gamedata(struct EngineContext *ctx, GameState *state, GamedataParams params)
{
    size_t length = strlen(params.toml_string);
    char *buffer = arena_alloc(ctx, &state->gamedata_arena, (AllocRequest){.size = length + 1, .alignment = 1});
    if (!buffer) {
        error_wrap(ctx, "game_load_gamedata");
        return false;
    }
    memcpy(buffer, params.toml_string, length + 1);

    char errbuf[TOML_ERRBUF_SIZE];
    toml_table_t *root = toml_parse(buffer, errbuf, (int)sizeof(errbuf));
    arena_restore(&state->gamedata_arena, state->gamedata_base);
    state->rule_table = (map_entity_ruleset){0};
    state->entity_blueprints = (map_int_str){0};
    if (!root) {
        error_set(ctx, "toml_parse: %s", errbuf);
        error_wrap(ctx, "game_load_gamedata");
        return false;
    }

    Allocator gamedata_alloc = allocator_arena(ctx, &state->gamedata_arena);
    state->subroutines = vec_subroutine_new(gamedata_alloc);
    state->timers = vec_timer_new(gamedata_alloc);
    state->prev_player_overlaps = vec_bool_new(gamedata_alloc);
    state->prev_solid_collisions = vec_bool_new(gamedata_alloc);
    blueprints_load(ctx, &state->blueprints, root, &state->gamedata_arena);
    bool subs_ok = subroutines_parse(ctx, &gamedata_alloc, &state->subroutines, root, &state->gamedata_arena);
    bool level_ok = false;
    if (subs_ok) {
        level_ok = level_load(ctx, &state->current_level, root, params.level_name, &state->blueprints,
                              params.texture_lookup, params.texture_user_data, &gamedata_alloc);
    }

    toml_free(root);
    state->gamedata_loaded = level_ok;

    if (level_ok) {
        for (int index = 0; index < state->current_level.entities.count; index++) {
            const Entity *entity = &state->current_level.entities.data[index];
            Str bp_name = {0};
            (void)str_from_strv(&gamedata_alloc, &bp_name, str_to_strv(entity->blueprint_name));
            (void)map_int_str_set(&state->entity_blueprints, entity->id, bp_name, &gamedata_alloc);
            const Blueprint *blueprint = blueprint_find(&state->blueprints, entity->blueprint_name.ptr);
            if (blueprint && blueprint->rules.count > 0) {
                (void)map_entity_ruleset_set(&state->rule_table, entity->id, blueprint->rules, &gamedata_alloc);
            }
        }
        state->player_index = find_player_entity(state);

        /* Initialize overlap tracking: one false entry per entity */
        for (int index = 0; index < state->current_level.entities.count; index++) {
            (void)vec_bool_push(&state->prev_player_overlaps, false);
        }

        /* Initialize solid-pair collision tracking: entity_count² entries */
        int entity_count = state->current_level.entities.count;
        for (int pair_index = 0; pair_index < entity_count * entity_count; pair_index++) {
            (void)vec_bool_push(&state->prev_solid_collisions, false);
        }

        /* Fire on_spawn for every entity now that the rule table is ready */
        {
            SCRATCH_SCOPE(&state->scratch_arena);
            Allocator scratch_alloc = allocator_arena(ctx, &state->scratch_arena);
            int spawn_count = state->current_level.entities.count;
            const AttrSet **spawn_defaults =
                (const AttrSet **)arena_alloc(ctx, &state->scratch_arena,
                                              (AllocRequest){.size = sizeof(const AttrSet *) * (size_t)spawn_count,
                                                             .alignment = _Alignof(const AttrSet *)});
            for (int index = 0; index < spawn_count; index++) {
                spawn_defaults[index] = entity_resolve_defaults(state, state->current_level.entities.data[index].id);
            }
            vec_trigger_event spawn_events = vec_trigger_event_new(scratch_alloc);
            for (int index = 0; index < spawn_count; index++) {
                (void)vec_trigger_event_push(&spawn_events,
                                             (TriggerEvent){.type = TRIGGER_ON_SPAWN, .entity_index = index});
            }
            if (spawn_events.count > 0) {
                rules_evaluate_batch(ctx, &gamedata_alloc, state->current_level.entities.data, spawn_count,
                                     spawn_events.data, spawn_events.count, &state->flags, &state->vars,
                                     &state->rule_table, &state->subroutines, &state->timers, spawn_defaults);
            }
        }
    } else {
        error_wrap(ctx, "game_load_gamedata");
    }

    return level_ok;
}

Entity *game_get_player(GameState *state)
{
    if (state->player_index < 0 || state->player_index >= state->current_level.entities.count) {
        return nullptr;
    }
    return &state->current_level.entities.data[state->player_index];
}

const Entity *game_get_player_const(const GameState *state)
{
    if (state->player_index < 0 || state->player_index >= state->current_level.entities.count) {
        return nullptr;
    }
    return &state->current_level.entities.data[state->player_index];
}

static void
update_player(Entity *player, const AttrSet *player_defaults, InputState input, float delta_time, RectU32 bounds)
{
    player->moving = false;

    float speed = attr_get_scoped_float(&player->attrs, player_defaults, "speed", DEFAULT_PLAYER_SPEED);

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

static void resolve_player_obstacles(GameState *state, int player_index)
{
    Level *level = &state->current_level;
    Entity *player = &level->entities.data[player_index];
    for (int index = 0; index < level->entities.count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, level->entities.data[index].id);
        if (index == player_index ||
            !attr_get_scoped_bool(&level->entities.data[index].attrs, defaults, "solid", false)) {
            continue;
        }
        Rectangle hitbox = player->collision;
        Rectangle obstacle = level->entities.data[index].collision;
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
    if (!level->entities.data) {
        return;
    }
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        if (entity->parent_index < 0) {
            continue;
        }
        const Entity *parent = &level->entities.data[entity->parent_index];
        entity->position.x = parent->position.x + entity->offset.x;
        entity->position.y = parent->position.y + entity->offset.y;
        entity_update_collision(entity);
    }
}

static void detect_interact_targets(struct EngineContext *ctx,
                                    const GameState *state,
                                    const Entity *player,
                                    int player_index,
                                    const Entity *entities,
                                    int entity_count,
                                    vec_trigger_event *out_events)
{
    for (int index = 0; index < entity_count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, entities[index].id);
        if (index == player_index || !attr_get_scoped_bool(&entities[index].attrs, defaults, "active", true)) {
            continue;
        }
        float delta_x = entities[index].position.x - player->position.x;
        float delta_y = entities[index].position.y - player->position.y;
        float distance_sq = (delta_x * delta_x) + (delta_y * delta_y);
        if (distance_sq <= INTERACT_RANGE * INTERACT_RANGE) {
            debug_log(ctx, "Player within interact range of entity %d (type: %s)", index,
                      entities[index].blueprint_name.ptr);
            (void)vec_trigger_event_push(out_events, (TriggerEvent){.type = TRIGGER_INTERACT, .entity_index = index});
        }
    }
}

static void detect_enter_targets(const GameState *state,
                                 const Entity *player,
                                 int player_index,
                                 const Entity *entities,
                                 int entity_count,
                                 vec_bool *overlaps,
                                 vec_trigger_event *out_events)
{
    for (int index = 0; index < entity_count && index < overlaps->count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, entities[index].id);
        if (index == player_index || !attr_get_scoped_bool(&entities[index].attrs, defaults, "active", true)) {
            overlaps->data[index] = false;
            continue;
        }
        bool currently_overlapping = CheckCollisionRecs(player->collision, entities[index].collision);
        bool was_overlapping = overlaps->data[index];
        overlaps->data[index] = currently_overlapping;
        if (currently_overlapping && !was_overlapping) {
            (void)vec_trigger_event_push(out_events, (TriggerEvent){.type = TRIGGER_ENTER, .entity_index = index});
        }
    }
}

static void
detect_solid_collisions(const GameState *state, Level *level, vec_bool *prev_collisions, vec_trigger_event *out_events)
{
    if (prev_collisions->count == 0) {
        return;
    }
    Entity *entities = level->entities.data;
    int entity_count = level->entities.count;
    for (int entity_a = 0; entity_a < entity_count; entity_a++) {
        const AttrSet *defaults_a = entity_resolve_defaults(state, entities[entity_a].id);
        if (!attr_get_scoped_bool(&entities[entity_a].attrs, defaults_a, "solid", false)) {
            continue;
        }
        for (int entity_b = entity_a + 1; entity_b < entity_count; entity_b++) {
            const AttrSet *defaults_b = entity_resolve_defaults(state, entities[entity_b].id);
            if (!attr_get_scoped_bool(&entities[entity_b].attrs, defaults_b, "solid", false)) {
                continue;
            }
            int pair_index = (entity_a * entity_count) + entity_b;
            bool currently = CheckCollisionRecs(entities[entity_a].collision, entities[entity_b].collision);
            bool was = prev_collisions->data[pair_index];
            prev_collisions->data[pair_index] = currently;
            if (currently && !was) {
                TriggerEvent event_a = {
                    .type = TRIGGER_COLLIDE, .entity_index = entity_a, .argument = entities[entity_b].blueprint_name};
                TriggerEvent event_b = {
                    .type = TRIGGER_COLLIDE, .entity_index = entity_b, .argument = entities[entity_a].blueprint_name};
                (void)vec_trigger_event_push(out_events, event_a);
                (void)vec_trigger_event_push(out_events, event_b);
            }
        }
    }
}

static void
collect_trigger_events(struct EngineContext *ctx, GameState *state, InputState input, vec_trigger_event *out_events)
{
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
                detect_interact_targets(ctx, state, player, state->player_index, state->current_level.entities.data,
                                        state->current_level.entities.count, out_events);
            }
        }
    }

    if (state->player_index >= 0 && state->prev_player_overlaps.count > 0) {
        const Entity *player = game_get_player_const(state);
        if (player) {
            detect_enter_targets(state, player, state->player_index, state->current_level.entities.data,
                                 state->current_level.entities.count, &state->prev_player_overlaps, out_events);
        }
    }
}

void game_update(struct EngineContext *ctx, GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;

    if (!state->editor_mode) {
        Entity *player = game_get_player(state);
        if (player) {
            const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
            update_player(player, player_defaults, input, delta_time, state->game_bounds);
            resolve_player_obstacles(state, state->player_index);
        }
    }

    update_child_positions(&state->current_level);

    if (!state->editor_mode) {
        SCRATCH_SCOPE(&state->scratch_arena);
        Allocator scratch_alloc = allocator_arena(ctx, &state->scratch_arena);
        vec_trigger_event trigger_events = vec_trigger_event_new(scratch_alloc);

        /* Detect new solid-entity overlaps and fire collide events on both parties */
        detect_solid_collisions(state, &state->current_level, &state->prev_solid_collisions, &trigger_events);

        /* Tick timers and collect fired events */
        for (int timer_index = state->timers.count - 1; timer_index >= 0; timer_index--) {
            Timer *timer = &state->timers.data[timer_index];
            timer->remaining -= delta_time;
            if (timer->remaining > 0.0F) {
                continue;
            }
            TriggerType fire_type = TRIGGER_TIMER;
            if (timer->periodic) {
                fire_type = TRIGGER_TIMER_PERIODIC;
            }
            (void)vec_trigger_event_push(
                &trigger_events,
                (TriggerEvent){.type = fire_type, .entity_index = timer->entity_index, .argument = timer->name});
            if (timer->periodic) {
                timer->remaining += timer->duration;
            } else {
                state->timers.data[timer_index] = state->timers.data[state->timers.count - 1];
                state->timers.count--;
            }
        }

        collect_trigger_events(ctx, state, input, &trigger_events);

        if (trigger_events.count > 0) {
            int update_count = state->current_level.entities.count;
            const AttrSet **update_defaults =
                (const AttrSet **)arena_alloc(ctx, &state->scratch_arena,
                                              (AllocRequest){.size = sizeof(const AttrSet *) * (size_t)update_count,
                                                             .alignment = _Alignof(const AttrSet *)});
            for (int index = 0; index < update_count; index++) {
                update_defaults[index] = entity_resolve_defaults(state, state->current_level.entities.data[index].id);
            }
            Allocator rule_alloc = allocator_arena(ctx, &state->gamedata_arena);
            rules_evaluate_batch(ctx, &rule_alloc, state->current_level.entities.data, update_count,
                                 trigger_events.data, trigger_events.count, &state->flags, &state->vars,
                                 &state->rule_table, &state->subroutines, &state->timers, update_defaults);
        }
    }
}

void game_free(struct EngineContext *ctx, GameState *state)
{
    (void)ctx;
    arena_free(&state->gamedata_arena);
    arena_free(&state->scratch_arena);
    *state = (GameState){0};
}

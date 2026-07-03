#include "game.h"
#include "diag.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "error.h"
#include "entity.h"
#include "input.h"
#include "input_func.h"
#include "level.h"
#include "preferences.h"
#include "rect.h"
#include "rule.h"
#include "str.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define TOML_ERRBUF_SIZE 200

bool game_init(Diag *diag, GameState *state, RectU32 game_bounds)
{
    /* Callers must zero-initialize state before calling game_init.
     * We only set fields that need non-zero defaults. */
    state->game_bounds = game_bounds;
    state->gamedata.player_index = -1;
    state->debug_enabled = true;
    if (!arena_init(diag->error, &state->gamedata_arena)) {
        error_wrap(diag->error, "game_init");
        return false;
    }
    if (!arena_init(diag->error, &state->scratch_arena)) {
        arena_free(&state->gamedata_arena);
        error_wrap(diag->error, "game_init");
        return false;
    }
    /* Default input bindings and preferences live in the gamedata arena.
     * main.c overlays the TOML files on top once persistent assets are
     * loaded. Both are populated BEFORE the gamedata_base checkpoint is
     * established so they sit below it and survive every
     * game_load_gamedata arena_restore. gamedata_base is set here at the
     * end of init so tests that skip load_persistent_assets still have a
     * valid checkpoint above them; production overwrites it after
     * textures and fonts. */
    input_func_load_defaults(&state->bindings, allocator_arena(&state->gamedata_arena));
    preferences_init_defaults(&state->preferences, allocator_arena(&state->gamedata_arena));
    state->preferences_path = str_new(allocator_arena(&state->gamedata_arena));
    state->gamedata_base = arena_save(&state->gamedata_arena);
    return true;
}

const AttrSet *entity_resolve_defaults(const GameState *state, int entity_id)
{
    const Str *name = map_int_str_get(&state->gamedata.entity_blueprints, entity_id);
    if (!name) {
        return nullptr;
    }
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, name->ptr);
    return blueprint ? &blueprint->attrs : nullptr;
}

static int find_player_entity(const GameState *state)
{
    const Level *level = &state->gamedata.current_level;
    for (int index = 0; index < level->entities.count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, level->entities.data[index].id);
        const char *behavior = attr_get_scoped_string(&level->entities.data[index].attrs, defaults, "behavior");
        if (behavior && strcmp(behavior, "player") == 0) {
            return index;
        }
    }
    return -1;
}

bool game_load_gamedata(Diag *diag, GameState *state, GamedataParams params)
{
    size_t length = strlen(params.toml_string);
    char *buffer = arena_alloc(&state->gamedata_arena, length + 1);
    if (!buffer) {
        error_wrap(diag->error, "game_load_gamedata");
        return false;
    }
    memcpy(buffer, params.toml_string, length + 1);

    char errbuf[TOML_ERRBUF_SIZE];
    toml_table_t *root = toml_parse(buffer, errbuf, (int)sizeof(errbuf));
    arena_restore(&state->gamedata_arena, state->gamedata_base);
    Allocator gamedata_alloc_early = allocator_arena(&state->gamedata_arena);
    state->gamedata.rule_table = map_entity_ruleset_new(gamedata_alloc_early);
    state->gamedata.entity_blueprints = map_int_str_new(gamedata_alloc_early);
    if (!root) {
        error_set(diag->error, "toml_parse: %s", errbuf);
        error_wrap(diag->error, "game_load_gamedata");
        return false;
    }

    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    state->gamedata.subroutines = vec_subroutine_new(gamedata_alloc);
    state->gamedata.timers = vec_timer_new(gamedata_alloc);
    state->gamedata.prev_player_overlaps = vec_bool_new(gamedata_alloc);
    state->gamedata.prev_solid_collisions = vec_bool_new(gamedata_alloc);
    state->gamedata.other_levels = vec_level_new(gamedata_alloc);
    blueprints_load(diag, &state->gamedata.blueprints, root, &state->gamedata_arena);
    bool subs_ok = subroutines_parse(diag, &gamedata_alloc, &state->gamedata.subroutines, root, &state->gamedata_arena);
    bool level_ok = false;
    if (subs_ok) {
        level_ok =
            level_load(diag, &state->gamedata.current_level, root, params.level_name, &state->gamedata.blueprints,
                       params.texture_lookup, params.texture_user_data, &gamedata_alloc);
    }
    if (level_ok) {
        (void)level_load_others(diag, &state->gamedata.other_levels, root, state->gamedata.current_level.name.ptr,
                                &state->gamedata.blueprints, params.texture_lookup, params.texture_user_data,
                                &gamedata_alloc);
    }

    toml_free(root);
    state->gamedata_loaded = level_ok;

    if (level_ok) {
        for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
            const Entity *entity = &state->gamedata.current_level.entities.data[index];
            Str bp_name = str_new(gamedata_alloc);
            (void)str_from_strv(&bp_name, str_to_strv(entity->blueprint_name));
            (void)map_int_str_set(&state->gamedata.entity_blueprints, entity->id, bp_name);
            const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, entity->blueprint_name.ptr);
            if (blueprint && blueprint->rules.count > 0) {
                (void)map_entity_ruleset_set(&state->gamedata.rule_table, entity->id, blueprint->rules);
            }
        }
        state->gamedata.player_index = find_player_entity(state);

        /* Initialize overlap tracking: one false entry per entity */
        for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
            (void)vec_bool_push(&state->gamedata.prev_player_overlaps, false);
        }

        /* Initialize solid-pair collision tracking: entity_count² entries */
        int entity_count = state->gamedata.current_level.entities.count;
        for (int pair_index = 0; pair_index < entity_count * entity_count; pair_index++) {
            (void)vec_bool_push(&state->gamedata.prev_solid_collisions, false);
        }

        /* Fire on_spawn for every entity now that the rule table is ready */
        {
            SCRATCH_SCOPE(&state->scratch_arena);
            Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
            int spawn_count = state->gamedata.current_level.entities.count;
            EntityView *spawn_views = arena_alloc(&state->scratch_arena, sizeof(EntityView) * (size_t)spawn_count);
            for (int index = 0; index < spawn_count; index++) {
                spawn_views[index] = (EntityView){
                    .entity = &state->gamedata.current_level.entities.data[index],
                    .defaults = entity_resolve_defaults(state, state->gamedata.current_level.entities.data[index].id),
                };
            }
            vec_trigger_event spawn_events = vec_trigger_event_new(scratch_alloc);
            for (int index = 0; index < spawn_count; index++) {
                (void)vec_trigger_event_push(&spawn_events,
                                             (TriggerEvent){.type = TRIGGER_ON_SPAWN, .entity_index = index});
            }
            if (spawn_events.count > 0) {
                rules_evaluate_batch(diag, &gamedata_alloc, spawn_views, spawn_count, spawn_events.data,
                                     spawn_events.count, &state->gamedata.flags, &state->gamedata.vars,
                                     &state->gamedata.rule_table, &state->gamedata.subroutines, &state->gamedata.timers,
                                     &scratch_alloc, &state->transition);
            }
        }
        game_snap_camera(state);
    } else {
        error_wrap(diag->error, "game_load_gamedata");
    }

    return level_ok;
}

Entity *game_get_player(GameState *state)
{
    if (state->gamedata.player_index < 0 ||
        state->gamedata.player_index >= state->gamedata.current_level.entities.count) {
        return nullptr;
    }
    return &state->gamedata.current_level.entities.data[state->gamedata.player_index];
}

const Entity *game_get_player_const(const GameState *state)
{
    if (state->gamedata.player_index < 0 ||
        state->gamedata.player_index >= state->gamedata.current_level.entities.count) {
        return nullptr;
    }
    return &state->gamedata.current_level.entities.data[state->gamedata.player_index];
}

static void update_player(Entity *player,
                          const AttrSet *player_defaults,
                          const InputState *input,
                          const BindingStore *bindings,
                          float delta_time,
                          RectU32 level_size)
{
    player->moving = false;

    float speed = attr_get_scoped_float(&player->attrs, player_defaults, "speed", DEFAULT_PLAYER_SPEED);
    Vector2 move = input_axis_pair(input, bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);

    if (move.x != 0.0F || move.y != 0.0F) {
        player->position.x += move.x * speed * delta_time;
        player->position.y += move.y * speed * delta_time;
        player->moving = true;

        if (fabsf(move.x) > fabsf(move.y)) {
            player->anim_row = ANIM_WALK_SIDE;
            player->flip = move.x < 0.0F;
        } else if (move.y > 0.0F) {
            player->anim_row = ANIM_WALK_DOWN;
        } else {
            player->anim_row = ANIM_WALK_UP;
        }
    }

    /* Clamp to level bounds */
    float half = FRAME_SIZE / 2.0F;
    if (player->position.x < half) {
        player->position.x = half;
    }
    if (player->position.y < half) {
        player->position.y = half;
    }
    if (player->position.x > (float)level_size.width - half) {
        player->position.x = (float)level_size.width - half;
    }
    if (player->position.y > (float)level_size.height - half) {
        player->position.y = (float)level_size.height - half;
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

static void resolve_player_obstacles(GameState *state, int player_index)
{
    Level *level = &state->gamedata.current_level;
    Entity *player = &level->entities.data[player_index];
    const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
    for (int index = 0; index < level->entities.count; index++) {
        const AttrSet *defaults = entity_resolve_defaults(state, level->entities.data[index].id);
        if (index == player_index ||
            !attr_get_scoped_bool(&level->entities.data[index].attrs, defaults, "solid", false)) {
            continue;
        }
        Rectangle hitbox = entity_collision_rect(player, player_defaults);
        Rectangle obstacle = entity_collision_rect(&level->entities.data[index], defaults);
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
    }
}

#define CAMERA_FOLLOW_SPEED 10.0F

static Vector2 camera_clamp_target(Vector2 target, RectU32 level_size, RectU32 viewport)
{
    float half_vw = (float)viewport.width / 2.0F;
    float half_vh = (float)viewport.height / 2.0F;

    if (level_size.width <= viewport.width) {
        target.x = (float)level_size.width / 2.0F;
    } else {
        if (target.x < half_vw) {
            target.x = half_vw;
        }
        if (target.x > (float)level_size.width - half_vw) {
            target.x = (float)level_size.width - half_vw;
        }
    }

    if (level_size.height <= viewport.height) {
        target.y = (float)level_size.height / 2.0F;
    } else {
        if (target.y < half_vh) {
            target.y = half_vh;
        }
        if (target.y > (float)level_size.height - half_vh) {
            target.y = (float)level_size.height - half_vh;
        }
    }

    return target;
}

static void camera_update_target(GameState *state, Vector2 player_position, float delta_time)
{
    float blend = fminf(CAMERA_FOLLOW_SPEED * delta_time, 1.0F);
    state->gamedata.camera_target.x += (player_position.x - state->gamedata.camera_target.x) * blend;
    state->gamedata.camera_target.y += (player_position.y - state->gamedata.camera_target.y) * blend;
    RectU32 level_size = {(uint32_t)state->gamedata.current_level.width,
                          (uint32_t)state->gamedata.current_level.height};
    state->gamedata.camera_target = camera_clamp_target(state->gamedata.camera_target, level_size, state->game_bounds);
}

void game_snap_camera(GameState *state)
{
    const Entity *player = game_get_player_const(state);
    Vector2 snap_position = player ? player->position
                                   : (Vector2){(float)state->gamedata.current_level.width / 2.0F,
                                               (float)state->gamedata.current_level.height / 2.0F};
    RectU32 level_size = {(uint32_t)state->gamedata.current_level.width,
                          (uint32_t)state->gamedata.current_level.height};
    state->gamedata.camera_target = camera_clamp_target(snap_position, level_size, state->game_bounds);
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
    }
}

static void detect_interact_targets(DebugState *dbg,
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
            debug_log(dbg, "Player within interact range of entity %d (type: %s)", index,
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
        const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
        bool currently_overlapping = CheckCollisionRecs(entity_collision_rect(player, player_defaults),
                                                        entity_collision_rect(&entities[index], defaults));
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
            bool currently = CheckCollisionRecs(entity_collision_rect(&entities[entity_a], defaults_a),
                                                entity_collision_rect(&entities[entity_b], defaults_b));
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
collect_trigger_events(DebugState *dbg, GameState *state, const InputState *input, vec_trigger_event *out_events)
{
    /* input_pressed is already an edge — true the frame the binding fired,
     * false otherwise. Drop the prev_interact bookkeeping; it was needed
     * because the legacy InputState.buttons[0] was a level read on
     * keyboard (IsKeyDown) merged with a one-shot on gamepad. */
    bool interact_edge = input_pressed(input, &state->bindings, ACTION_INTERACT);
    if (interact_edge) {
        debug_log(dbg, "Interact button pressed");
    }
    state->prev_interact = interact_edge;

    if (interact_edge) {
        debug_log(dbg, "Processing interact edge");
        if (state->gamedata.player_index >= 0) {
            const Entity *player = game_get_player_const(state);
            if (player) {
                detect_interact_targets(dbg, state, player, state->gamedata.player_index,
                                        state->gamedata.current_level.entities.data,
                                        state->gamedata.current_level.entities.count, out_events);
            }
        }
    }

    if (state->gamedata.player_index >= 0 && state->gamedata.prev_player_overlaps.count > 0) {
        const Entity *player = game_get_player_const(state);
        if (player) {
            detect_enter_targets(
                state, player, state->gamedata.player_index, state->gamedata.current_level.entities.data,
                state->gamedata.current_level.entities.count, &state->gamedata.prev_player_overlaps, out_events);
        }
    }
}

void game_update(Diag *diag, GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;

    if (!state->editor_mode) {
        Entity *player = game_get_player(state);
        if (player) {
            const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
            RectU32 level_size = {(uint32_t)state->gamedata.current_level.width,
                                  (uint32_t)state->gamedata.current_level.height};
            update_player(player, player_defaults, &input, &state->bindings, delta_time, level_size);
            resolve_player_obstacles(state, state->gamedata.player_index);
            camera_update_target(state, player->position, delta_time);
        }
    }

    update_child_positions(&state->gamedata.current_level);

    if (!state->editor_mode) {
        SCRATCH_SCOPE(&state->scratch_arena);
        Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
        vec_trigger_event trigger_events = vec_trigger_event_new(scratch_alloc);

        /* Detect new solid-entity overlaps and fire collide events on both parties */
        detect_solid_collisions(state, &state->gamedata.current_level, &state->gamedata.prev_solid_collisions,
                                &trigger_events);

        /* Tick timers and collect fired events */
        for (int timer_index = state->gamedata.timers.count - 1; timer_index >= 0; timer_index--) {
            Timer *timer = &state->gamedata.timers.data[timer_index];
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
                state->gamedata.timers.data[timer_index] =
                    state->gamedata.timers.data[state->gamedata.timers.count - 1];
                state->gamedata.timers.count--;
            }
        }

        collect_trigger_events(diag->debug, state, &input, &trigger_events);

        if (trigger_events.count > 0) {
            int update_count = state->gamedata.current_level.entities.count;
            EntityView *update_views = arena_alloc(&state->scratch_arena, sizeof(EntityView) * (size_t)update_count);
            for (int index = 0; index < update_count; index++) {
                update_views[index] = (EntityView){
                    .entity = &state->gamedata.current_level.entities.data[index],
                    .defaults = entity_resolve_defaults(state, state->gamedata.current_level.entities.data[index].id),
                };
            }
            Allocator rule_alloc = allocator_arena(&state->gamedata_arena);
            rules_evaluate_batch(diag, &rule_alloc, update_views, update_count, trigger_events.data,
                                 trigger_events.count, &state->gamedata.flags, &state->gamedata.vars,
                                 &state->gamedata.rule_table, &state->gamedata.subroutines, &state->gamedata.timers,
                                 &scratch_alloc, &state->transition);
        }
    }
}

void game_free(Diag *diag, GameState *state)
{
    (void)diag;
    arena_free(&state->gamedata_arena);
    arena_free(&state->scratch_arena);
    *state = (GameState){0};
}

Texture2D *texture_registry_lookup(const char *filename, void *user_data)
{
    GameState *state = (GameState *)user_data;
    if (!state || !filename) {
        return nullptr;
    }
    for (int index = 0; index < state->assets.textures.count; index++) {
        if (strcmp(state->assets.textures.data[index].filename, filename) == 0) {
            return &state->assets.textures.data[index].texture;
        }
    }
    return nullptr;
}

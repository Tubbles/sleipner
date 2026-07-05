#include "game.h"
#include "diag.h"

#include "alloc.h"
#include "arena.h"
#include "atlas.h"
#include "attribute.h"
#include "blueprint.h"
#include "collision.h"
#include "debug.h"
#include "effect.h"
#include "error.h"
#include "entity.h"
#include "input.h"
#include "input_func.h"
#include "level.h"
#include "preferences.h"
#include "random.h"
#include "rect.h"
#include "rule.h"
#include "str.h"
#include "strv.h"
#include "tileset.h"

#include "raylib.h"
#include "toml.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define TOML_ERRBUF_SIZE 200

/* Moved here from main.c (S5.4b) so headless tests can seed
 * state->assets.textures directly: main.c isn't linked into the test
 * binary, so a VEC_IMPL only there means no test TU can ever push a
 * TextureEntry. game.c already owns texture_registry_lookup, which reads
 * the same vec, so this is the natural home. */
VEC_IMPL(texture_entry, TextureEntry)

bool game_init(Diag *diag, GameState *state, RectU32 game_bounds)
{
    /* Callers must zero-initialize state before calling game_init.
     * We only set fields that need non-zero defaults. */
    state->game_bounds = game_bounds;
    state->gamedata.player_index = -1;
    state->debug_enabled = true;
    random_seed(&state->rng, (uint64_t)time(nullptr));
    if (!arena_init(diag->error, &state->gamedata_arena)) {
        error_wrap(diag->error, "game_init");
        return false;
    }
    if (!arena_init(diag->error, &state->scratch_arena)) {
        arena_free(&state->gamedata_arena);
        error_wrap(diag->error, "game_init");
        return false;
    }
    if (!arena_init(diag->error, &state->progression_arena)) {
        arena_free(&state->gamedata_arena);
        arena_free(&state->scratch_arena);
        error_wrap(diag->error, "game_init");
        return false;
    }
    effect_queue_init(&state->effects, allocator_arena(&state->progression_arena));
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

/* Builds one EntityView per current_level entity into scratch_arena --
 * the same array shape both rules_evaluate_batch and (S6.7b, D24)
 * rules_resume_continuations iterate. Factored out so the resume pass and
 * the trigger-driven batch in game_update share one build instead of
 * each rebuilding it. */
static EntityView *build_entity_views(GameState *state, int *out_count)
{
    int count = state->gamedata.current_level.entities.count;
    EntityView *views = arena_alloc(&state->scratch_arena, sizeof(EntityView) * (size_t)count);
    for (int index = 0; index < count; index++) {
        views[index] = (EntityView){
            .entity = &state->gamedata.current_level.entities.data[index],
            .defaults = entity_resolve_defaults(state, state->gamedata.current_level.entities.data[index].id),
        };
    }
    *out_count = count;
    return views;
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

/* Rebuild the runtime-derived state tied to whichever Level is current:
 * rule_table (entity id -> ruleset from its blueprint), entity_blueprints
 * (entity id -> blueprint name), player_index, and the prev_player_overlaps
 * / prev_solid_collisions trigger-edge trackers sized to the current entity
 * count. Called by game_load_gamedata after a fresh parse, and by
 * level_activate after swapping in a different Level as current_level —
 * both cases rebuild from scratch rather than reuse the previous maps/vecs
 * because entity ids are only unique within a single Level's own
 * next_entity_id sequence, so carrying over another level's entries would
 * silently misresolve rather than error. */
[[nodiscard]] bool setup_current_level_runtime(Diag *diag, GameState *state)
{
    (void)diag;
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    state->gamedata.rule_table = map_entity_ruleset_new(gamedata_alloc);
    state->gamedata.entity_blueprints = map_int_str_new(gamedata_alloc);
    state->gamedata.prev_player_overlaps = vec_bool_new(gamedata_alloc);
    state->gamedata.prev_solid_collisions = vec_bool_new(gamedata_alloc);
    /* A continuation's entity id / node indices are only meaningful
     * against the level they were captured from (S6.7b, D24) -- drop
     * every pending wait whenever the runtime is rebuilt for a
     * (possibly different) current level, the same way rule_table itself
     * is rebuilt from scratch above rather than carried over. */
    state->gamedata.continuations = vec_rule_continuation_new(gamedata_alloc);
    /* A dialogue box from a previous level must never survive a reload or
     * level switch (S6.7c, D24): `pages` is allocated from gamedata_arena,
     * so it dangles the moment this runtime is rebuilt for a different
     * level, and any continuation waiting on it is being dropped above
     * anyway. */
    state->dialogue = (DialogueState){0};

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
    return true;
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
    /* Reset these here, ahead of the root check, so a hard parse failure
     * still leaves valid empty maps rather than stale ones from a
     * previous load. setup_current_level_runtime recreates them again
     * (harmless, bounded) once a level actually loads successfully below. */
    Allocator gamedata_alloc_early = allocator_arena(&state->gamedata_arena);
    state->gamedata.rule_table = map_entity_ruleset_new(gamedata_alloc_early);
    state->gamedata.entity_blueprints = map_int_str_new(gamedata_alloc_early);
    state->gamedata.continuations = vec_rule_continuation_new(gamedata_alloc_early);
    state->dialogue = (DialogueState){0};
    if (!root) {
        error_set(diag->error, "toml_parse: %s", errbuf);
        error_wrap(diag->error, "game_load_gamedata");
        return false;
    }

    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    state->gamedata.subroutines = vec_subroutine_new(gamedata_alloc);
    state->gamedata.timers = vec_timer_new(gamedata_alloc);
    /* Same reasoning as rule_table/entity_blueprints above: reset ahead of
     * the subs_ok/level_ok checks so a parse-succeeds-but-load-fails path
     * still leaves valid empty vecs. setup_current_level_runtime recreates
     * them again once a level actually loads successfully. */
    state->gamedata.prev_player_overlaps = vec_bool_new(gamedata_alloc);
    state->gamedata.prev_solid_collisions = vec_bool_new(gamedata_alloc);
    state->gamedata.other_levels = vec_level_new(gamedata_alloc);
    /* Atlas regions are parsed before blueprints so blueprint_resolve_sprites
     * has a populated registry to resolve `sprite = "name"` refs against. */
    atlas_load(diag, &state->gamedata.atlas_regions, root, &state->gamedata_arena);
    blueprints_load(diag, &state->gamedata.blueprints, root, &state->gamedata_arena);
    blueprint_resolve_sprites(diag, &gamedata_alloc, &state->gamedata.blueprints, &state->gamedata.atlas_regions);
    tileset_load(diag, &state->gamedata.tileset, root, &state->gamedata_arena);
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
        if (!setup_current_level_runtime(diag, state)) {
            state->gamedata_loaded = false;
            error_wrap(diag->error, "game_load_gamedata");
            return false;
        }

        /* Fire on_spawn for every entity now that the rule table is ready */
        {
            SCRATCH_SCOPE(&state->scratch_arena);
            Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
            int spawn_count = 0;
            EntityView *spawn_views = build_entity_views(state, &spawn_count);
            vec_trigger_event spawn_events = vec_trigger_event_new(scratch_alloc);
            for (int index = 0; index < spawn_count; index++) {
                (void)vec_trigger_event_push(&spawn_events,
                                             (TriggerEvent){.type = TRIGGER_ON_SPAWN, .entity_index = index});
            }
            if (spawn_events.count > 0) {
                Allocator progression_alloc = allocator_arena(&state->progression_arena);
                rules_evaluate_batch(
                    diag, &gamedata_alloc, spawn_views, spawn_count, spawn_events.data, spawn_events.count,
                    &state->progression.flags, &state->progression.vars, &state->progression.items, &progression_alloc,
                    &state->gamedata.rule_table, &state->gamedata.subroutines, &state->gamedata.timers, &scratch_alloc,
                    &state->transition, &state->effects, &state->dialogue, &state->gamedata.continuations);
            }
        }
        game_snap_camera(state);
    } else {
        error_wrap(diag->error, "game_load_gamedata");
    }

    return level_ok;
}

static int find_other_level_index(const GameState *state, Strv target_name)
{
    for (int index = 0; index < state->gamedata.other_levels.count; index++) {
        if (strv_eq(str_to_strv(state->gamedata.other_levels.data[index].name), target_name)) {
            return index;
        }
    }
    return -1;
}

bool level_activate(Diag *diag, GameState *state, Strv target_name)
{
    if (strv_eq(str_to_strv(state->gamedata.current_level.name), target_name)) {
        return true;
    }
    int other_index = find_other_level_index(state, target_name);
    if (other_index < 0) {
        return false;
    }

    /* Shallow struct swap: both Levels (and their entity vecs) already
     * live in gamedata_arena, so exchanging the two struct values trades
     * ownership without touching the underlying bytes — both levels'
     * entities survive the round trip. */
    Level swap_temp = state->gamedata.current_level;
    state->gamedata.current_level = state->gamedata.other_levels.data[other_index];
    state->gamedata.other_levels.data[other_index] = swap_temp;

    return setup_current_level_runtime(diag, state);
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
            player->facing = (Vector2){move.x < 0.0F ? -1.0F : 1.0F, 0.0F};
        } else if (move.y > 0.0F) {
            player->anim_row = ANIM_WALK_DOWN;
            player->facing = (Vector2){0.0F, 1.0F};
        } else {
            player->anim_row = ANIM_WALK_UP;
            player->facing = (Vector2){0.0F, -1.0F};
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

/* Push `entity` out of every solid obstacle in the level. Generalized from
 * the original player-only resolve_player_obstacles (S6.9a, D30) so any
 * moving behavior can reuse it -- only the mover's own position is ever
 * adjusted, obstacles never move. */
static void resolve_entity_obstacles(GameState *state, int entity_index)
{
    Level *level = &state->gamedata.current_level;
    Entity *entity = &level->entities.data[entity_index];
    const AttrSet *entity_defaults = entity_resolve_defaults(state, entity->id);
    CollisionPrimitive entity_prim_storage;
    CollisionShape entity_shape = entity_collision_region(entity, entity_defaults, &entity_prim_storage);
    for (int index = 0; index < level->entities.count; index++) {
        Entity *obstacle = &level->entities.data[index];
        const AttrSet *defaults = entity_resolve_defaults(state, obstacle->id);
        if (index == entity_index || !attr_get_scoped_bool(&obstacle->attrs, defaults, "solid", false)) {
            continue;
        }
        CollisionPrimitive obstacle_prim_storage;
        CollisionShape obstacle_shape = entity_collision_region(obstacle, defaults, &obstacle_prim_storage);
        Vector2 push =
            resolve_composite(&entity_shape, entity->position, 0.0F, &obstacle_shape, obstacle->position, 0.0F);
        entity->position.x += push.x;
        entity->position.y += push.y;
    }
}

/* --- Behavior dispatch table (S6.9a/S6.9b, D30/D39) ---
 *
 * `behavior name -> update fn` lookup, no OOP -- mirrors action_mappings'
 * table-lookup style (rule.c). game_update dispatches every current-level
 * entity through this instead of hardcoding a single player update. */

typedef struct {
    GameState *state;
    int entity_index;
    const InputState *input; /* the real local input for this frame */
    const BindingStore *bindings;
    float delta_time;
} BehaviorContext;

typedef void (*BehaviorUpdateFn)(BehaviorContext *context);

/* D39's multiplayer input seam: an entity's `input_source` attr (default
 * "local:0") selects where its input comes from. Only "local:0" resolves to
 * the real local input today -- every other source (a second local gamepad,
 * or a future networked player) yields idle input, since no other input
 * providers exist yet. This is the sole point behaviors read input through,
 * so plugging in real multi-source routing later (S8) means changing only
 * this function. */
static InputState input_for_entity(const Entity *entity, const AttrSet *defaults, const InputState *local_input)
{
    const char *source = attr_get_scoped_string(&entity->attrs, defaults, "input_source");
    if (!source || strcmp(source, "local:0") == 0) {
        return *local_input;
    }
    return (InputState){0};
}

static void behavior_static(BehaviorContext *context)
{
    (void)context;
}

/* How long a fresh ACTION_ATTACK press keeps the player's hitbox active
 * (S6.10b, D26). Chosen as a short swing window, well under
 * ENTITY_DEFAULT_IFRAME_SECONDS so a single swing can't double-hit a
 * target through its own i-frames. */
#define ATTACK_ACTIVE_SECONDS 0.15F

/* Player attack activation (S6.10b, D26): a fresh ACTION_ATTACK press
 * starts a timed melee swing by setting hitbox_active_timer, gated on
 * "not already mid-attack" so holding/mashing the button can't restart
 * the window every frame -- the next press only takes effect once
 * tick_combat_timers (game_update) has ticked hitbox_active_timer back
 * down to 0. The attack's direction is read live from player->facing
 * wherever the hitbox is built (entity_hitbox_region) rather than
 * captured into a separate field -- movement during the active window is
 * allowed (no movement lock in v1), so the hitbox simply tracks whatever
 * direction the player is currently facing. */
static void update_player_attack(Entity *player, const InputState *input, const BindingStore *bindings)
{
    if (player->hitbox_active_timer > 0.0F) {
        return;
    }
    if (!input_pressed(input, bindings, ACTION_ATTACK)) {
        return;
    }
    player->hitbox_active_timer = ATTACK_ACTIVE_SECONDS;
}

static void behavior_player(BehaviorContext *context)
{
    GameState *state = context->state;
    Entity *player = &state->gamedata.current_level.entities.data[context->entity_index];
    const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
    InputState routed_input = input_for_entity(player, player_defaults, context->input);
    RectU32 level_size = {(uint32_t)state->gamedata.current_level.width,
                          (uint32_t)state->gamedata.current_level.height};
    update_player(player, player_defaults, &routed_input, context->bindings, context->delta_time, level_size);
    resolve_entity_obstacles(state, context->entity_index);
    update_player_attack(player, &routed_input, context->bindings);
}

/* Sets moving/anim_row/flip/frame_timer/frame_index from a per-frame move
 * step, the same walk-cycle rule update_player uses -- shared by
 * behavior_npc_patrol and behavior_chase (S6.9b, D30) so both animate
 * consistently without duplicating the frame-cycling math twice. */
static void animate_walking_entity(Entity *entity, Vector2 step, float delta_time)
{
    entity->moving = step.x != 0.0F || step.y != 0.0F;
    if (!entity->moving) {
        entity->frame_index = 0;
        entity->frame_timer = 0.0F;
        return;
    }
    if (fabsf(step.x) > fabsf(step.y)) {
        entity->anim_row = ANIM_WALK_SIDE;
        entity->flip = step.x < 0.0F;
    } else if (step.y > 0.0F) {
        entity->anim_row = ANIM_WALK_DOWN;
    } else {
        entity->anim_row = ANIM_WALK_UP;
    }
    entity->frame_timer += delta_time * ANIM_SPEED;
    if (entity->frame_timer >= 1.0F) {
        entity->frame_timer -= 1.0F;
        entity->frame_index = (entity->frame_index + 1) % WALK_FRAMES;
    }
}

/* Back-and-forth mover per D30: oscillates along (patrol_dx, patrol_dy)
 * with a full out-and-back cycle every patrol_period seconds. Uses an
 * INCREMENTAL model -- entity->patrol_phase (a runtime-only Entity field,
 * never emitted to TOML) advances by delta_time and wraps at
 * patrol_period, then the first half of the cycle steps toward
 * +patrol_delta and the second half steps back -- so resolve_entity_
 * obstacles can push the mover out of a wall on every frame of travel,
 * not just at two fixed endpoints. patrol_period <= 0 (unset/misauthored)
 * or a zero/negative delta_time is a no-op, matching a static entity. */
static void behavior_npc_patrol(BehaviorContext *context)
{
    GameState *state = context->state;
    Entity *entity = &state->gamedata.current_level.entities.data[context->entity_index];
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    float patrol_period = attr_get_scoped_float(&entity->attrs, defaults, "patrol_period", 0.0F);
    if (patrol_period <= 0.0F || context->delta_time <= 0.0F) {
        animate_walking_entity(entity, (Vector2){0}, context->delta_time);
        return;
    }

    float patrol_dx = attr_get_scoped_float(&entity->attrs, defaults, "patrol_dx", 0.0F);
    float patrol_dy = attr_get_scoped_float(&entity->attrs, defaults, "patrol_dy", 0.0F);
    float half_period = patrol_period / 2.0F;

    entity->patrol_phase = fmodf(entity->patrol_phase + context->delta_time, patrol_period);
    float direction = entity->patrol_phase < half_period ? 1.0F : -1.0F;
    Vector2 step = {
        direction * (patrol_dx / half_period) * context->delta_time,
        direction * (patrol_dy / half_period) * context->delta_time,
    };

    entity->position.x += step.x;
    entity->position.y += step.y;
    animate_walking_entity(entity, step, context->delta_time);
    resolve_entity_obstacles(state, context->entity_index);
}

#define CHASE_STEER_EPSILON 0.01F

static float distance_between(Vector2 first, Vector2 second)
{
    float delta_x = second.x - first.x;
    float delta_y = second.y - first.y;
    return sqrtf((delta_x * delta_x) + (delta_y * delta_y));
}

/* Pure steering step: moves speed * delta_time pixels from chaser_position
 * straight toward target_position, or zero once within CHASE_STEER_EPSILON
 * (avoids normalizing a near-zero vector). Factored out of behavior_chase
 * so the direction/step math is unit-testable independent of the
 * aggro-radius gate and collision resolution (S6.9b, D30). */
static Vector2 chase_step_toward(Vector2 chaser_position, Vector2 target_position, float speed, float delta_time)
{
    float distance = distance_between(chaser_position, target_position);
    if (distance <= CHASE_STEER_EPSILON) {
        return (Vector2){0};
    }
    float scale = (speed * delta_time) / distance;
    return (Vector2){(target_position.x - chaser_position.x) * scale, (target_position.y - chaser_position.y) * scale};
}

/* Straight-line chaser per D30: while within aggro_radius of the player,
 * steers directly toward it (chase_step_toward) and reuses
 * resolve_entity_obstacles for collision, so a solid wall between chaser
 * and player blocks the chase exactly like it blocks the player. Idles
 * (no movement) if there's no player entity or the player is outside
 * aggro_radius. */
static void behavior_chase(BehaviorContext *context)
{
    GameState *state = context->state;
    Entity *entity = &state->gamedata.current_level.entities.data[context->entity_index];
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    const Entity *player = game_get_player_const(state);
    entity->moving = false;
    if (!player) {
        return;
    }

    float aggro_radius = attr_get_scoped_float(&entity->attrs, defaults, "aggro_radius", 0.0F);
    if (distance_between(entity->position, player->position) > aggro_radius) {
        return;
    }

    float speed = attr_get_scoped_float(&entity->attrs, defaults, "speed", DEFAULT_PLAYER_SPEED);
    Vector2 step = chase_step_toward(entity->position, player->position, speed, context->delta_time);
    entity->position.x += step.x;
    entity->position.y += step.y;
    animate_walking_entity(entity, step, context->delta_time);
    resolve_entity_obstacles(state, context->entity_index);
}

static const struct {
    const char *name;
    BehaviorUpdateFn fn;
} behavior_table[] = {
    {"static", behavior_static}, {"player", behavior_player}, {"npc_patrol", behavior_npc_patrol},
    {"chase", behavior_chase},   {nullptr, nullptr},
};

/* Falls back to the static (no-op) behavior for a null or unrecognized
 * name -- an entity with no `behavior` attr is inert by default. */
static BehaviorUpdateFn behavior_lookup(const char *name)
{
    if (!name) {
        return behavior_static;
    }
    for (int index = 0; behavior_table[index].name; index++) {
        if (strcmp(name, behavior_table[index].name) == 0) {
            return behavior_table[index].fn;
        }
    }
    return behavior_static;
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

Vector2 camera_pan_position(Vector2 from, Vector2 target, float elapsed, float duration)
{
    float progress = duration > 0.0F ? elapsed / duration : 1.0F;
    if (progress < 0.0F) {
        progress = 0.0F;
    } else if (progress > 1.0F) {
        progress = 1.0F;
    }
    return (Vector2){
        from.x + ((target.x - from.x) * progress),
        from.y + ((target.y - from.y) * progress),
    };
}

/* decayed = magnitude * (1 - elapsed/duration), rearranged to
 * magnitude - (magnitude * elapsed) / duration (equivalent by the
 * distributive law) so magnitude and elapsed appear together in the same
 * sub-expression -- clang-tidy's bugprone-easily-swappable-parameters
 * otherwise flags the two as swappable, since nothing in the original
 * formula used them together. */
float camera_shake_magnitude(float magnitude, float elapsed, float duration)
{
    if (duration <= 0.0F || elapsed >= duration) {
        return 0.0F;
    }
    float decayed = magnitude - ((magnitude * elapsed) / duration);
    return decayed < 0.0F ? 0.0F : decayed;
}

/* Pan (S6.5, D22/D26) overrides normal follow while active: advance
 * elapsed, interpolate via the pure camera_pan_position, and clear
 * `active` once elapsed reaches duration so plain follow resumes next
 * frame. Both branches feed the same camera_clamp_target call so a pan
 * target can never push the camera outside the level. */
static void camera_update_target(GameState *state, Vector2 player_position, float delta_time)
{
    CameraPanEffect *pan = &state->camera_effect.pan;
    if (pan->active) {
        pan->elapsed += delta_time;
        state->gamedata.camera_target = camera_pan_position(pan->from, pan->target, pan->elapsed, pan->duration);
        if (pan->elapsed >= pan->duration) {
            pan->active = false;
        }
    } else {
        float blend = fminf(CAMERA_FOLLOW_SPEED * delta_time, 1.0F);
        state->gamedata.camera_target.x += (player_position.x - state->gamedata.camera_target.x) * blend;
        state->gamedata.camera_target.y += (player_position.y - state->gamedata.camera_target.y) * blend;
    }
    RectU32 level_size = {(uint32_t)state->gamedata.current_level.width,
                          (uint32_t)state->gamedata.current_level.height};
    state->gamedata.camera_target = camera_clamp_target(state->gamedata.camera_target, level_size, state->game_bounds);
}

/* Shake (S6.5, D22/D26) is a render-time jitter, never written into
 * camera_target itself: advance elapsed, decay the magnitude via the pure
 * camera_shake_magnitude, and redraw a fresh random offset every frame
 * while active. Computed here (not render.c/main.c) so it stays
 * headless-observable and render stays read-only; main.c's camera
 * assembly adds this offset to camera_target at draw time. */
static void camera_update_shake(GameState *state, float delta_time)
{
    CameraShakeEffect *shake = &state->camera_effect.shake;
    if (shake->elapsed >= shake->duration) {
        shake->offset = (Vector2){0};
        return;
    }
    shake->elapsed += delta_time;
    float current_magnitude = camera_shake_magnitude(shake->magnitude, shake->elapsed, shake->duration);
    shake->offset = (Vector2){
        random_float_range(&state->rng, -1.0F, 1.0F) * current_magnitude,
        random_float_range(&state->rng, -1.0F, 1.0F) * current_magnitude,
    };
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
    /* A snap means fresh load, hot-reload, or level transition (see the
     * two call sites: game_load_gamedata and frame.c's handle_transition)
     * -- a pan/shake in flight must not survive into the new context. */
    state->camera_effect = (CameraEffect){0};
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
        CollisionPrimitive player_prim_storage;
        CollisionPrimitive trigger_prim_storage;
        CollisionShape player_shape = entity_collision_region(player, player_defaults, &player_prim_storage);
        CollisionShape trigger_shape = entity_trigger_region(&entities[index], defaults, &trigger_prim_storage);
        bool currently_overlapping =
            composite_overlap(&player_shape, player->position, 0.0F, &trigger_shape, entities[index].position, 0.0F);
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
            CollisionPrimitive prim_storage_a;
            CollisionPrimitive prim_storage_b;
            CollisionShape shape_a = entity_collision_region(&entities[entity_a], defaults_a, &prim_storage_a);
            CollisionShape shape_b = entity_collision_region(&entities[entity_b], defaults_b, &prim_storage_b);
            bool currently = composite_overlap(&shape_a, entities[entity_a].position, 0.0F, &shape_b,
                                               entities[entity_b].position, 0.0F);
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

/* Decrements the per-entity combat timers (S6.10a, D26) by delta_time,
 * clamped at 0 so they never go negative: iframe_timer (invincibility
 * window after a hit) and hitbox_active_timer (how much longer this
 * entity's own hitbox stays live -- set by the S6.10b attack activator,
 * just honored here). Runs once per frame for every entity in the level,
 * mirroring update_child_positions' "for each entity, mutate" shape. */
static void tick_combat_timers(Level *level, float delta_time)
{
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        entity->iframe_timer = fmaxf(0.0F, entity->iframe_timer - delta_time);
        entity->hitbox_active_timer = fmaxf(0.0F, entity->hitbox_active_timer - delta_time);
    }
}

/* One-hit-per-frame melee resolution (S6.10a, D26): every entity A with a
 * live hitbox_active_timer damages every OTHER active entity T (that has a
 * `health` attr) whose hurtbox its hitbox overlaps. entity_apply_damage
 * itself rejects a T still inside its own i-frame window, so a T hit by
 * several overlapping hitboxes in the same frame is only damaged once --
 * the iframe_timer set on the first hit blocks the rest with no separate
 * "already hit this frame" bookkeeping needed. A hit that drops health
 * from > 0 to <= 0 pushes TRIGGER_DEFEAT into out_events so it fires in
 * this same frame's rules_evaluate_batch, mirroring how
 * execute_set_attr_action/execute_add_attr_action (rule.c) push the same
 * event when a rule-driven health write crosses zero; gating on "old
 * health was > 0" (not just "new health <= 0") keeps an already-defeated
 * entity that gets hit again (once its i-frames lapse) from re-firing
 * defeat every subsequent hit. */
static void detect_melee_damage(GameState *state, Level *level, Allocator *alloc, vec_trigger_event *out_events)
{
    Entity *entities = level->entities.data;
    int entity_count = level->entities.count;
    for (int attacker_index = 0; attacker_index < entity_count; attacker_index++) {
        if (entities[attacker_index].hitbox_active_timer <= 0.0F) {
            continue;
        }
        const AttrSet *attacker_defaults = entity_resolve_defaults(state, entities[attacker_index].id);
        CollisionPrimitive hitbox_prim_storage;
        CollisionShape hitbox =
            entity_hitbox_region(&entities[attacker_index], attacker_defaults, &hitbox_prim_storage);
        float damage = attr_get_scoped_float(&entities[attacker_index].attrs, attacker_defaults, "damage", 0.0F);

        for (int target_index = 0; target_index < entity_count; target_index++) {
            if (target_index == attacker_index) {
                continue;
            }
            const AttrSet *target_defaults = entity_resolve_defaults(state, entities[target_index].id);
            if (!attr_get_scoped(&entities[target_index].attrs, target_defaults, "health") ||
                !attr_get_scoped_bool(&entities[target_index].attrs, target_defaults, "active", true)) {
                continue;
            }
            CollisionPrimitive hurtbox_prim_storage;
            CollisionShape hurtbox =
                entity_hurtbox_region(&entities[target_index], target_defaults, &hurtbox_prim_storage);
            bool overlapping = composite_overlap(&hitbox, entities[attacker_index].position, 0.0F, &hurtbox,
                                                 entities[target_index].position, 0.0F);
            if (!overlapping) {
                continue;
            }
            float old_health = attr_get_scoped_float(&entities[target_index].attrs, target_defaults, "health", 0.0F);
            if (!entity_apply_damage(&entities[target_index], target_defaults, damage, alloc)) {
                continue;
            }
            float new_health = attr_get_scoped_float(&entities[target_index].attrs, target_defaults, "health", 0.0F);
            if (old_health > 0.0F && new_health <= 0.0F) {
                (void)vec_trigger_event_push(out_events,
                                             (TriggerEvent){.type = TRIGGER_DEFEAT, .entity_index = target_index});
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
        for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
            Entity *entity = &state->gamedata.current_level.entities.data[index];
            const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
            const char *behavior_name = attr_get_scoped_string(&entity->attrs, defaults, "behavior");
            BehaviorUpdateFn update_fn = behavior_lookup(behavior_name);
            BehaviorContext context = {
                .state = state,
                .entity_index = index,
                .input = &input,
                .bindings = &state->bindings,
                .delta_time = delta_time,
            };
            update_fn(&context);
        }

        const Entity *player = game_get_player_const(state);
        if (player) {
            camera_update_target(state, player->position, delta_time);
            camera_update_shake(state, delta_time);
        }
    }

    update_child_positions(&state->gamedata.current_level);

    if (!state->editor_mode) {
        SCRATCH_SCOPE(&state->scratch_arena);
        Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
        vec_trigger_event trigger_events = vec_trigger_event_new(scratch_alloc);

        int view_count = 0;
        EntityView *views = build_entity_views(state, &view_count);

        /* Combat timer tick (S6.10a, D26) -- decrement before this frame's
         * melee pass below reads them, so a timer set by scenario setup
         * or the S6.10b attack activator is honored as active THIS frame,
         * not the frame after. */
        tick_combat_timers(&state->gamedata.current_level, delta_time);

        /* Resume due `wait:`/`dialogue:` continuations (S6.7b/c, D24)
         * before any normal trigger detection/evaluation this frame -- a
         * resumed action's fire_event/destroy feeds into trigger_events
         * below, joining this frame's own cascade the same way
         * rules_evaluate_batch's internal cascade already handles events
         * from freshly-triggered rules. Note this only runs when
         * game_update itself runs -- frame.c's dialogue branch skips
         * game_update entirely while a dialogue is open, so a
         * WAKE_DIALOGUE_CLOSED continuation only becomes due starting the
         * first frame AFTER the dialogue closes. */
        if (state->gamedata.continuations.count > 0) {
            Allocator rule_alloc = allocator_arena(&state->gamedata_arena);
            Allocator progression_alloc = allocator_arena(&state->progression_arena);
            rules_resume_continuations(diag, &rule_alloc, views, view_count, &trigger_events, &state->progression.flags,
                                       &state->progression.vars, &state->progression.items, &progression_alloc,
                                       &state->gamedata.rule_table, &state->gamedata.subroutines,
                                       &state->gamedata.timers, &state->transition, &state->effects, &state->dialogue,
                                       &state->gamedata.continuations, delta_time);
        }

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

        /* Melee damage pass (S6.10a, D26) -- must run before the
         * trigger_events.count check below, since a defeat it detects
         * needs to feed into THIS frame's rules_evaluate_batch. */
        Allocator combat_alloc = allocator_arena(&state->gamedata_arena);
        detect_melee_damage(state, &state->gamedata.current_level, &combat_alloc, &trigger_events);

        if (trigger_events.count > 0) {
            Allocator rule_alloc = allocator_arena(&state->gamedata_arena);
            Allocator progression_alloc = allocator_arena(&state->progression_arena);
            rules_evaluate_batch(diag, &rule_alloc, views, view_count, trigger_events.data, trigger_events.count,
                                 &state->progression.flags, &state->progression.vars, &state->progression.items,
                                 &progression_alloc, &state->gamedata.rule_table, &state->gamedata.subroutines,
                                 &state->gamedata.timers, &scratch_alloc, &state->transition, &state->effects,
                                 &state->dialogue, &state->gamedata.continuations);
        }
    }
}

void game_free(Diag *diag, GameState *state)
{
    (void)diag;
    arena_free(&state->gamedata_arena);
    arena_free(&state->scratch_arena);
    arena_free(&state->progression_arena);
    *state = (GameState){0};
}

void game_reset_progression(GameState *state)
{
    arena_reset(&state->progression_arena);
    state->progression = (ProgressionState){0};
    effect_queue_init(&state->effects, allocator_arena(&state->progression_arena));
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

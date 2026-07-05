#include "game.h"
#include "diag.h"

#include "alloc.h"
#include "arena.h"
#include "atlas.h"
#include "attribute.h"
#include "audio.h"
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

const Blueprint *entity_resolve_blueprint(const GameState *state, int entity_id)
{
    const Str *name = map_int_str_get(&state->gamedata.entity_blueprints, entity_id);
    if (!name) {
        return nullptr;
    }
    return blueprint_find(&state->gamedata.blueprints, name->ptr);
}

const AttrSet *entity_resolve_defaults(const GameState *state, int entity_id)
{
    const Blueprint *blueprint = entity_resolve_blueprint(state, entity_id);
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
        music_on_level_changed(&state->music, state->gamedata.current_level.music_name.ptr);
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

    if (!setup_current_level_runtime(diag, state)) {
        return false;
    }
    music_on_level_changed(&state->music, state->gamedata.current_level.music_name.ptr);
    return true;
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

/* Cardinal unit vector for a movement step: the dominant axis by magnitude
 * wins ties toward the side row, matching the direction picking every
 * movement behavior below uses. Only ever called from an "actually moving"
 * branch (step is never the zero vector at the call sites below). */
static Vector2 facing_from_step(Vector2 step)
{
    if (fabsf(step.x) > fabsf(step.y)) {
        return (Vector2){step.x < 0.0F ? -1.0F : 1.0F, 0.0F};
    }
    if (step.y > 0.0F) {
        return (Vector2){0.0F, 1.0F};
    }
    return (Vector2){0.0F, -1.0F};
}

/* Name of the built-in `direction` attr matching a facing unit vector --
 * the inverse of facing_from_step's side/down/up cases, splitting side
 * into left/right by the sign of facing.x. */
static const char *direction_name_from_facing(Vector2 facing)
{
    if (facing.x < 0.0F) {
        return "left";
    }
    if (facing.x > 0.0F) {
        return "right";
    }
    return facing.y > 0.0F ? "down" : "up";
}

/* Translates an entity's moving/facing (already computed by the caller's
 * own movement logic) into the built-in `state`/`direction` attrs the
 * generic animation pass (advance_entity_animation, below the behavior
 * table) reads every frame -- this is what update_player and
 * animate_walking_entity now call instead of writing
 * anim_row/flip/frame_timer/frame_index directly (S6.11a, D31). */
static void update_entity_anim_attrs(Allocator *alloc, Entity *entity)
{
    const char *state_name = entity->moving ? "walk" : "idle";
    const char *direction = direction_name_from_facing(entity->facing);
    (void)attr_set_string(alloc, &entity->attrs, (AttrStringPair){.name = "state", .value = state_name});
    (void)attr_set_string(alloc, &entity->attrs, (AttrStringPair){.name = "direction", .value = direction});
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
        player->facing = facing_from_step(move);
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

/* Seconds a clip plays for one full cycle (frames / speed), or
 * fallback_seconds if the clip is absent or malformed (no frames, or a
 * non-positive speed -- an "idle"-style clip authoring that can't drive a
 * timed state). Shared by update_player_attack's attack_state_timer,
 * begin_hurt_state's hurt_state_timer, and begin_death_state's
 * death_state_timer below (S6.11b, D31) -- all three derive a combat
 * timer's length from a blueprint-authored clip the same way. */
static float anim_clip_duration(const AnimClip *clip, float fallback_seconds)
{
    if (!clip || clip->frames <= 0 || clip->speed <= 0.0F) {
        return fallback_seconds;
    }
    return (float)clip->frames / clip->speed;
}

/* Fallback attack_state_timer length (S6.10b/S6.11b, D26/D31) when the
 * attacker's blueprint authors no `attack` clip -- a short swing window,
 * well under ENTITY_DEFAULT_IFRAME_SECONDS so a single swing can't
 * double-hit a target through its own i-frames. A blueprint that DOES
 * author an `attack` clip uses that clip's own frames/speed duration
 * instead (see update_player_attack), so a longer/shorter clip naturally
 * lengthens/shortens the swing. */
#define ATTACK_STATE_DEFAULT_SECONDS 0.15F

/* Player attack activation (S6.10b/S6.11b, D26/D31): a fresh ACTION_ATTACK
 * press starts a timed attack STATE by setting attack_state_timer to the
 * attacker's `attack` clip duration (or ATTACK_STATE_DEFAULT_SECONDS with
 * no such clip) and resetting the animation to frame 0, gated on "not
 * already mid-attack" so holding/mashing the button can't restart the
 * window every frame -- the next press only takes effect once
 * tick_combat_timers (game_update) has ticked attack_state_timer back down
 * to 0. Which frames of that window actually deal damage is
 * entity_hitbox_is_active's job (entity.c), driven by frame_index as the
 * clip plays (advance_entity_animation, below). The attack's direction is
 * read live from player->facing wherever the hitbox is built
 * (entity_hitbox_region) rather than captured into a separate field --
 * movement during the active window is allowed (no movement lock in v1),
 * so the hitbox simply tracks whatever direction the player is currently
 * facing. */
static void
update_player_attack(Entity *player, const Blueprint *blueprint, const InputState *input, const BindingStore *bindings)
{
    if (player->attack_state_timer > 0.0F) {
        return;
    }
    if (!input_pressed(input, bindings, ACTION_ATTACK)) {
        return;
    }
    const AnimClip *attack_clip = blueprint ? blueprint_find_anim_clip(blueprint, "attack") : nullptr;
    player->attack_state_timer = anim_clip_duration(attack_clip, ATTACK_STATE_DEFAULT_SECONDS);
    player->frame_index = 0;
    player->frame_timer = 0.0F;
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
    const Blueprint *player_blueprint = entity_resolve_blueprint(state, player->id);
    update_player_attack(player, player_blueprint, &routed_input, context->bindings);
    Allocator anim_alloc = allocator_arena(&state->gamedata_arena);
    update_entity_anim_attrs(&anim_alloc, player);
}

/* Sets moving/facing and the built-in state/direction attrs from a
 * per-frame move step -- shared by behavior_npc_patrol and behavior_chase
 * (S6.9b, D30) so both feed the generic animation pass (advance_entity_
 * animation, below the behavior table) the same way update_player's own
 * movement code does, without duplicating the facing/attr-setting logic
 * twice. Frame cycling itself is no longer done here -- see S6.11a, D31. */
static void animate_walking_entity(Allocator *alloc, Entity *entity, Vector2 step)
{
    entity->moving = step.x != 0.0F || step.y != 0.0F;
    if (entity->moving) {
        entity->facing = facing_from_step(step);
    }
    update_entity_anim_attrs(alloc, entity);
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
    Allocator anim_alloc = allocator_arena(&state->gamedata_arena);
    if (patrol_period <= 0.0F || context->delta_time <= 0.0F) {
        animate_walking_entity(&anim_alloc, entity, (Vector2){0});
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
    animate_walking_entity(&anim_alloc, entity, step);
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
    Allocator anim_alloc = allocator_arena(&state->gamedata_arena);
    animate_walking_entity(&anim_alloc, entity, step);
    resolve_entity_obstacles(state, context->entity_index);
}

/* Default speed (px/s) and lifetime (s) a `projectile` entity falls back to
 * when its blueprint authors no `speed`/`projectile_lifetime` attr. */
#define PROJECTILE_DEFAULT_SPEED 200.0F
#define PROJECTILE_DEFAULT_LIFETIME_SECONDS 2.0F

/* Straight-line mover for spawn-based ranged attacks (S6.10d, D26): moves
 * `facing * speed * delta_time` every frame -- no steering, no
 * resolve_entity_obstacles, so a projectile flies straight through solid
 * geometry rather than stopping at (or vanishing on) a wall; see TODO.md
 * for that deferred polish. Damage is NOT this function's job -- a
 * projectile blueprint authors contact_damage/damage/destroy_on_hit and
 * rides the existing detect_contact_damage pass (S6.10c) unchanged.
 *
 * Lifetime: entity->projectile_lifetime_timer (runtime-only, same footing
 * as patrol_phase/iframe_timer -- entity_init's memset zeroes it, never
 * emitted to TOML) counts down from the scoped `projectile_lifetime` attr.
 * A freshly spawned entity has no per-instance init hook, so the timer
 * being exactly 0 doubles as "never initialized yet": the very first
 * update seeds it from the attr, every later update just decrements. This
 * is safe because the same frame the timer reaches 0 also soft-destroys
 * the entity (active = false) -- the active check at the top of this
 * function short-circuits every later call before the seed branch could
 * ever misfire on a genuinely-expired timer.
 *
 * Soft-destroyed entities (active = false, set here on lifetime expiry, or
 * by detect_contact_damage's destroy_on_hit) must stop moving -- unlike
 * the hitbox/hurtbox/contact-damage passes, game_update's per-entity
 * behavior dispatch loop (below) has no active gate of its own, so every
 * behavior that can leave an entity soft-destroyed must self-check. */
static void behavior_projectile(BehaviorContext *context)
{
    GameState *state = context->state;
    Entity *entity = &state->gamedata.current_level.entities.data[context->entity_index];
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    if (!attr_get_scoped_bool(&entity->attrs, defaults, "active", true)) {
        return;
    }

    if (entity->projectile_lifetime_timer <= 0.0F) {
        entity->projectile_lifetime_timer =
            attr_get_scoped_float(&entity->attrs, defaults, "projectile_lifetime", PROJECTILE_DEFAULT_LIFETIME_SECONDS);
    }

    float speed = attr_get_scoped_float(&entity->attrs, defaults, "speed", PROJECTILE_DEFAULT_SPEED);
    entity->position.x += entity->facing.x * speed * context->delta_time;
    entity->position.y += entity->facing.y * speed * context->delta_time;

    entity->projectile_lifetime_timer -= context->delta_time;
    if (entity->projectile_lifetime_timer <= 0.0F) {
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        (void)attr_set_bool(&alloc, &entity->attrs, "active", false);
    }
}

static const struct {
    const char *name;
    BehaviorUpdateFn fn;
} behavior_table[] = {
    {"static", behavior_static}, {"player", behavior_player},         {"npc_patrol", behavior_npc_patrol},
    {"chase", behavior_chase},   {"projectile", behavior_projectile}, {nullptr, nullptr},
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

/* Decrements the per-entity combat timers (S6.10a/S6.11b, D26/D31) by
 * delta_time, clamped at 0 so they never go negative: iframe_timer
 * (invincibility window after a hit), attack_state_timer (how much longer
 * this entity's `state` resolves to "attack" -- set by the S6.10b attack
 * activator, just honored here), and hurt_state_timer (how much longer it
 * resolves to "hurt" -- set by begin_hurt_state below). Runs once per
 * frame for every entity in the level, mirroring update_child_positions'
 * "for each entity, mutate" shape. death_state_timer is ticked separately
 * by tick_death_state, since it also needs to soft-destroy the entity once
 * it reaches 0 (an Allocator this function doesn't otherwise need). */
static void tick_combat_timers(Level *level, float delta_time)
{
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        entity->iframe_timer = fmaxf(0.0F, entity->iframe_timer - delta_time);
        entity->attack_state_timer = fmaxf(0.0F, entity->attack_state_timer - delta_time);
        entity->hurt_state_timer = fmaxf(0.0F, entity->hurt_state_timer - delta_time);
    }
}

/* Ticks death_state_timer down for every `dying` entity and soft-destroys
 * it (`active` attr false) once the timer reaches (or starts at) 0 -- see
 * begin_death_state below for how `dying`/death_state_timer get set. A
 * `death`-clip entity has a positive timer at first (so this decrements it
 * across several frames, letting the clip play out before deactivating);
 * a clip-less entity's timer is already 0 the frame it starts dying, so
 * this deactivates it the very next time tick_death_state runs -- one
 * frame after `dying` was set, NOT the same frame. That one-frame
 * deferral is deliberate, not an oversight: this pass runs at the top of
 * game_update, before this frame's own detect_melee_damage/detect_contact_
 * damage (which is what sets `dying` for a freshly-defeated entity) and
 * rules_evaluate_batch (rule.c). rules_evaluate_batch skips inactive
 * entities entirely, so deactivating synchronously in the same frame
 * TRIGGER_DEFEAT is pushed would make the target invisible to its own
 * `defeat` rule before that rule ever runs. Re-setting `active` to false
 * every frame once already false is a harmless no-op (attr_set_bool
 * overwrites the existing bool value in place, no arena growth). */
static void tick_death_state(Level *level, Allocator *alloc, float delta_time)
{
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        if (!entity->dying) {
            continue;
        }
        if (entity->death_state_timer > 0.0F) {
            entity->death_state_timer = fmaxf(0.0F, entity->death_state_timer - delta_time);
        }
        if (entity->death_state_timer <= 0.0F) {
            (void)attr_set_bool(alloc, &entity->attrs, "active", false);
        }
    }
}

/* Fallback hurt_state_timer length (S6.11b, D31) when the target's
 * blueprint authors no `hurt` clip -- a short flinch, comfortably shorter
 * than ENTITY_DEFAULT_IFRAME_SECONDS so the hurt pose clears well before
 * the target can be hit again. */
#define HURT_STATE_DEFAULT_SECONDS 0.3F

/* Starts target's hurt state (S6.11b, D31): called right after a landed
 * entity_apply_damage from both detect_melee_damage and
 * detect_contact_damage below, so any source of combat damage (melee or
 * contact/projectile) triggers the same hurt animation. Duration comes
 * from the target's own `hurt` clip (frames/speed), or
 * HURT_STATE_DEFAULT_SECONDS if it has none. */
static void begin_hurt_state(GameState *state, Entity *target)
{
    const Blueprint *blueprint = entity_resolve_blueprint(state, target->id);
    const AnimClip *hurt_clip = blueprint ? blueprint_find_anim_clip(blueprint, "hurt") : nullptr;
    target->hurt_state_timer = anim_clip_duration(hurt_clip, HURT_STATE_DEFAULT_SECONDS);
}

/* Starts target's terminal death state (S6.11b, D31): called from
 * detect_melee_damage/detect_contact_damage at the exact point they push
 * TRIGGER_DEFEAT (health just crossed from > 0 to <= 0). Sets `dying`
 * (permanent -- see resolve_effective_anim_state's priority order below)
 * and, if the target's blueprint authors a `death` clip, resets its
 * animation to frame 0 and starts death_state_timer at that clip's
 * duration. With no `death` clip, death_state_timer is left at its
 * entity_init default of 0 -- tick_death_state (above) is what actually
 * soft-destroys the entity in EITHER case, not this function: it must NOT
 * happen synchronously here, in the same frame TRIGGER_DEFEAT is pushed,
 * because rules_evaluate_batch (rule.c) skips inactive entities, and this
 * runs before that same frame's rule evaluation gets a chance to fire the
 * target's own `defeat` rule. */
static void begin_death_state(GameState *state, Entity *target)
{
    target->dying = true;
    const Blueprint *blueprint = entity_resolve_blueprint(state, target->id);
    const AnimClip *death_clip = blueprint ? blueprint_find_anim_clip(blueprint, "death") : nullptr;
    if (!death_clip) {
        return;
    }
    target->death_state_timer = anim_clip_duration(death_clip, 0.0F);
    target->frame_index = 0;
    target->frame_timer = 0.0F;
}

/* One-hit-per-frame melee resolution (S6.10a/S6.11b, D26/D31): every
 * entity A whose hitbox entity_hitbox_is_active says is currently live
 * damages every OTHER active entity T (that has a `health` attr) whose
 * hurtbox its hitbox overlaps. entity_apply_damage itself rejects a T
 * still inside its own i-frame window, so a T hit by several overlapping
 * hitboxes in the same frame is only damaged once -- the iframe_timer set
 * on the first hit blocks the rest with no separate "already hit this
 * frame" bookkeeping needed. A landed hit also starts T's hurt state
 * (begin_hurt_state) and, if it drops health from > 0 to <= 0, both
 * starts T's death state (begin_death_state) and pushes TRIGGER_DEFEAT
 * into out_events so it fires in this same frame's rules_evaluate_batch,
 * mirroring how execute_set_attr_action/execute_add_attr_action (rule.c)
 * push the same event when a rule-driven health write crosses zero;
 * gating on "old health was > 0" (not just "new health <= 0") keeps an
 * already-defeated entity that gets hit again (once its i-frames lapse)
 * from re-firing defeat every subsequent hit. A landed hit also calls
 * entity_apply_knockback (S6.10c, D26) with the attacker's `knockback`
 * attr (default 0, a no-op) -- the resulting push is resolved against
 * solids by the knockback tick pass below, alongside detect_contact_
 * damage's own knockback calls. */
static void detect_melee_damage(GameState *state, Level *level, Allocator *alloc, vec_trigger_event *out_events)
{
    Entity *entities = level->entities.data;
    int entity_count = level->entities.count;
    for (int attacker_index = 0; attacker_index < entity_count; attacker_index++) {
        const AttrSet *attacker_defaults = entity_resolve_defaults(state, entities[attacker_index].id);
        if (!entity_hitbox_is_active(&entities[attacker_index], attacker_defaults)) {
            continue;
        }
        CollisionPrimitive hitbox_prim_storage;
        CollisionShape hitbox =
            entity_hitbox_region(&entities[attacker_index], attacker_defaults, &hitbox_prim_storage);
        float damage = attr_get_scoped_float(&entities[attacker_index].attrs, attacker_defaults, "damage", 0.0F);
        float knockback_distance =
            attr_get_scoped_float(&entities[attacker_index].attrs, attacker_defaults, "knockback", 0.0F);

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
            begin_hurt_state(state, &entities[target_index]);
            entity_apply_knockback(&entities[target_index], entities[attacker_index].position, knockback_distance);
            float new_health = attr_get_scoped_float(&entities[target_index].attrs, target_defaults, "health", 0.0F);
            if (old_health > 0.0F && new_health <= 0.0F) {
                begin_death_state(state, &entities[target_index]);
                (void)vec_trigger_event_push(out_events,
                                             (TriggerEvent){.type = TRIGGER_DEFEAT, .entity_index = target_index});
            }
        }
    }
}

/* Contact damage pass (S6.10c, D26): every entity A with a truthy
 * contact_damage attr damages every OTHER active entity T (that has a
 * `health` attr) whose COLLISION region -- the physical body, not a
 * hitbox/hurtbox -- it overlaps, through the same entity_apply_damage/
 * entity_apply_knockback path detect_melee_damage above uses, so the
 * damage formula, i-frames, hurt/death state (S6.11b, D31), and defeat
 * wiring all apply identically. i-frames de-dupe repeated per-frame
 * contact the same way they de-dupe repeated melee hits -- no separate
 * "already hit this frame" bookkeeping. Melee (hitbox) and contact (body)
 * are distinct damage sources; an entity could in principle have both
 * live at once, but a target's i-frames still collapse that to at most
 * one applied hit per window.
 *
 * destroy_on_hit (S6.10d, D26): if A also has a truthy destroy_on_hit
 * attr, a landed hit soft-destroys A (active = false) instead of leaving
 * it to keep hurting whatever it touches -- a projectile blueprint authors
 * this so it vanishes on impact rather than phasing through its target.
 * entity_can_deal_contact_damage's active check (absent before this
 * feature) is what makes that stick: without it, a "destroyed" attacker
 * sitting on top of its target would resume dealing damage the moment the
 * target's i-frames lapse, since nothing else in this loop reads the
 * attacker's own active state. */
static bool entity_can_deal_contact_damage(const Entity *entity, const AttrSet *defaults)
{
    return attr_get_scoped_bool(&entity->attrs, defaults, "active", true) &&
           attr_get_scoped_bool(&entity->attrs, defaults, "contact_damage", false);
}

static void detect_contact_damage(GameState *state, Level *level, Allocator *alloc, vec_trigger_event *out_events)
{
    Entity *entities = level->entities.data;
    int entity_count = level->entities.count;
    for (int attacker_index = 0; attacker_index < entity_count; attacker_index++) {
        const AttrSet *attacker_defaults = entity_resolve_defaults(state, entities[attacker_index].id);
        if (!entity_can_deal_contact_damage(&entities[attacker_index], attacker_defaults)) {
            continue;
        }
        CollisionPrimitive attacker_prim_storage;
        CollisionShape attacker_shape =
            entity_collision_region(&entities[attacker_index], attacker_defaults, &attacker_prim_storage);
        float damage = attr_get_scoped_float(&entities[attacker_index].attrs, attacker_defaults, "damage", 0.0F);
        float knockback_distance =
            attr_get_scoped_float(&entities[attacker_index].attrs, attacker_defaults, "knockback", 0.0F);

        for (int target_index = 0; target_index < entity_count; target_index++) {
            if (target_index == attacker_index) {
                continue;
            }
            const AttrSet *target_defaults = entity_resolve_defaults(state, entities[target_index].id);
            if (!attr_get_scoped(&entities[target_index].attrs, target_defaults, "health") ||
                !attr_get_scoped_bool(&entities[target_index].attrs, target_defaults, "active", true)) {
                continue;
            }
            CollisionPrimitive target_prim_storage;
            CollisionShape target_shape =
                entity_collision_region(&entities[target_index], target_defaults, &target_prim_storage);
            bool overlapping = composite_overlap(&attacker_shape, entities[attacker_index].position, 0.0F,
                                                 &target_shape, entities[target_index].position, 0.0F);
            if (!overlapping) {
                continue;
            }
            float old_health = attr_get_scoped_float(&entities[target_index].attrs, target_defaults, "health", 0.0F);
            if (!entity_apply_damage(&entities[target_index], target_defaults, damage, alloc)) {
                continue;
            }
            begin_hurt_state(state, &entities[target_index]);
            entity_apply_knockback(&entities[target_index], entities[attacker_index].position, knockback_distance);
            float new_health = attr_get_scoped_float(&entities[target_index].attrs, target_defaults, "health", 0.0F);
            if (old_health > 0.0F && new_health <= 0.0F) {
                begin_death_state(state, &entities[target_index]);
                (void)vec_trigger_event_push(out_events,
                                             (TriggerEvent){.type = TRIGGER_DEFEAT, .entity_index = target_index});
            }
            if (attr_get_scoped_bool(&entities[attacker_index].attrs, attacker_defaults, "destroy_on_hit", false)) {
                (void)attr_set_bool(alloc, &entities[attacker_index].attrs, "active", false);
                break;
            }
        }
    }
}

/* Knockback tick/apply pass (S6.10c, D26): every entity with a live
 * knockback_timer (set by entity_apply_knockback from a landed melee or
 * contact hit) moves by its current decaying step, then is resolved
 * against solids by resolve_entity_obstacles -- the same push-out every
 * other mover in this file uses -- so a knockback can't shove a target
 * through a wall. knockback_velocity is the FULL initial velocity
 * entity_apply_knockback computed for a linear decay to zero over
 * KNOCKBACK_SECONDS; scaling it by the remaining-time fraction
 * (knockback_timer / KNOCKBACK_SECONDS) before each step reproduces that
 * decay frame by frame. A knockback_timer of 0 (never hit, or hit by an
 * attacker with no/zero `knockback` attr) is a no-op -- existing movement
 * is unaffected. */
static void tick_knockback(GameState *state, float delta_time)
{
    Level *level = &state->gamedata.current_level;
    for (int index = 0; index < level->entities.count; index++) {
        Entity *entity = &level->entities.data[index];
        if (entity->knockback_timer <= 0.0F) {
            continue;
        }
        float fraction = entity->knockback_timer / KNOCKBACK_SECONDS;
        entity->position.x += entity->knockback_velocity.x * fraction * delta_time;
        entity->position.y += entity->knockback_velocity.y * fraction * delta_time;
        entity->knockback_timer = fmaxf(0.0F, entity->knockback_timer - delta_time);
        resolve_entity_obstacles(state, index);
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

/* --- Generic animation state machine (S6.11a/S6.11b, D31) ---
 *
 * Replaces the player's old hardcoded ANIM_WALK_DOWN/SIDE/UP row constants
 * and the per-behavior frame-cycling code above: any entity whose blueprint
 * authors [[blueprint.animation]] clips now animates through this single
 * pass, keyed off the built-in `state`/`direction` attrs the behaviors
 * above set via update_entity_anim_attrs, overridden by
 * resolve_effective_anim_state below whenever a combat state (attack/hurt/
 * death) is live. */

/* Fallback state/direction for an entity with neither attr authored (no
 * behavior has run for it yet, or its behavior never sets them) --
 * standing still, facing down, matching entity_init's default `facing`. */
#define ANIM_STATE_DEFAULT "idle"
#define ANIM_DIRECTION_DEFAULT "down"

/* Row offset added to a clip's authored `row` to reach the down/side/up
 * sub-row within the sprite sheet -- preserves the layout the player's old
 * ANIM_WALK_DOWN/SIDE/UP constants used (row 3 = down, 4 = side, 5 = up): a
 * walk clip authored with row = 3 reproduces that exactly. `left` and
 * `right` share the side row; the caller's `flip` picks the mirror. */
#define ANIM_ROW_OFFSET_DOWN 0
#define ANIM_ROW_OFFSET_SIDE 1
#define ANIM_ROW_OFFSET_UP 2

static int anim_row_offset_for_direction(const char *direction)
{
    if (strcmp(direction, "up") == 0) {
        return ANIM_ROW_OFFSET_UP;
    }
    if (strcmp(direction, "left") == 0 || strcmp(direction, "right") == 0) {
        return ANIM_ROW_OFFSET_SIDE;
    }
    return ANIM_ROW_OFFSET_DOWN;
}

/* Looks up a clip for the entity's exact current state, falling back to
 * "walk" then "idle" so a blueprint that only authors one or two states
 * still animates something reasonable (e.g. a not-yet-authored "attack"
 * state falls back to "walk" rather than freezing). nullptr if the
 * blueprint has no clip under any of the three names. */
static const AnimClip *find_entity_anim_clip(const Blueprint *blueprint, const char *state_name)
{
    const AnimClip *clip = blueprint_find_anim_clip(blueprint, state_name);
    if (clip) {
        return clip;
    }
    clip = blueprint_find_anim_clip(blueprint, "walk");
    if (clip) {
        return clip;
    }
    return blueprint_find_anim_clip(blueprint, "idle");
}

/* Advances frame_index at clip->speed frames/second, wrapping at
 * clip->frames -- the same frame_timer-accumulator math update_player used
 * to do directly. A clip with one frame or non-positive speed (an "idle"
 * clip's usual authoring: frames = 1) holds frame 0 instead. */
static void advance_entity_frame(Entity *entity, const AnimClip *clip, float delta_time)
{
    if (clip->frames <= 1 || clip->speed <= 0.0F) {
        entity->frame_index = 0;
        entity->frame_timer = 0.0F;
        return;
    }
    entity->frame_timer += delta_time * clip->speed;
    if (entity->frame_timer >= 1.0F) {
        entity->frame_timer -= 1.0F;
        entity->frame_index = (entity->frame_index + 1) % clip->frames;
    }
}

/* Overrides a behavior's walk/idle `state` with whichever transient combat
 * state is currently live (S6.11b, D31), highest priority first: `death`
 * (dying is terminal -- once set it always wins, unlike the timers below
 * which only override while positive), then `hurt` (hurt_state_timer > 0),
 * then `attack` (attack_state_timer > 0). Falls through to
 * behavior_state (whatever update_entity_anim_attrs/animate_walking_entity
 * last wrote) once none of the combat timers are live. */
static const char *resolve_effective_anim_state(const Entity *entity, const char *behavior_state)
{
    if (entity->dying) {
        return "death";
    }
    if (entity->hurt_state_timer > 0.0F) {
        return "hurt";
    }
    if (entity->attack_state_timer > 0.0F) {
        return "attack";
    }
    return behavior_state;
}

/* One entity's animation tick: resolve the effective `state` (the
 * behavior-set `state` attr, default "idle", overridden by
 * resolve_effective_anim_state whenever a combat state is live) and use
 * it -- NOT the raw behavior-set state -- as the clip lookup key. The
 * effective state is deliberately NOT written back onto the entity's
 * `state` attr: that attr is the behaviors' own read/write channel
 * (update_entity_anim_attrs sets it from movement every frame this pass
 * reads it back from), and an entity with no behavior driving it (e.g. a
 * static combat target) would otherwise never overwrite a stale "hurt"/
 * "attack"/"death" value this pass wrote on some earlier frame -- the
 * combat state would "stick" forever once entered, since nothing else
 * ever resets `state` back to idle for such an entity. Callers that need
 * to observe the effective combat state (tests included) read the
 * entity's own `dying`/`hurt_state_timer`/`attack_state_timer` fields
 * directly, the same way existing tests already read `moving`/
 * `frame_index` directly rather than through an attr. A no-op for any
 * entity whose blueprint authors no [[blueprint.animation]] clips at all
 * -- it keeps rendering through the static get_source_rect path (main.c).
 * Takes the already-dereferenced Entity* (the caller's own loop variable)
 * rather than re-deriving from an index -- safe because the behavior that
 * just ran only ever mutates the entity's own attrs/collision vecs, never
 * the level's entities array itself, the same assumption behavior_player
 * already relies on when it keeps using `player` after
 * resolve_entity_obstacles. */
static void advance_entity_animation(GameState *state, Entity *entity, float delta_time)
{
    const Blueprint *blueprint = entity_resolve_blueprint(state, entity->id);
    if (!blueprint || blueprint->animation.count == 0) {
        return;
    }

    const char *behavior_state = attr_get_scoped_string(&entity->attrs, &blueprint->attrs, "state");
    if (!behavior_state) {
        behavior_state = ANIM_STATE_DEFAULT;
    }
    const char *effective_state = resolve_effective_anim_state(entity, behavior_state);

    const char *direction = attr_get_scoped_string(&entity->attrs, &blueprint->attrs, "direction");
    if (!direction) {
        direction = ANIM_DIRECTION_DEFAULT;
    }

    const AnimClip *clip = find_entity_anim_clip(blueprint, effective_state);
    if (!clip) {
        return;
    }

    entity->anim_row = clip->row + anim_row_offset_for_direction(direction);
    entity->flip = strcmp(direction, "left") == 0;
    advance_entity_frame(entity, clip, delta_time);
}

void game_update(Diag *diag, GameState *state, InputState input, float delta_time)
{
    state->frame++;
    state->elapsed += delta_time;
    music_state_tick(&state->music, delta_time);

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
            advance_entity_animation(state, entity, delta_time);
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

        /* Combat timer tick (S6.10a/S6.11b, D26/D31) -- decrement before
         * this frame's melee pass below reads them, so a timer set by
         * scenario setup or the S6.10b attack activator is honored as
         * active THIS frame, not the frame after. combat_alloc is declared
         * here (rather than just above detect_melee_damage/
         * detect_contact_damage further down) so tick_death_state can also
         * use it to soft-destroy an entity whose death animation just
         * finished. */
        Allocator combat_alloc = allocator_arena(&state->gamedata_arena);
        tick_combat_timers(&state->gamedata.current_level, delta_time);
        tick_death_state(&state->gamedata.current_level, &combat_alloc, delta_time);

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

        /* Melee + contact damage passes (S6.10a/c, D26) -- must run before
         * the trigger_events.count check below, since a defeat either pass
         * detects needs to feed into THIS frame's rules_evaluate_batch. */
        detect_melee_damage(state, &state->gamedata.current_level, &combat_alloc, &trigger_events);
        detect_contact_damage(state, &state->gamedata.current_level, &combat_alloc, &trigger_events);

        /* Knockback tick/apply pass (S6.10c, D26) -- after both damage
         * passes above so a hit landed THIS frame starts moving THIS
         * frame too, not one frame later. */
        tick_knockback(state, delta_time);

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

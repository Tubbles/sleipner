#pragma once

#include "alloc.h"
#include "strv.h"
#include "vec.h" // IWYU pragma: export

#include "raylib.h"

/* --- EffectQueue: the channel stub rule actions use to reach the world ---
 *
 * Generalizes the TransitionRequest pattern (rule.h) for effects that fire
 * every frame (camera_shake, sound) rather than rarely (level transitions).
 * Lives on GameState, NOT GamedataState -- it is not undo-snapshotted.
 * Backed by progression_arena: scratch-independent, survives the
 * level-transition/hot-reload arena_restore that rewinds gamedata_arena,
 * and its lifecycle matches the queue exactly (init at game_init, re-init
 * at game_reset_progression, freed wholesale with progression_arena at
 * game_free).
 *
 * This module is a PURE channel: push helpers and effect_queue_clear only,
 * no GameState access and no side effects. The per-frame apply pass that
 * actually handles each effect type (looks up sounds in the SFX registry,
 * moves the camera, spawns entities) lives in frame.c instead
 * (apply_effect_queue), since it needs GameState (registry, audio
 * device, camera) that this header deliberately does not depend on.
 * frame.c calls it once per frame, right after game_update returns, and it
 * clears the queue via effect_queue_clear at the end. Dialogue is NOT one
 * of these effects (S6.7c, D24): it must block the triggering rule, which
 * this fire-and-forget queue can't express, so ACTION_DIALOGUE writes
 * directly into GameState.dialogue instead -- see rule.h's DialogueState
 * and ActionContext.dialogue doc comments.
 *
 * STRING LIFETIME RULE -- read before pushing a Strv into any of these:
 * every Strv pushed here (sound name, spawn blueprint) is NON-OWNING,
 * unlike TransitionRequest.level (an owning Str). Transitions
 * are rare and event-bounded, so copying the level name once is cheap.
 * Effects can be pushed every frame (camera_shake, sound), so an
 * owning-copy-per-effect-per-frame would grow progression_arena without
 * bound. Pushed Strv values MUST reference memory that outlives the
 * same-frame drain: action arguments live in gamedata_arena (the parsed
 * rule tree, never rewound mid-frame), so they stay valid through the
 * drain that runs right after game_update. NEVER point a pushed Strv at
 * scratch_arena -- drain runs after game_update returns, by which point
 * any SCRATCH_SCOPE opened during that update has already unwound and the
 * memory may be reused. */

typedef struct {
    Strv name;
} SoundEffectRequest;

VEC_DECL(sound_effect_request, SoundEffectRequest)

typedef struct {
    Vector2 target;
    float duration;
} CameraPanRequest;

VEC_DECL(camera_pan_request, CameraPanRequest)

typedef struct {
    float magnitude;
    float duration;
} CameraShakeRequest;

VEC_DECL(camera_shake_request, CameraShakeRequest)

typedef struct {
    Strv blueprint;
    float x;
    float y;
} SpawnRequest;

VEC_DECL(spawn_request, SpawnRequest)

typedef struct {
    vec_sound_effect_request sounds;
    vec_camera_pan_request camera_pans;
    vec_camera_shake_request camera_shakes;
    vec_spawn_request spawns;
} EffectQueue;

/* Initialize all four vecs against `alloc` (progression_arena in
 * production -- see GameState.effects). Call at game_init, and again at
 * game_reset_progression since arena_reset there invalidates the previous
 * vecs' backing storage. */
void effect_queue_init(EffectQueue *queue, Allocator alloc);

/* Clear all four vecs: count -> 0, capacity retained (see CLAUDE.md "Vec
 * growth and pointer stability"). Capacity is bounded by the most effects
 * ever pushed in a single frame, not by frame count, so repeated
 * push-then-clear cycles never grow progression_arena. */
void effect_queue_clear(EffectQueue *queue);

/* Push helpers, one per request type. `name`/`blueprint` fields must
 * satisfy the string-lifetime rule documented above. Each returns false
 * only if the backing vec push failed to allocate. */
[[nodiscard]] bool effect_queue_push_sound(EffectQueue *queue, Strv name);
[[nodiscard]] bool effect_queue_push_camera_pan(EffectQueue *queue, Vector2 target, float duration);
[[nodiscard]] bool effect_queue_push_camera_shake(EffectQueue *queue, CameraShakeRequest request);
[[nodiscard]] bool effect_queue_push_spawn(EffectQueue *queue, Strv blueprint, Vector2 position);

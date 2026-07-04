#pragma once

#include "alloc.h"
#include "diag.h"
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
 * game_free). frame.c drains it once per frame, right after game_update
 * returns, then clears it -- see effect_queue_drain.
 *
 * STRING LIFETIME RULE -- read before pushing a Strv into any of these:
 * every Strv pushed here (sound name, spawn blueprint, dialogue text) is
 * NON-OWNING, unlike TransitionRequest.level (an owning Str). Transitions
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
    Strv text;
} DialogueRequest;

VEC_DECL(dialogue_request, DialogueRequest)

typedef struct {
    vec_sound_effect_request sounds;
    vec_camera_pan_request camera_pans;
    vec_camera_shake_request camera_shakes;
    vec_spawn_request spawns;
    vec_dialogue_request dialogues;
} EffectQueue;

/* Initialize all five vecs against `alloc` (progression_arena in
 * production -- see GameState.effects). Call at game_init, and again at
 * game_reset_progression since arena_reset there invalidates the previous
 * vecs' backing storage. */
void effect_queue_init(EffectQueue *queue, Allocator alloc);

/* Clear all five vecs: count -> 0, capacity retained (see CLAUDE.md "Vec
 * growth and pointer stability"). Capacity is bounded by the most effects
 * ever pushed in a single frame, not by frame count, so repeated
 * push-then-clear cycles never grow progression_arena. */
void effect_queue_clear(EffectQueue *queue);

/* Push helpers, one per request type. `name`/`blueprint`/`text` fields
 * must satisfy the string-lifetime rule documented above. Each returns
 * false only if the backing vec push failed to allocate. */
[[nodiscard]] bool effect_queue_push_sound(EffectQueue *queue, Strv name);
[[nodiscard]] bool effect_queue_push_camera_pan(EffectQueue *queue, Vector2 target, float duration);
[[nodiscard]] bool effect_queue_push_camera_shake(EffectQueue *queue, CameraShakeRequest request);
[[nodiscard]] bool effect_queue_push_spawn(EffectQueue *queue, Strv blueprint, Vector2 position);
[[nodiscard]] bool effect_queue_push_dialogue(EffectQueue *queue, Strv text);

/* Drain the queue: log every pending effect via debug_log, then clear all
 * five vecs. S6.2 scaffold only -- no real side effects land here yet;
 * those are S6.4 (sound) through S6.7 (dialogue). Headless: takes Diag,
 * not a window or GameState, so it is testable without raylib init. */
void effect_queue_drain(Diag *diag, EffectQueue *queue);

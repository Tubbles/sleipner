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
 * moves the camera, spawns entities, points the toast overlay at a
 * formatted message) lives in frame.c instead (apply_effect_queue), since
 * it needs GameState (registry, audio device, camera) or EditorState
 * (toast overlay) that this header deliberately does not depend on.
 * frame.c calls it once per frame, right after game_update returns, and it
 * clears the queue via effect_queue_clear at the end. Dialogue is NOT one
 * of these effects (S6.7c, D24): it must block the triggering rule, which
 * this fire-and-forget queue can't express, so ACTION_DIALOGUE writes
 * directly into GameState.dialogue instead -- see rule.h's DialogueState
 * and ActionContext.dialogue doc comments.
 *
 * STRING LIFETIME RULE -- read before pushing a Strv into any of these:
 * every Strv pushed here (sound name, spawn blueprint, toast text) is NON-OWNING,
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
    /* Direction the spawned entity (or entities -- see apply_spawn_effects,
     * frame.c) should be oriented in immediately after spawning (S6.10d,
     * D26): copied from the spawning entity's own `facing` at the point
     * ACTION_SPAWN fires (execute_spawn_action, rule.c). Harmless for a
     * non-directional spawn (a static prop just gets a facing nobody
     * reads); essential for a projectile blueprint using the `projectile`
     * behavior, which moves along entity->facing every frame. */
    Vector2 facing;
} SpawnRequest;

VEC_DECL(spawn_request, SpawnRequest)

/* Pickup toast (S6.8b, D25): carries the raw item name, not a pre-formatted
 * "Got X" string -- formatting happens once, in frame.c's apply_toast_effects,
 * which owns the buffer the toast surface reads from for its 2-second
 * lifetime. */
typedef struct {
    Strv text;
} ToastRequest;

VEC_DECL(toast_request, ToastRequest)

typedef struct {
    vec_sound_effect_request sounds;
    vec_camera_pan_request camera_pans;
    vec_camera_shake_request camera_shakes;
    vec_spawn_request spawns;
    vec_toast_request toasts;
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

/* Push helpers, one per request type. `name`/`blueprint`/`text` fields must
 * satisfy the string-lifetime rule documented above. Each returns false
 * only if the backing vec push failed to allocate. effect_queue_push_spawn
 * takes the whole SpawnRequest by value (rather than separate blueprint/
 * position/facing parameters) for the same reason effect_queue_push_
 * camera_shake takes a whole CameraShakeRequest: two adjacent Vector2
 * parameters (position, facing) would be an easily-swappable pair by
 * clang-tidy's bugprone-easily-swappable-parameters, since both share the
 * exact same type. */
[[nodiscard]] bool effect_queue_push_sound(EffectQueue *queue, Strv name);
[[nodiscard]] bool effect_queue_push_camera_pan(EffectQueue *queue, Vector2 target, float duration);
[[nodiscard]] bool effect_queue_push_camera_shake(EffectQueue *queue, CameraShakeRequest request);
[[nodiscard]] bool effect_queue_push_spawn(EffectQueue *queue, SpawnRequest request);
[[nodiscard]] bool effect_queue_push_toast(EffectQueue *queue, Strv text);

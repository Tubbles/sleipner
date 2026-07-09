#pragma once

#include "arena.h"
#include "audio.h"
#include "blueprint.h"
#include "diag.h"
#include "effect.h"
#include "gamedata.h"
#include "input.h"
#include "input_func.h"
#include "network.h"
#include "preferences.h"
#include "progression.h"
#include "random.h"
#include "raylib.h"
#include "rect.h"
#include "strv.h"

#include <stdbool.h>

#define MAX_TEXTURE_FILENAME 64
#define FONT_NAME_LEN 64
#define SCREEN_WIDTH_DEFAULT 800
#define SCREEN_HEIGHT_DEFAULT 600

#define DEFAULT_PLAYER_SPEED 80.0F
#define FRAME_SIZE 32

typedef struct {
    char filename[MAX_TEXTURE_FILENAME];
    Texture2D texture;
} TextureEntry;

VEC_DECL(texture_entry, TextureEntry)

typedef struct {
    char name[FONT_NAME_LEN];
    Font font;
    bool valid;
} FontPreviewEntry;

VEC_DECL(font_preview, FontPreviewEntry)

/* One shared Font keyed on (asset data pointer, pixel size). ASSET()
 * symbols live in .rodata, so pointer identity is a stable cache key.
 * The cache is the SOLE owner of every Font it hands out — see
 * font_cache.h for the ownership contract. */
typedef struct {
    const unsigned char *data;
    int size;
    Font font;
} FontCacheEntry;

VEC_DECL(font_cache, FontCacheEntry)

typedef struct {
    vec_texture_entry textures;
    vec_font_preview font_previews;
    Font ui_font;
    vec_font_cache font_cache;
    /* Embedded SFX registry (S6.4, D32) -- see audio.h's MAP_DECL(strv_sound, ...)
     * for the full lifecycle/lookup contract. Same persistent, below-
     * gamedata_base lifetime as textures/font_cache above. */
    map_strv_sound sounds;
    /* Embedded music registry (S6.13b, D32) -- see audio.h's
     * MAP_DECL(strv_music, ...) for the full lifecycle/lookup contract.
     * Same persistent, below-gamedata_base lifetime as sounds above. */
    map_strv_music music;
} AssetRegistry;

/* --- CameraEffect: runtime pan/shake state started by the camera_pan/
 * camera_shake rule actions (S6.5, D22/D26) ---
 *
 * Lives on GameState, NOT GamedataState: it is transient per-session
 * effect state, not undo-snapshotted, and must not bleed across a level
 * change -- game_snap_camera zeroes it every time the camera itself is
 * snapped (fresh load, hot-reload, level transition), so a pan/shake
 * in flight never survives into the next level.
 *
 * `pan` overrides camera_update_target's normal player-follow while
 * active (game.c): from/target/duration/elapsed drive the pure
 * camera_pan_position lerp, and `active` clears itself once elapsed
 * reaches duration, letting follow resume next frame.
 *
 * `shake` has no separate `active` flag -- it is implicitly inactive
 * whenever elapsed >= duration (the zero-initialized default), same
 * convention camera_shake_magnitude uses. `offset` is computed once per
 * frame in game_update (not render) so it is headless-observable; the
 * gameplay camera assembly (main.c) adds it to camera_target as a
 * visual-only jitter, never writing it back into camera_target itself. */
typedef struct {
    bool active;
    Vector2 from;
    Vector2 target;
    float duration;
    float elapsed;
} CameraPanEffect;

typedef struct {
    float magnitude;
    float duration;
    float elapsed;
    Vector2 offset;
} CameraShakeEffect;

typedef struct {
    CameraPanEffect pan;
    CameraShakeEffect shake;
} CameraEffect;

/* --- TransitionFade: fade-to-black state machine driving level
 * transitions (S6.14, D27) ---
 *
 * Lives on GameState, NOT GamedataState: like CameraEffect above, it is
 * transient per-session state, not undo-snapshotted. Unlike CameraEffect,
 * it is NOT reset by game_snap_camera -- the swap step of the state
 * machine below calls game_snap_camera itself while `fade.phase` is
 * already mid-flight (about to become FADE_IN), so resetting `fade`
 * there would stomp the very transition it belongs to. See frame.h's
 * handle_transition doc comment for the full driver.
 *
 * `phase` starts NONE (idle). frame.c's handle_transition moves it to
 * FADE_OUT (`timer = TRANSITION_FADE_SECONDS`) when a `transition:`
 * rule action fires. transition_fade_tick below counts `timer` down
 * each frame; at 0 during FADE_OUT it reports `do_swap = true` (the
 * frame the level swap itself must run, at the fully-black midpoint)
 * and moves to FADE_IN with a fresh timer; at 0 during FADE_IN it
 * returns to NONE. Player input is suppressed by the caller (frame.c's
 * run_active_frame) for every frame `phase != TRANSITION_FADE_NONE`. */
#define TRANSITION_FADE_SECONDS 0.3F

typedef enum {
    TRANSITION_FADE_NONE,
    TRANSITION_FADE_OUT,
    TRANSITION_FADE_IN,
} TransitionFadePhase;

typedef struct {
    TransitionFadePhase phase;
    float timer;
} TransitionFade;

/* Result of one transition_fade_tick call: `fade` is the new (phase,
 * timer) to store back onto GameState.fade; `do_swap` is true exactly
 * on the frame FADE_OUT's timer reaches zero, telling handle_transition
 * to run the level-swap body THIS frame and no other. Bundling phase
 * and timer into the TransitionFade struct (rather than passing them as
 * two loose parameters) is also what keeps transition_fade_tick's and
 * transition_fade_alpha's own parameter lists from tripping clang-tidy's
 * bugprone-easily-swappable-parameters: a bare (TransitionFadePhase,
 * float) pair is flagged because the enum implicitly converts to
 * float. */
typedef struct {
    TransitionFade fade;
    bool do_swap;
} TransitionFadeStep;

typedef struct {
    GamedataState gamedata;
    Arena gamedata_arena;
    ArenaCheckpoint gamedata_base; /* offset just above persistent assets (textures, fonts) */
    Arena scratch_arena;
    /* Process-lifetime play progression (flags, global vars, inventory
     * items). Backed by its own arena so it survives game_load_gamedata's
     * arena_restore(gamedata_base) on transitions and hot-reloads (see
     * progression.h). Explicitly cleared by the pause-menu RESTORE
     * action via game_reset_progression. */
    ProgressionState progression;
    Arena progression_arena;
    RectU32 game_bounds;
    int frame;
    float elapsed;
    bool gamedata_loaded;
    bool editor_mode;
    bool debug_enabled;
    bool prev_interact;
    ErrorState error;
    DebugState debug;
    AudioState audio;
    /* Per-level music crossfade state (S6.13b, D32) -- see audio.h's
     * MusicState doc comment for the full design. Runtime session state,
     * not gamedata: not undo-snapshotted, updated by music_on_level_changed
     * (game_load_gamedata below and level_activate) and ticked every frame
     * by game_update. */
    MusicState music;
    /* Runtime session state, not gamedata: seeded once from wall-clock time
     * in game_init and never undo-snapshotted or persisted (see random.h,
     * D21). */
    RandomState rng;
    AssetRegistry assets;
    BindingStore bindings;
    Preferences preferences;
    /* Cached path to preferences.toml resolved at startup via
     * platform_paths. Owned by gamedata_arena (set once, never freed). */
    Str preferences_path;
    long gamedata_mtime;
    int screen_width;
    int screen_height;
    TransitionRequest transition;
    /* Channel stub rule actions use to reach the world (sound, camera_pan,
     * camera_shake, spawn). Backed by progression_arena -- see effect.h
     * for the full lifecycle and string-lifetime rules. Not part of
     * GamedataState: not undo-snapshotted, and it must survive the
     * arena_restore(gamedata_base) that a level transition or hot-reload
     * runs against gamedata_arena. Dialogue is NOT routed through this
     * queue -- see DialogueState below. */
    EffectQueue effects;
    /* Runtime camera_pan/camera_shake state -- see CameraEffect above. */
    CameraEffect camera_effect;
    /* Blocking dialogue state opened directly by ACTION_DIALOGUE (S6.7c,
     * D24, rule.h) -- not part of GamedataState (not undo-snapshotted),
     * and reset to inactive everywhere GamedataState.continuations is
     * reset (game_load_gamedata, setup_current_level_runtime, game.c)
     * since `pages` is allocated from gamedata_arena. See DialogueState's
     * doc comment (rule.h) for the full design. */
    DialogueState dialogue;
    /* Fade-to-black state machine driving level transitions (S6.14,
     * D27) -- see TransitionFade's doc comment above. */
    TransitionFade fade;
    /* Multiplayer session state (S8.3a) -- see network.h's NetworkState
     * doc comment. Not part of GamedataState: not undo-snapshotted, and
     * zero-initializes to NET_OFFLINE so nothing networked happens unless
     * a caller explicitly sets a mode (S8.3b). */
    NetworkState network;
} GameState;

typedef struct {
    const char *toml_string;
    const char *level_name;
    TextureLookupFn texture_lookup;
    void *texture_user_data;
} GamedataParams;

[[nodiscard]] bool game_init(Diag *diag, GameState *state, RectU32 game_bounds);
[[nodiscard]] bool game_load_gamedata(Diag *diag, GameState *state, GamedataParams params);
void game_update(Diag *diag, GameState *state, InputState input, float delta_time);
Entity *game_get_player(GameState *state);
const Entity *game_get_player_const(const GameState *state);
void game_free(Diag *diag, GameState *state);

/* Discard all play progression (flags, global vars, inventory items):
 * resets progression_arena and zeroes state->progression. This is a
 * new-game-style reset, not a reload — callers that also want fresh
 * gamedata must separately call game_load_gamedata. Used by the
 * pause-menu RESTORE action. */
void game_reset_progression(GameState *state);

/* Snap camera to the clamped player position (no lerp). Call after level load
 * or transition. Also zeroes state->camera_effect (S6.5) -- see CameraEffect's
 * doc comment above for why this is the single reset site. */
void game_snap_camera(GameState *state);

/* Pure lerp for an active camera_pan effect: from -> target as elapsed goes
 * 0 -> duration, clamped at both ends (elapsed <= 0 returns from, elapsed >=
 * duration returns target). duration <= 0 also returns target. Exposed for
 * direct unit testing (game_test.c); the only production caller is
 * camera_update_target's pan-override branch (game.c). */
Vector2 camera_pan_position(Vector2 from, Vector2 target, float elapsed, float duration);

/* Pure linear decay for an active camera_shake effect: full `magnitude` at
 * elapsed = 0, decaying to 0 at elapsed >= duration, never negative.
 * duration <= 0 returns 0. Exposed for direct unit testing (game_test.c);
 * the only production caller is game_update's shake-offset computation. */
float camera_shake_magnitude(float magnitude, float elapsed, float duration);

/* Pure fade-to-black step (S6.14, D27): advances `fade` by delta_time
 * and reports whether this is the frame to run the level swap. See
 * TransitionFade's doc comment above for the full state machine.
 * Exposed for direct unit testing (game_test.c); the only production
 * caller is frame.c's handle_transition. */
TransitionFadeStep transition_fade_tick(TransitionFade fade, float delta_time);

/* Pure black-overlay opacity in [0, 1] for the current fade state: 0 at
 * NONE, ramping 0->1 across FADE_OUT, 1 at the swap instant (FADE_IN's
 * first tick), ramping 1->0 across FADE_IN. Exposed for direct unit
 * testing (game_test.c); the only production caller is main.c's
 * render_frame. */
float transition_fade_alpha(TransitionFade fade);

/* Switch the active level in memory: swaps state->gamedata.current_level
 * with the matching entry in state->gamedata.other_levels and rebuilds the
 * per-level runtime state (rule_table, entity_blueprints, player_index,
 * prev_player_overlaps, prev_solid_collisions) for the newly current level.
 * No disk I/O, no re-parse. No-op (returns true) if target_name already
 * names current_level. Returns false if target_name matches neither
 * current_level nor any other_levels entry — the caller should show a
 * toast rather than treat this as a hard error.
 *
 * Does NOT touch undo history. Callers must reset it themselves after a
 * successful switch (clear + push a fresh baseline entry), the same way
 * handle_transition and reset_editor_after_reload do after
 * game_load_gamedata — old undo snapshots reference the previous level's
 * arena bytes and must not be replayed against the new one. */
[[nodiscard]] bool level_activate(Diag *diag, GameState *state, Strv target_name);

/* Rebuild the runtime-derived state tied to the current Level: rule_table,
 * entity_blueprints, player_index, and the prev_player_overlaps /
 * prev_solid_collisions trigger-edge trackers, all resized to the CURRENT
 * entity count. Exposed (rather than file-local to game.c) so any editor
 * flow that spawns a batch of entities directly into current_level (S5.7's
 * paste, editor/core.c) can rebuild every count-parallel tracking structure
 * in one call afterward, instead of hand-patching entity_blueprints/
 * rule_table per spawn and leaving prev_player_overlaps/prev_solid_collisions
 * under-sized until the next full reload. */
[[nodiscard]] bool setup_current_level_runtime(Diag *diag, GameState *state);

/* Resolve an entity's blueprint defaults via the entity→blueprint map.
 * Returns nullptr if entity has no blueprint mapping or blueprint not found. */
const AttrSet *entity_resolve_defaults(const GameState *state, int entity_id);

/* Resolve an entity's full Blueprint (not just its attrs) via the same
 * entity→blueprint map entity_resolve_defaults uses -- needed wherever more
 * than attrs are required, e.g. the animation state machine's per-entity
 * clip lookup (advance_entity_animation, game.c) and the renderer's "does
 * this entity animate" check (main.c). entity_resolve_defaults is a thin
 * wrapper over this that returns just &blueprint->attrs. Returns nullptr on
 * the same conditions entity_resolve_defaults does. */
const Blueprint *entity_resolve_blueprint(const GameState *state, int entity_id);

/* Linear-scan texture lookup against state->assets.textures.
 * Matches the TextureLookupFn callback signature so it can be passed
 * directly to level_spawn_entity, gamedata loaders, the editor's
 * fuzzy finder, etc. user_data is the GameState pointer. */
Texture2D *texture_registry_lookup(const char *filename, void *user_data);

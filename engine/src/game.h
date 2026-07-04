#pragma once

#include "arena.h"
#include "audio.h"
#include "diag.h"
#include "effect.h"
#include "gamedata.h"
#include "input.h"
#include "input_func.h"
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
#define WALK_FRAMES 6
#define ANIM_SPEED 10.0F

enum {
    ANIM_IDLE_DOWN = 0,
    ANIM_IDLE_UP = 2,
    ANIM_WALK_DOWN = 3,
    ANIM_WALK_SIDE = 4,
    ANIM_WALK_UP = 5,
};

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
} AssetRegistry;

typedef struct {
    GamedataState gamedata;
    Arena gamedata_arena;
    ArenaCheckpoint gamedata_base; /* offset just above persistent assets (textures, fonts) */
    Arena scratch_arena;
    /* Process-lifetime play progression (flags, global vars). Backed by
     * its own arena so it survives game_load_gamedata's
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
     * camera_shake, spawn, dialogue). Backed by progression_arena -- see
     * effect.h for the full lifecycle and string-lifetime rules. Not part
     * of GamedataState: not undo-snapshotted, and it must survive the
     * arena_restore(gamedata_base) that a level transition or hot-reload
     * runs against gamedata_arena. */
    EffectQueue effects;
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

/* Discard all play progression (flags, global vars): resets
 * progression_arena and zeroes state->progression. This is a
 * new-game-style reset, not a reload — callers that also want fresh
 * gamedata must separately call game_load_gamedata. Used by the
 * pause-menu RESTORE action. */
void game_reset_progression(GameState *state);

/* Snap camera to the clamped player position (no lerp). Call after level load or transition. */
void game_snap_camera(GameState *state);

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

/* Linear-scan texture lookup against state->assets.textures.
 * Matches the TextureLookupFn callback signature so it can be passed
 * directly to level_spawn_entity, gamedata loaders, the editor's
 * fuzzy finder, etc. user_data is the GameState pointer. */
Texture2D *texture_registry_lookup(const char *filename, void *user_data);

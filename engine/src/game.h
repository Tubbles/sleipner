#pragma once

#include "arena.h"
#include "audio.h"
#include "diag.h"
#include "gamedata.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"
#include "rect.h"

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

typedef struct {
    vec_texture_entry textures;
    vec_font_preview font_previews;
    Font ui_font;
} AssetRegistry;

typedef struct {
    GamedataState gamedata;
    Arena gamedata_arena;
    ArenaCheckpoint gamedata_base; /* offset just above persistent assets (textures, fonts) */
    Arena scratch_arena;
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
    AssetRegistry assets;
    BindingStore bindings;
    long gamedata_mtime;
    int screen_width;
    int screen_height;
    TransitionRequest transition;
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

/* Snap camera to the clamped player position (no lerp). Call after level load or transition. */
void game_snap_camera(GameState *state);

/* Resolve an entity's blueprint defaults via the entity→blueprint map.
 * Returns nullptr if entity has no blueprint mapping or blueprint not found. */
const AttrSet *entity_resolve_defaults(const GameState *state, int entity_id);

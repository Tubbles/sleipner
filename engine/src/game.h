#ifndef GAME_H
#define GAME_H

#include "arena.h"
#include "blueprint.h"
#include "input.h"
#include "level.h"
#include "rect.h"
#include "rule.h"

#include <stdbool.h>

struct EngineContext;

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
    int player_index;
    Arena gamedata_arena;
    Arena scratch_arena;
    BlueprintTable blueprints;
    map_entity_ruleset rule_table;
    Level current_level;
    FlagSet flags;
    AttrSet vars;
    RectU32 game_bounds;
    int frame;
    float elapsed;
    bool gamedata_loaded;
    bool debug_enabled;
    bool prev_interact;
} GameState;

typedef struct {
    const char *toml_string;
    const char *level_name;
    TextureLookupFn texture_lookup;
    void *texture_user_data;
} GamedataParams;

[[nodiscard]] bool game_init(struct EngineContext *ctx, GameState *state, RectU32 game_bounds);
[[nodiscard]] bool game_load_gamedata(struct EngineContext *ctx, GameState *state, GamedataParams params);
void game_update(struct EngineContext *ctx, GameState *state, InputState input, float delta_time);
Entity *game_get_player(GameState *state);
const Entity *game_get_player_const(const GameState *state);
void game_free(struct EngineContext *ctx, GameState *state);

#endif

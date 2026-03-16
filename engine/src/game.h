#ifndef GAME_H
#define GAME_H

#include "arena.h"
#include "blueprint.h"
#include "input.h"
#include "level.h"
#include "rect.h"

#include <stdbool.h>

#define PLAYER_SPEED 80.0F
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
    Vector2 position;
    int anim_row;
    bool flip;
    float frame_timer;
    int frame_index;
    bool moving;
} Player;

typedef struct {
    Player player;
    Arena gamedata_arena;
    BlueprintTable blueprints;
    Level current_level;
    RectU32 game_bounds;
    int frame;
    float elapsed;
    bool gamedata_loaded;
    bool debug_enabled;
} GameState;

void game_init(GameState *state, RectU32 game_bounds);
bool game_load_gamedata(GameState *state,
                        const char *toml_string,
                        const char *level_name,
                        TextureLookupFn texture_lookup,
                        void *texture_user_data);
void game_update(GameState *state, InputState input, float delta_time);
Rectangle game_player_hitbox(const Player *player);
void game_free(GameState *state);

#endif

#pragma once

#include "atlas.h"
#include "blueprint.h"
#include "level.h"
#include "raylib.h"
#include "rule.h"
#include "tileset.h"
#include "vec.h"

/* Fields snapshotted by the undo system — all arena-backed, all reset by game_load_gamedata. */
typedef struct {
    int player_index;
    vec_bool prev_player_overlaps;  /* one entry per level entity: true if player overlapped last frame */
    vec_bool prev_solid_collisions; /* entity_count² entries: true if pair [a*count+b] overlapped last frame */
    BlueprintTable blueprints;
    map_entity_ruleset rule_table;
    map_int_str entity_blueprints;
    vec_subroutine subroutines;
    vec_timer timers;
    /* Suspended `wait:`/(S6.7c) `dialogue:` rule executions (S6.7b, D24).
     * Lives here (not GameState) so it rides gamedata_arena and is wiped
     * by the same arena_restore(gamedata_base) that clears rule_table on
     * every hot-reload/level-load/level-switch -- a continuation must
     * never survive past one of those, since entity ids and node pools
     * are only stable within a single load. game_load_gamedata and
     * setup_current_level_runtime (game.c) both explicitly reset this
     * vec, matching how they already reset rule_table/entity_blueprints:
     * the arena rewind alone does not zero this struct's own count/data
     * fields, only the bytes it used to point at. */
    vec_rule_continuation continuations;
    /* Tile id -> texture+src map shared by every level's tiles_ground/
     * tiles_overlay layers (D36). Indexed by tile id — see tileset.h. */
    vec_tileset_entry tileset;
    /* Named texture regions referenced by blueprint `sprite = "name"` (D37).
     * Lookup is by name via atlas_find_region — see atlas.h. */
    vec_atlas_region atlas_regions;
    Level current_level;
    vec_level other_levels;
    Vector2 camera_target;
} GamedataState;

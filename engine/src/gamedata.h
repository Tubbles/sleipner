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

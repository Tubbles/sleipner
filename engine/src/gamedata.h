#pragma once

#include "attribute.h"
#include "blueprint.h"
#include "level.h"
#include "raylib.h"
#include "rule.h"
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
    Level current_level;
    vec_level other_levels;
    FlagSet flags;
    AttrSet vars;
    Vector2 camera_target;
} GamedataState;

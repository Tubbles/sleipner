#pragma once

#include "attribute.h"
#include "rule.h"

/* Process-lifetime play progression: flags and global vars set by
 * rules. Lives in its own progression_arena (see GameState), which is
 * never rewound by game_load_gamedata's arena_restore(gamedata_base) —
 * unlike GamedataState, which is rewound on every level transition and
 * hot-reload. This is what lets progression survive both (fixes F17)
 * while still being excluded from undo snapshots (which only cover
 * GamedataState + gamedata_arena) and from editor undo/redo. The
 * pause-menu RESTORE action explicitly clears it via
 * game_reset_progression, since RESTORE is a deliberate "discard my
 * changes" reset, not a reload. Items/inventory are deferred to S6.8
 * and do not belong here yet. */
typedef struct {
    FlagSet flags;
    AttrSet vars;
} ProgressionState;

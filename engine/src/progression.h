#pragma once

#include "attribute.h"
#include "rule.h"

/* Process-lifetime play progression: flags, global vars, and inventory
 * items set by rules. Lives in its own progression_arena (see
 * GameState), which is never rewound by game_load_gamedata's
 * arena_restore(gamedata_base) — unlike GamedataState, which is rewound
 * on every level transition and hot-reload. This is what lets
 * progression survive both (fixes F17) while still being excluded from
 * undo snapshots (which only cover GamedataState + gamedata_arena) and
 * from editor undo/redo. The pause-menu RESTORE action explicitly
 * clears it via game_reset_progression, since RESTORE is a deliberate
 * "discard my changes" reset, not a reload.
 *
 * `items` (S6.8a, D25) rides the same arena for the same reason: its
 * map keys are copied into progression_alloc on first give (see
 * ItemSet's doc comment, rule.h) precisely so they survive the
 * gamedata_arena rewind a transition/hot-reload triggers -- a bare view
 * into the ActionNode argument that named the item would dangle the
 * moment gamedata_arena rewinds. Equipment/categories and the
 * pause-menu inventory grid UI are still deferred to later steps. */
typedef struct {
    FlagSet flags;
    AttrSet vars;
    ItemSet items;
} ProgressionState;

#pragma once

#include "attribute.h"
#include "diag.h"
#include "level.h"
#include "map.h" // IWYU pragma: export
#include "rule.h"
#include "strv.h"
#include "vec.h" // IWYU pragma: export

#include "raylib.h"

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
 * pause-menu inventory grid UI are still deferred to later steps.
 *
 * `level_deltas` (S6.15b, D33) is the in-memory half of D33's deferred
 * persistence: gameplay changes to a level's AUTHORED entities (moved
 * position, instance attr overrides, active/inactive) captured on
 * LEAVING the level and re-applied on RETURNING, so e.g. an opened
 * chest stays open across a transition round trip. Serialization to a
 * save file is S6.15c; this slice is process-lifetime only, same as
 * flags/vars/items. */
typedef struct {
    int id;
    Vector2 position;
    /* The entity's instance attrs at capture time (attr_set_copy'd, never
     * a borrowed view into the gamedata_arena entity being torn down) --
     * per the two-level scoping design (DESIGN.md § "Entity–blueprint
     * connection"), an entity's instance AttrSet already IS its delta
     * against blueprint defaults, so this one field covers every
     * attr-shaped gameplay change (opened chests, quest flags on an
     * entity, etc). */
    AttrSet attrs;
    /* Redundant with an explicit "active" entry inside attrs above when
     * one was ever written (attr_set_bool(..., "active", ...) is exactly
     * how the engine soft-destroys an entity today, see game.c/rule.c) --
     * kept as its own field anyway so the delta's active/inactive
     * marker has a stable, attr-encoding-independent home once S6.15c
     * gives it a save-file schema, rather than requiring a save reader
     * to know "active" is presently just an attr. Captured/restored
     * unscoped (no blueprint defaults dependency, keeping this struct's
     * producers narrow) -- safe because no blueprint in this codebase
     * authors a default `active` override today; see progression.c. */
    bool active;
} EntityDelta;

VEC_DECL(entity_delta, EntityDelta)

typedef struct {
    vec_entity_delta entities;
} LevelDelta;

/* Keyed on Strv, same gamedata-arena-dangling hazard as ItemSet's
 * map_strv_int (rule.h): the STORED key is always a progression_alloc
 * copy of the level's name, freshly made on every capture -- see
 * progression_capture_level_delta's doc comment for why a bare view into
 * the gamedata-arena-backed Level.name would dangle, and why (unlike
 * ItemSet) this doesn't bother reusing an existing key on recapture. */
MAP_DECL(strv_level_delta, Strv, LevelDelta)

typedef struct {
    FlagSet flags;
    AttrSet vars;
    ItemSet items;
    map_strv_level_delta level_deltas;
} ProgressionState;

/* Snapshot every entity in `level` (position, instance attrs, active) into
 * `progression`'s level_deltas store, keyed by level->name, OVERWRITING any
 * delta already stored for that level -- the previous copy is simply
 * orphaned in progression_alloc's arena, bounded by transition events (see
 * CLAUDE.md's arena growth rule). Call this BEFORE reloading/rewinding the
 * level being left, while its entities are still live. Entities without a
 * stable authored id (spawned via spawn:, S6.6) are captured the same as
 * any other, but level_find_entity_by_id simply won't find them again after
 * a reload -- spawned entities do not persist across transitions in this
 * v1 (see TODO.md). */
void progression_capture_level_delta(Diag *diag,
                                     ProgressionState *progression,
                                     Allocator *progression_alloc,
                                     const Level *level);

/* Re-apply `progression`'s stored delta (if any) for `level->name` onto
 * `level`'s freshly-parsed entities: for each stored EntityDelta,
 * level_find_entity_by_id locates the matching authored entity and its
 * position/attrs/active are overwritten from the delta (attrs via
 * attr_set_copy into gamedata_alloc -- entity->attrs must stay
 * gamedata_arena-backed like the rest of Entity, never progression_alloc-
 * backed). Unmatched ids (an entity the fresh parse no longer has, or a
 * previously-spawned entity, per the capture doc comment above) are
 * skipped. Call this AFTER the level has been (re)loaded. If the caller is
 * about to reposition an entity itself afterward (e.g. the player at a
 * transition's door spawn coords), that later write wins -- this function
 * does not special-case any entity id. */
void progression_apply_level_delta(Diag *diag,
                                   const ProgressionState *progression,
                                   Level *level,
                                   Allocator *gamedata_alloc);

/* Drop every stored level delta. Called on hot-reload (the gamedata on
 * disk changed, so any captured gameplay delta is stale against the new
 * authoring) -- mirrors undo_history_clear's reload-time call. NOT called
 * on a transition (that's when capture/apply run) -- new-game/pause-menu
 * RESTORE clear it implicitly via game_reset_progression's wholesale
 * ProgressionState zeroing instead, since level_deltas rides the same
 * progression_arena reset as flags/vars/items. */
void progression_clear_level_deltas(ProgressionState *progression);

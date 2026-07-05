#pragma once

#include "atlas.h"
#include "attribute.h"
#include "blueprint.h"
#include "error.h"
#include "input_func.h"
#include "level.h"
#include "map.h"
#include "progression.h"
#include "rule.h"
#include "strv.h"
#include "tileset.h"

/* Emit blueprint, subroutine, tileset, atlas region, and level data as TOML
 * into a buffer. Returns the number of bytes written (excluding null
 * terminator), or -1 if the buffer is too small. */
[[nodiscard]] int toml_emit_gamedata(ErrorState *err,
                                     char *buffer,
                                     int capacity,
                                     const BlueprintTable *blueprints,
                                     const vec_subroutine *subroutines,
                                     const vec_tileset_entry *tileset,
                                     const vec_atlas_region *atlas_regions,
                                     const Level *levels,
                                     int level_count);

/* Emit a BindingStore as keybindings.toml content. The schema mirrors
 * input_func_load_bindings_toml: top-level [function.NAME] tables, each
 * with a `bindings` array of `{ parts = [{...}] }` entries. Only
 * actions/axes that have at least one alternative are emitted. Returns
 * bytes written (excluding null terminator), or -1 if the buffer is
 * too small. */
[[nodiscard]] int toml_emit_bindings(ErrorState *err, char *buffer, int capacity, const BindingStore *store);

/* Emit a save-state bundle as TOML (S6.15c, D33): a `[save]` header
 * (`version`, `current_level`, and a `flags` array of set flag names),
 * `[save.vars]`/`[save.items]` tables (reusing the same typed-value
 * encoding `emit_attr_value` already uses for blueprint/entity attrs),
 * and one `[[level_delta]]` array-of-tables entry per captured level,
 * each carrying a `[[level_delta.entity]]` sub-array (`id`, `pos`,
 * `active`, and the entity's own attrs). Takes the raw ProgressionState
 * pieces rather than a bundled SaveState type, mirroring how
 * toml_emit_gamedata takes raw blueprint/level pieces instead of a
 * "Gamedata" struct -- SaveState itself is save.h's concern, not this
 * generic emitter's. Returns bytes written (excluding the null
 * terminator), or -1 if the buffer is too small. */
[[nodiscard]] int toml_emit_save(ErrorState *err,
                                 char *buffer,
                                 int capacity,
                                 int version,
                                 Strv current_level_name,
                                 const FlagSet *flags,
                                 const AttrSet *vars,
                                 const ItemSet *items,
                                 const map_strv_level_delta *level_deltas);

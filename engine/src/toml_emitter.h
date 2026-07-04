#pragma once

#include "atlas.h"
#include "blueprint.h"
#include "error.h"
#include "input_func.h"
#include "level.h"
#include "rule.h"
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

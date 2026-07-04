#pragma once

#include "arena.h"
#include "diag.h"
#include "str.h"
#include "vec.h"

#include "raylib.h"

/* A named region within a texture atlas: maps a name to a texture + source
 * rectangle. Blueprints reference a region via `sprite = "name"` as an
 * alternative to authoring a raw `src` (see D37). Unlike tileset.h's
 * id-indexed vec, lookup here is always by name via atlas_find_region --
 * vec order carries no meaning. */
typedef struct {
    Str name;
    Str texture;
    Rectangle src;
} AtlasRegion;

VEC_DECL(atlas_region, AtlasRegion)

/* Parse every [[atlas.region]] entry from a tomlc99 root table into `regions`.
 * Entries with a missing/invalid name, texture, or src are logged and
 * skipped, as are entries whose name duplicates one already registered
 * (first-registered wins). A missing [atlas] table or [[atlas.region]]
 * array leaves `regions` empty. Returns the number of entries found in the
 * TOML array (not the number successfully parsed). */
int atlas_load(Diag *diag, vec_atlas_region *regions, void *toml_root, Arena *arena);

/* Free every entry's owned Str name/texture, then the vec itself. */
void atlas_free(vec_atlas_region *regions);

/* Find a region by name. Returns nullptr if not found. */
const AtlasRegion *atlas_find_region(const vec_atlas_region *regions, const char *name);

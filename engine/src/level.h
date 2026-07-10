#pragma once

#include "alloc.h"
#include "blueprint.h"
#include "diag.h"
#include "entity.h"
#include "str.h"

#include <stdbool.h>

/* Callback for resolving a texture name to a Texture2D pointer.
 * The loader calls this for each entity's blueprint texture_name. */
typedef Texture2D *(*TextureLookupFn)(const char *texture_name, void *user_data);

/* Pixel size of one tile in both tile layers (D36). Single source of truth
 * for level.c's tiles_wide/tiles_high computation and main.c's tile-layer
 * screen-rect math — both must agree on this value. */
#define TILE_SIZE 16

typedef struct {
    Str name;
    Str music_name;
    Str background_tile;
    Color background_tint;
    int width;
    int height;
    int floor_width;
    int floor_height;
    int next_entity_id;
    vec_entity entities;
    /* Tile grid dimensions, ceil(width|height / TILE_SIZE) — see
     * level_tiles_wide/level_tiles_high. tiles_ground and tiles_overlay are
     * flat, row-major vec_int layers of size tiles_wide * tiles_high (index
     * via level_tile_index). Tile value 0 means empty; nonzero indexes into
     * GamedataState.tileset. Both layers are empty (count == 0) when the
     * level authors no tile data — the ground layer then falls back to the
     * flat background_tile fill, and the overlay layer simply draws
     * nothing. */
    int tiles_wide;
    int tiles_high;
    vec_int tiles_ground;
    vec_int tiles_overlay;
} Level;

VEC_DECL(level, Level)

/* Free level name, music name, tile layers, and all entity Str fields and attrs. */
void level_free(Allocator *alloc, Level *level);

/* Ceil-division tile-grid dimensions for a level of the given pixel
 * width/height — the number of TILE_SIZE columns/rows needed to cover it. */
int level_tiles_wide(int width);
int level_tiles_high(int height);

/* Row-major flat index into a tile layer vec_int (row * tiles_wide + col). */
int level_tile_index(int row, int col, int tiles_wide);

/* Find an entity by its stable id (Entity.id). Returns its current index
 * into level->entities, or -1 if no entity has that id. Ids survive
 * undo/redo restores, compaction on delete, and vec reallocation, so
 * this is the safe way to re-locate an entity across those events. */
int level_find_entity_by_id(const Level *level, int entity_id);

/* Propagate deletion marks down the parent_index tree: any entity whose
 * (transitive) ancestor is already marked in is_deleted[] gets marked too.
 * is_deleted is caller-owned, one flag per entity, seeded with the roots to
 * delete; count is the entity count the array covers. Pure walk -- mutates
 * only is_deleted, never the level. Shared by the editor's cascade deletes
 * (editor/blueprint.c, editor/child.c) and game_delete_entity_cascade
 * (game.c). */
void level_mark_deleted_descendants(const Level *level, bool *is_deleted, int count);

/* Spawn a single child entity from a BlueprintChild definition under the given parent.
 * Instantiates grandchildren recursively; assigns next_entity_id. Returns true on success. */
[[nodiscard]] bool level_spawn_single_child(Diag *diag,
                                            Level *level,
                                            int parent_index,
                                            const BlueprintChild *child_def,
                                            const BlueprintTable *blueprints,
                                            TextureLookupFn texture_lookup,
                                            void *texture_user_data,
                                            Allocator *alloc);

/* Spawn a new root entity from a blueprint at the given position.
 * Instantiates children; assigns next_entity_id. Returns true on success. */
[[nodiscard]] bool level_spawn_entity(Diag *diag,
                                      Level *level,
                                      const Blueprint *blueprint,
                                      Vector2 position,
                                      const BlueprintTable *blueprints,
                                      TextureLookupFn texture_lookup,
                                      void *texture_user_data,
                                      Allocator *alloc);

/* Parse the first [[level]] (or the one matching `level_name` if non-nullptr)
 * from a tomlc99 root table. Instantiates entities from blueprints.
 * Returns true on success. */
[[nodiscard]] bool level_load(Diag *diag,
                              Level *level,
                              void *toml_root,
                              const char *level_name,
                              const BlueprintTable *blueprints,
                              TextureLookupFn texture_lookup,
                              void *texture_user_data,
                              Allocator *alloc);

/* Parse all [[level]] sections EXCEPT the one named `exclude_name` into a vec.
 * Used to preserve non-current levels during editor save. */
[[nodiscard]] bool level_load_others(Diag *diag,
                                     vec_level *others,
                                     void *toml_root,
                                     const char *exclude_name,
                                     const BlueprintTable *blueprints,
                                     TextureLookupFn texture_lookup,
                                     void *texture_user_data,
                                     Allocator *alloc);

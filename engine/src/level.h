#ifndef LEVEL_H
#define LEVEL_H

#include "alloc.h"
#include "blueprint.h"
#include "diag.h"
#include "entity.h"
#include "str.h"

#include <stdbool.h>

/* Callback for resolving a texture name to a Texture2D pointer.
 * The loader calls this for each entity's blueprint texture_name. */
typedef Texture2D *(*TextureLookupFn)(const char *texture_name, void *user_data);

typedef struct {
    Str name;
    Str music_name;
    int width;
    int height;
    int next_entity_id;
    vec_entity entities;
} Level;

/* Free level name, music name, and all entity Str fields and attrs. */
void level_free(Allocator *alloc, Level *level);

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

#endif

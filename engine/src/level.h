#ifndef LEVEL_H
#define LEVEL_H

#include "alloc.h"
#include "blueprint.h"
#include "entity.h"
#include "str.h"

#include <stdbool.h>

struct EngineContext;

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

/* Parse the first [[level]] (or the one matching `level_name` if non-NULL)
 * from a tomlc99 root table. Instantiates entities from blueprints.
 * Returns true on success. */
[[nodiscard]] bool level_load(struct EngineContext *ctx,
                              Level *level,
                              void *toml_root,
                              const char *level_name,
                              const BlueprintTable *blueprints,
                              TextureLookupFn texture_lookup,
                              void *texture_user_data,
                              Allocator *alloc);

#endif

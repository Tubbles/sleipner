#ifndef LEVEL_H
#define LEVEL_H

#include "entity.h"

#include <stdbool.h>

#define MAX_LEVEL_NAME 64
#define MAX_MUSIC_NAME 64
#define MAX_LEVEL_ENTITIES 512

/* Callback for resolving a texture name to a Texture2D pointer.
 * The loader calls this for each entity's blueprint texture_name. */
typedef Texture2D *(*TextureLookupFn)(const char *texture_name, void *user_data);

typedef struct {
    char name[MAX_LEVEL_NAME];
    char music_name[MAX_MUSIC_NAME];
    int width;
    int height;
    Entity entities[MAX_LEVEL_ENTITIES];
    int entity_count;
} Level;

/* Parse the first [[level]] (or the one matching `level_name` if non-NULL)
 * from a tomlc99 root table. Instantiates entities from blueprints.
 * Returns true on success. */
[[nodiscard]] bool level_load(Level *level,
                              void *toml_root,
                              const char *level_name,
                              const BlueprintTable *blueprints,
                              TextureLookupFn texture_lookup,
                              void *texture_user_data);

#endif

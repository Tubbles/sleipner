#ifndef BLUEPRINT_H
#define BLUEPRINT_H

#include "arena.h"
#include "attribute.h"

#include "raylib.h"

#include <stdbool.h>

#define MAX_BLUEPRINTS 256
#define MAX_BLUEPRINT_NAME 64
#define MAX_TEXTURE_NAME 64

typedef struct {
    char name[MAX_BLUEPRINT_NAME];
    char extends_name[MAX_BLUEPRINT_NAME];
    char texture_name[MAX_TEXTURE_NAME];
    Rectangle source;
    Vector2 collision_offset;
    Vector2 collision_size;
    AttrSet attrs;
} Blueprint;

typedef struct {
    Blueprint entries[MAX_BLUEPRINTS];
    int count;
} BlueprintTable;

/* Parse all [[blueprint]] entries from a tomlc99 root table into the blueprint table.
 * Arena is used for any variable-length data (currently unused, reserved for future
 * attribute/rule storage). Returns the number of blueprints loaded, or -1 on error. */
int blueprints_load(BlueprintTable *table, void *toml_root, Arena *arena);

/* Find a blueprint by name. Returns NULL if not found. */
const Blueprint *blueprint_find(const BlueprintTable *table, const char *name);

#endif

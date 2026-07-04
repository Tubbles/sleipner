#pragma once

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "collision.h"
#include "diag.h"
#include "rule.h"
#include "str.h"
#include "vec.h"

#include "raylib.h"

#include <stdbool.h>

typedef struct {
    Str blueprint_name;
    Str tag;
    Vector2 offset;
} BlueprintChild;

VEC_DECL(blueprint_child, BlueprintChild)

typedef struct {
    AttrSet attrs;
    vec_blueprint_child children;
    vec_rule rules;

    /* Authored composite collision shape from [[blueprint.collision]].
     * Empty (prims.count == 0) means "no composite" — instantiated entities
     * then fall back to the one-rect shape derived from the
     * collision_offset/collision_w/collision_h attrs (see
     * entity_collision_region). */
    CollisionShape collision;
} Blueprint;

VEC_DECL(blueprint, Blueprint)

typedef struct BlueprintTable BlueprintTable;
struct BlueprintTable {
    vec_blueprint entries;
};

/* Geometry helpers — read src/collision attrs and assemble into raylib types. */
Rectangle blueprint_get_source(const Blueprint *blp);
Vector2 blueprint_get_collision_offset(const Blueprint *blp);
Vector2 blueprint_get_collision_size(const Blueprint *blp);
Vector2 blueprint_get_sprite_offset(const Blueprint *blp);

/* Parse all [[blueprint]] entries from a tomlc99 root table into the blueprint table.
 * Arena is used for variable-length data (rule arrays).
 * Returns the number of blueprints loaded, or -1 on error. */
int blueprints_load(Diag *diag, BlueprintTable *table, void *toml_root, Arena *arena);

/* Free all blueprints and their children/attrs from a table, and the table's vec. */
void blueprint_table_free(Allocator *alloc, BlueprintTable *table);

/* Find a blueprint by name. Returns nullptr if not found. */
const Blueprint *blueprint_find(const BlueprintTable *table, const char *name);

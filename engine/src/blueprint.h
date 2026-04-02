#ifndef BLUEPRINT_H
#define BLUEPRINT_H

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "rule.h"
#include "str.h"
#include "vec.h"

#include "raylib.h"

#include <stdbool.h>

// cppcheck-suppress noForwardDecl-noForwardDecl
struct EngineContext;

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

/* Parse all [[blueprint]] entries from a tomlc99 root table into the blueprint table.
 * Arena is used for variable-length data (rule arrays).
 * Returns the number of blueprints loaded, or -1 on error. */
int blueprints_load(struct EngineContext *ctx, BlueprintTable *table, void *toml_root, Arena *arena);

/* Free all blueprints and their children/attrs from a table, and the table's vec. */
void blueprint_table_free(Allocator *alloc, BlueprintTable *table);

/* Find a blueprint by name. Returns nullptr if not found. */
const Blueprint *blueprint_find(const BlueprintTable *table, const char *name);

#endif

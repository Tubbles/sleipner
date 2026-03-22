#ifndef BLUEPRINT_H
#define BLUEPRINT_H

#include "arena.h"
#include "attribute.h"
#include "str.h"
#include "vec.h"

#include "raylib.h"

#include <stdbool.h>

struct EngineContext;

typedef struct {
    struct Rule *entries;
    int count;
} RuleSet;

typedef struct {
    Str blueprint_name;
    Str tag;
    Vector2 offset;
} BlueprintChild;

VEC_DECL(blueprint_child, BlueprintChild)

typedef struct {
    Str name;
    Str extends_name;
    Str texture_name;
    Rectangle source;
    Vector2 collision_offset;
    Vector2 collision_size;
    AttrSet attrs;
    vec_blueprint_child children;
    RuleSet rules;
} Blueprint;

VEC_DECL(blueprint, Blueprint)

typedef struct {
    vec_blueprint entries;
} BlueprintTable;

/* Parse all [[blueprint]] entries from a tomlc99 root table into the blueprint table.
 * Arena is used for variable-length data (rule arrays).
 * Returns the number of blueprints loaded, or -1 on error. */
int blueprints_load(struct EngineContext *ctx, BlueprintTable *table, void *toml_root, Arena *arena);

/* Free all blueprints and their children/attrs from a table, and the table's vec. */
void blueprint_table_free(struct EngineContext *ctx, BlueprintTable *table);

/* Find a blueprint by name. Returns NULL if not found. */
const Blueprint *blueprint_find(const BlueprintTable *table, const char *name);

#endif

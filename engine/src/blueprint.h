#pragma once

#include "alloc.h"
#include "arena.h"
#include "atlas.h"
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

/* One row of a blueprint's [[blueprint.animation]] table (S6.11a, D31):
 * `state` names which built-in `state` attr value this clip plays for
 * (e.g. "walk", "idle"); `row`/`frames` locate it in the sprite sheet
 * (row * FRAME_SIZE, frames of FRAME_SIZE each, see game.h); `speed` is
 * frames per second (0 or frames <= 1 holds frame 0, e.g. an idle clip). */
typedef struct {
    Str state;
    int row;
    int frames;
    float speed;
} AnimClip;

VEC_DECL(anim_clip, AnimClip)

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

    /* Animation clips authored via [[blueprint.animation]] (S6.11a, D31):
     * a state name (matched against an entity's own `state` attr) plus the
     * sprite-sheet row/frame-count/playback speed to show while in that
     * state. Looked up live by the entity's blueprint every frame
     * (advance_entity_animation, game.c) rather than deep-copied onto each
     * instantiated entity -- unlike collision/hitbox/hurtbox composites,
     * clips are read-only reference data with no per-instance mutation, so
     * a live lookup (the same pattern entity_resolve_defaults already uses
     * for attrs) is simpler and skips an arena copy on every spawn. Empty
     * (count == 0) means the blueprint has no animation state machine at
     * all -- entities render through the static get_source_rect path
     * instead (main.c). */
    vec_anim_clip animation;
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

/* Resolve every blueprint's optional `sprite = "region_name"` attr against
 * atlas_regions (already parsed via atlas_load — call this AFTER
 * blueprints_load, since it mutates already-parsed blueprints in place).
 * A match overwrites the blueprint's src_x/src_y/src_w/src_h and texture
 * attrs with the region's src/texture — sprite wins over any raw src/texture
 * the blueprint also declares. An unresolved name is logged via `diag` and
 * the blueprint's own src/texture (or defaults) are left untouched. */
void blueprint_resolve_sprites(Diag *diag,
                               Allocator *alloc,
                               BlueprintTable *table,
                               const vec_atlas_region *atlas_regions);

/* Free all blueprints and their children/attrs from a table, and the table's vec. */
void blueprint_table_free(Allocator *alloc, BlueprintTable *table);

/* Find a blueprint by name. Returns nullptr if not found. */
const Blueprint *blueprint_find(const BlueprintTable *table, const char *name);

/* Find an animation clip by its `state` name within one blueprint's
 * [[blueprint.animation]] table (S6.11a, D31). Returns nullptr if the
 * blueprint has no clip under that name (including having no clips at
 * all). */
const AnimClip *blueprint_find_anim_clip(const Blueprint *blueprint, const char *state);

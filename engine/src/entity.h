#pragma once

#include "alloc.h"
#include "attribute.h"
#include "str.h"
#include "vec.h" // IWYU pragma: export

#include "raylib.h"

#include <stdbool.h>

/* Blueprint properties needed to initialize an entity. Caller owns all
 * pointed-to data for the duration of entity_init — nothing is borrowed
 * beyond that call. */
typedef struct {
    Strv blueprint_name;
    Texture2D *texture;
} EntitySpec;

typedef struct {
    Texture2D *texture;

    AttrSet persisted_attrs;
    AttrSet attrs;
    Str blueprint_name;
    Str tag;

    /* Scalar fields (4 bytes each, packed together) */
    int id;
    int anim_row;
    float frame_timer;
    int frame_index;
    int parent_index;

    /* Vectors (8 bytes each) */
    Vector2 position;
    Vector2 offset;

    /* Bools (1 byte each, packed at end) */
    bool flip;
    bool moving;
} Entity;

/* Initialize an entity from a spec. Copies blueprint_name.
 * Returns false on allocation failure. */
[[nodiscard]] bool entity_init(Entity *entity, EntitySpec spec, Vector2 position, Allocator *alloc);

/* Compute the collision rectangle for an entity. Uses scoped attr lookup so
 * blueprint edits to collision_offset/size are reflected immediately. */
Rectangle entity_collision_rect(const Entity *entity, const AttrSet *defaults);

/* Find an entity by tag within the same composition tree as source.
 * Handles implicit tags: "self", "parent", "root".
 * Returns nullptr if not found. */
const Entity *entity_find_by_tag(const Entity *source, const char *tag, const Entity *entities, int entity_count);

/* Mutable version of entity_find_by_tag. */
Entity *entity_find_by_tag_mut(Entity *source, const char *tag, Entity *entities, int entity_count);

/* Effective visibility: own visible AND all ancestors visible. */
bool entity_is_visible(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

/* Effective active state: own active AND all ancestors active. */
bool entity_is_active(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

/* Compute the draw position for an entity. Uses scoped attr lookup so
 * blueprint edits to sprite_offset are reflected immediately. */
Vector2 entity_draw_position(const Entity *entity, const AttrSet *defaults);

VEC_DECL(entity, Entity)

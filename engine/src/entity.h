#ifndef ENTITY_H
#define ENTITY_H

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
    Vector2 collision_offset;
    Vector2 collision_size;
    Texture2D *texture;
} EntitySpec;

typedef struct {
    Texture2D *texture;

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
    Vector2 collision_offset;
    Vector2 collision_size;
    Vector2 offset;

    /* Rectangles (16 bytes each) */
    Rectangle collision;

    /* Bools (1 byte each, packed at end) */
    bool flip;
    bool moving;
} Entity;

/* Initialize an entity from a spec. Copies blueprint_name.
 * Returns false on allocation failure. */
[[nodiscard]] bool entity_init(Entity *entity, EntitySpec spec, Vector2 position, Allocator *alloc);

/* Recompute collision rect from position + entity's stored collision_offset.
 * Call after moving an entity. */
void entity_update_collision(Entity *entity);

/* Find an entity by tag within the same composition tree as source.
 * Handles implicit tags: "self", "parent", "root".
 * Returns NULL if not found. */
const Entity *entity_find_by_tag(const Entity *source, const char *tag, const Entity *entities, int entity_count);

/* Mutable version of entity_find_by_tag. */
Entity *entity_find_by_tag_mut(Entity *source, const char *tag, Entity *entities, int entity_count);

/* Effective visibility: own visible AND all ancestors visible. */
bool entity_is_visible(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

/* Effective active state: own active AND all ancestors active. */
bool entity_is_active(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

VEC_DECL(entity, Entity)

#endif

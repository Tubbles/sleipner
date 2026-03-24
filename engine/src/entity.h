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
    const AttrSet *defaults;
    Rectangle source;
    Vector2 collision_offset;
    Vector2 collision_size;
    Texture2D *texture;
} EntitySpec;

typedef struct {
    /* Pointers (8 bytes each, no padding gap) */
    const AttrSet *defaults;
    Texture2D *texture;

    /* Attrs + identity strings (all naturally aligned) */
    AttrSet attrs;
    Str blueprint_name;
    Str tag;

    /* Scalar fields (4 bytes each, packed together) */
    int id;
    int health;
    int max_health;
    float opacity;
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
    Rectangle source;
    Rectangle collision;

    /* Bools (1 byte each, packed at end) */
    bool visible;
    bool active;
    bool solid;
    bool flip;
    bool moving;
} Entity;

/* Get an attribute with instance -> defaults fallback.
 * Returns NULL if not found in either. */
const Attribute *entity_get_attr(const Entity *entity, const char *name);

/* Typed getters with instance -> defaults fallback. */
float entity_get_float(const Entity *entity, const char *name, float fallback);
int entity_get_int(const Entity *entity, const char *name, int fallback);
bool entity_get_bool(const Entity *entity, const char *name, bool fallback);
const char *entity_get_string(const Entity *entity, const char *name);

/* Initialize an entity from a spec. Copies blueprint_name; borrows defaults
 * pointer. Returns false on allocation failure. */
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
bool entity_is_visible(int entity_index, const Entity *entities);

/* Effective active state: own active AND all ancestors active. */
bool entity_is_active(int entity_index, const Entity *entities);

VEC_DECL(entity, Entity)

#endif

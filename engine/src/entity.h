#ifndef ENTITY_H
#define ENTITY_H

#include "alloc.h"
#include "attribute.h"
#include "blueprint.h"
#include "str.h"
#include "vec.h" // IWYU pragma: export

#include "raylib.h"

#include <stdbool.h>

typedef struct {
    /* Identity */
    Str blueprint_name;
    const Blueprint *blueprint;
    Str tag;

    /* Built-in attributes */
    Vector2 position;
    int health;
    int max_health;
    bool visible;
    bool active;
    bool solid;
    float opacity;

    /* Rendering */
    Texture2D *texture;
    Rectangle source;
    Rectangle collision;

    /* Animation (runtime state, not persisted) */
    int anim_row;
    bool flip;
    float frame_timer;
    int frame_index;
    bool moving;

    /* Custom attributes (instance overrides) */
    AttrSet attrs;

    /* Composition */
    int parent_index;
    Vector2 offset;
} Entity;

/* Get an attribute with instance -> blueprint fallback.
 * Returns NULL if not found in either. */
const Attribute *entity_get_attr(const Entity *entity, const char *name);

/* Typed getters with instance -> blueprint fallback. */
float entity_get_float(const Entity *entity, const char *name, float fallback);
int entity_get_int(const Entity *entity, const char *name, int fallback);
bool entity_get_bool(const Entity *entity, const char *name, bool fallback);
const char *entity_get_string(const Entity *entity, const char *name);

/* Initialize an entity from a blueprint. Sets built-in attributes
 * from blueprint defaults and copies rendering fields.
 * Returns false on allocation failure. */
[[nodiscard]] bool entity_init_from_blueprint(
    Entity *entity, const Blueprint *blueprint, Vector2 position, Texture2D *texture, Allocator *alloc);

/* Recompute collision rect from position + blueprint offsets.
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

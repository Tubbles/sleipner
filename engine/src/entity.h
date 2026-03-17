#ifndef ENTITY_H
#define ENTITY_H

#include "attribute.h"
#include "blueprint.h"

#include "raylib.h"

#include <stdbool.h>

#define MAX_ENTITIES 512
#define MAX_TAG 32

typedef struct {
    /* Identity */
    char blueprint_name[MAX_BLUEPRINT_NAME];
    const Blueprint *blueprint;
    char tag[MAX_TAG];

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

    /* Custom attributes (instance overrides) */
    AttrSet attrs;

    /* Composition */
    int parent_index;
} Entity;

/* Get an attribute with instance -> blueprint fallback.
 * Returns NULL if not found in either. */
const Attribute *entity_get_attr(const Entity *entity, const char *name);

/* Typed getters with instance -> blueprint fallback. */
float entity_get_float(const Entity *entity, const char *name, float fallback);
int entity_get_int(const Entity *entity, const char *name, int fallback);
bool entity_get_bool(const Entity *entity, const char *name, bool fallback);
const char *entity_get_string(const Entity *entity, const char *name, const char *fallback);

/* Initialize an entity from a blueprint. Sets built-in attributes
 * from blueprint defaults and copies rendering fields. */
void entity_init_from_blueprint(Entity *entity, const Blueprint *blueprint, Vector2 position, Texture2D *texture);

#endif

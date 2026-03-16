#ifndef ENTITY_H
#define ENTITY_H

#include "collision.h"
#include "input.h"
#include <stdbool.h>

typedef struct Entity Entity;

typedef struct {
    void (*update)(Entity *e, InputState input, float dt);
    void (*render)(const Entity *e);
    void (*get_collision_shape)(const Entity *e, CollisionShape *out);
} EntityVTable;

struct Entity {
    int kind;
    const EntityVTable *vtable;
    bool active;
    Vector2 position;
    float rotation; /* degrees */
    float scale;
    Color color;
    int gamepad_id;
};

#define MAX_ENTITIES 64

#endif

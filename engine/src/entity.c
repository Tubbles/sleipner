#include "entity.h"
#include "attribute.h"
#include "blueprint.h"

#include "raylib.h"

#include <string.h>

const Attribute *entity_get_attr(const Entity *entity, const char *name)
{
    if (entity->blueprint) {
        return attr_get_scoped(&entity->attrs, &entity->blueprint->attrs, name);
    }
    return attr_get(&entity->attrs, name);
}

float entity_get_float(const Entity *entity, const char *name, float fallback)
{
    const Attribute *entry = entity_get_attr(entity, name);
    if (!entry) {
        return fallback;
    }
    if (entry->type == ATTR_FLOAT) {
        return entry->value.f;
    }
    if (entry->type == ATTR_INT) {
        return (float)entry->value.i;
    }
    return fallback;
}

int entity_get_int(const Entity *entity, const char *name, int fallback)
{
    const Attribute *entry = entity_get_attr(entity, name);
    if (!entry) {
        return fallback;
    }
    if (entry->type == ATTR_INT) {
        return entry->value.i;
    }
    if (entry->type == ATTR_FLOAT) {
        return (int)entry->value.f;
    }
    return fallback;
}

bool entity_get_bool(const Entity *entity, const char *name, bool fallback)
{
    const Attribute *entry = entity_get_attr(entity, name);
    if (entry && entry->type == ATTR_BOOL) {
        return entry->value.b;
    }
    return fallback;
}

const char *entity_get_string(const Entity *entity, const char *name, const char *fallback)
{
    const Attribute *entry = entity_get_attr(entity, name);
    if (entry && entry->type == ATTR_STRING) {
        return entry->value.s;
    }
    return fallback;
}

void entity_init_from_blueprint(Entity *entity, const Blueprint *blueprint, Vector2 position, Texture2D *texture)
{
    memset(entity, 0, sizeof(*entity));

    strncpy(entity->blueprint_name, blueprint->name, MAX_BLUEPRINT_NAME - 1);
    entity->blueprint = blueprint;

    entity->position = position;
    entity->texture = texture;
    entity->source = blueprint->source;
    entity->collision = (Rectangle){
        position.x + blueprint->collision_offset.x,
        position.y + blueprint->collision_offset.y,
        blueprint->collision_size.x,
        blueprint->collision_size.y,
    };

    /* Built-in defaults */
    entity->health = attr_get_int(&blueprint->attrs, "health", 0);
    entity->max_health = attr_get_int(&blueprint->attrs, "max_health", 0);
    entity->visible = true;
    entity->active = true;
    entity->solid = (bool)((blueprint->collision_size.x > 0.0F) || (blueprint->collision_size.y > 0.0F));
    entity->opacity = 1.0F;
    entity->parent_index = -1;
}

void entity_update_collision(Entity *entity)
{
    if (entity->blueprint) {
        entity->collision.x = entity->position.x + entity->blueprint->collision_offset.x;
        entity->collision.y = entity->position.y + entity->blueprint->collision_offset.y;
    }
}

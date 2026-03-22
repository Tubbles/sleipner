#include "entity.h"
#include "attribute.h"
#include "blueprint.h"
#include "str.h"
#include "strv.h"

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

const char *entity_get_string(const Entity *entity, const char *name)
{
    const Attribute *entry = entity_get_attr(entity, name);
    if (entry && entry->type == ATTR_STRING) {
        return entry->value.str.ptr;
    }
    return NULL;
}

bool entity_init_from_blueprint(
    struct EngineContext *ctx, Entity *entity, const Blueprint *blueprint, Vector2 position, Texture2D *texture)
{
    (void)ctx;
    memset(entity, 0, sizeof(*entity));

    if (!str_from_strv(NULL, &entity->blueprint_name, str_to_strv(blueprint->name))) {
        return false;
    }
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
    return true;
}

void entity_update_collision(Entity *entity)
{
    if (entity->blueprint) {
        entity->collision.x = entity->position.x + entity->blueprint->collision_offset.x;
        entity->collision.y = entity->position.y + entity->blueprint->collision_offset.y;
    }
}

static int find_entity_index(const Entity *entity, const Entity *entities, int entity_count)
{
    for (int index = 0; index < entity_count; index++) {
        if (&entities[index] == entity) {
            return index;
        }
    }
    return -1;
}

static int find_root_index(const Entity *entities, int entity_index)
{
    int current = entity_index;
    while (entities[current].parent_index >= 0) {
        current = entities[current].parent_index;
    }
    return current;
}

const Entity *entity_find_by_tag(const Entity *source, const char *tag, const Entity *entities, int entity_count)
{
    if (strcmp(tag, "self") == 0) {
        return source;
    }

    if (strcmp(tag, "parent") == 0) {
        if (source->parent_index >= 0) {
            return &entities[source->parent_index];
        }
        return NULL;
    }

    int source_index = find_entity_index(source, entities, entity_count);
    if (source_index < 0) {
        return NULL;
    }

    if (strcmp(tag, "root") == 0) {
        return &entities[find_root_index(entities, source_index)];
    }

    int root_index = find_root_index(entities, source_index);
    for (int index = 0; index < entity_count; index++) {
        if (find_root_index(entities, index) == root_index && entities[index].tag.len > 0 &&
            (int)strv_eq_cstr(str_to_strv(entities[index].tag), tag)) {
            return &entities[index];
        }
    }
    return NULL;
}

Entity *entity_find_by_tag_mut(Entity *source, const char *tag, Entity *entities, int entity_count)
{
    const Entity *result = entity_find_by_tag((const Entity *)source, tag, (const Entity *)entities, entity_count);
    if (!result) {
        return NULL;
    }
    return &entities[result - entities];
}

bool entity_is_visible(int entity_index, const Entity *entities)
{
    int current = entity_index;
    while (current >= 0) {
        if (!entities[current].visible) {
            return false;
        }
        current = entities[current].parent_index;
    }
    return true;
}

bool entity_is_active(int entity_index, const Entity *entities)
{
    int current = entity_index;
    while (current >= 0) {
        if (!entities[current].active) {
            return false;
        }
        current = entities[current].parent_index;
    }
    return true;
}

#include "entity.h"
#include "alloc.h"
#include "attribute.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

VEC_IMPL(entity, Entity)

#include "raylib.h"

#include <string.h>

const Attribute *entity_get_attr(const Entity *entity, const char *name)
{
    if (entity->defaults) {
        return attr_get_scoped(&entity->attrs, entity->defaults, name);
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

bool entity_init(Entity *entity, EntitySpec spec, Vector2 position, Allocator *alloc)
{
    memset(entity, 0, sizeof(*entity));

    if (!str_from_strv(alloc, &entity->blueprint_name, spec.blueprint_name)) {
        return false;
    }
    entity->defaults = spec.defaults;

    entity->position = position;
    entity->texture = spec.texture;
    entity->collision_offset = spec.collision_offset;
    entity->collision_size = spec.collision_size;
    entity->collision = (Rectangle){
        position.x + spec.collision_offset.x,
        position.y + spec.collision_offset.y,
        spec.collision_size.x,
        spec.collision_size.y,
    };

    bool is_solid = (bool)((spec.collision_size.x > 0.0F) || (spec.collision_size.y > 0.0F));
    if (!attr_set_bool(alloc, &entity->attrs, "solid", is_solid)) {
        str_free(alloc, &entity->blueprint_name);
        return false;
    }
    entity->parent_index = -1;
    return true;
}

void entity_update_collision(Entity *entity)
{
    entity->collision.x = entity->position.x + entity->collision_offset.x;
    entity->collision.y = entity->position.y + entity->collision_offset.y;
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

Rectangle entity_get_source(const Entity *entity)
{
    return (Rectangle){
        entity_get_float(entity, "src_x", 0.0F),
        entity_get_float(entity, "src_y", 0.0F),
        entity_get_float(entity, "src_w", 0.0F),
        entity_get_float(entity, "src_h", 0.0F),
    };
}

bool entity_is_visible(int entity_index, const Entity *entities)
{
    int current = entity_index;
    while (current >= 0) {
        if (!entity_get_bool(&entities[current], "visible", true)) {
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
        if (!entity_get_bool(&entities[current], "active", true)) {
            return false;
        }
        current = entities[current].parent_index;
    }
    return true;
}

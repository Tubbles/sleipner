#include "entity.h"
#include "alloc.h"
#include "attribute.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

VEC_IMPL(entity, Entity)

#include "raylib.h"

#include <string.h>

bool entity_init(Entity *entity, EntitySpec spec, Vector2 position, Allocator *alloc)
{
    memset(entity, 0, sizeof(*entity));

    entity->blueprint_name = str_new(*alloc);
    if (!str_from_strv(&entity->blueprint_name, spec.blueprint_name)) {
        return false;
    }

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
        return nullptr;
    }

    int source_index = find_entity_index(source, entities, entity_count);
    if (source_index < 0) {
        return nullptr;
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
    return nullptr;
}

Entity *entity_find_by_tag_mut(Entity *source, const char *tag, Entity *entities, int entity_count)
{
    const Entity *result = entity_find_by_tag((const Entity *)source, tag, (const Entity *)entities, entity_count);
    if (!result) {
        return nullptr;
    }
    return &entities[result - entities];
}

bool entity_is_visible(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults)
{
    int current = entity_index;
    while (current >= 0) {
        if (!attr_get_scoped_bool(&entities[current].attrs, entity_defaults[current], "visible", true)) {
            return false;
        }
        current = entities[current].parent_index;
    }
    return true;
}

bool entity_is_active(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults)
{
    int current = entity_index;
    while (current >= 0) {
        if (!attr_get_scoped_bool(&entities[current].attrs, entity_defaults[current], "active", true)) {
            return false;
        }
        current = entities[current].parent_index;
    }
    return true;
}

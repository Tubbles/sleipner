#include "entity.h"
#include "alloc.h"
#include "attribute.h"
#include "collision.h"
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

    entity->parent_index = -1;
    return true;
}

/* One-rect fallback shape for entities with no authored composite: a single
 * COLLIDER_RECT primitive whose offset is the rect's center relative to
 * entity->position (matching the "relative to entity center" convention in
 * collision.h), reconstructed exactly from the collision_offset/size attrs. */
static CollisionPrimitive entity_collision_rect_prim(const Entity *entity, const AttrSet *defaults)
{
    float offset_x = attr_get_scoped_float(&entity->attrs, defaults, "collision_offset_x", 0.0F);
    float offset_y = attr_get_scoped_float(&entity->attrs, defaults, "collision_offset_y", 0.0F);
    float width = attr_get_scoped_float(&entity->attrs, defaults, "collision_w", 0.0F);
    float height = attr_get_scoped_float(&entity->attrs, defaults, "collision_h", 0.0F);
    return (CollisionPrimitive){
        .kind = COLLIDER_RECT,
        .offset = {offset_x + (width / 2.0F), offset_y + (height / 2.0F)},
        .angle_offset = 0.0F,
        .rect = {.half_w = width / 2.0F, .half_h = height / 2.0F},
    };
}

CollisionShape entity_collision_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage)
{
    if (entity->collision_region.prims.count > 0) {
        return entity->collision_region;
    }
    *prim_storage = entity_collision_rect_prim(entity, defaults);
    return (CollisionShape){.prims = {.data = prim_storage, .count = 1, .capacity = 1}};
}

CollisionShape entity_trigger_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage)
{
    if (entity->trigger_region.prims.count > 0) {
        return entity->trigger_region;
    }
    return entity_collision_region(entity, defaults, prim_storage);
}

Rectangle entity_collision_rect(const Entity *entity, const AttrSet *defaults)
{
    CollisionPrimitive prim_storage;
    CollisionShape region = entity_collision_region(entity, defaults, &prim_storage);
    const CollisionPrimitive *rect_prim = &region.prims.data[0];
    return (Rectangle){
        entity->position.x + rect_prim->offset.x - rect_prim->rect.half_w,
        entity->position.y + rect_prim->offset.y - rect_prim->rect.half_h,
        rect_prim->rect.half_w * 2.0F,
        rect_prim->rect.half_h * 2.0F,
    };
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

Vector2 entity_draw_position(const Entity *entity, const AttrSet *defaults)
{
    float offset_x = attr_get_scoped_float(&entity->attrs, defaults, "sprite_offset_x", 0.0F);
    float offset_y = attr_get_scoped_float(&entity->attrs, defaults, "sprite_offset_y", 0.0F);
    return (Vector2){entity->position.x - offset_x, entity->position.y - offset_y};
}

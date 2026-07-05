#include "entity.h"
#include "alloc.h"
#include "attribute.h"
#include "collision.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

VEC_IMPL(entity, Entity)

#include "raylib.h"

#include <math.h>
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
    entity->facing = (Vector2){0.0F, 1.0F}; /* default down (S6.10b, D26) */

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

bool entity_hitbox_is_active(const Entity *entity, const AttrSet *defaults)
{
    if (entity->attack_state_timer <= 0.0F) {
        return false;
    }
    int start = attr_get_scoped_int(&entity->attrs, defaults, "attack_hit_frame_start", ATTACK_HIT_FRAME_START_DEFAULT);
    int end = attr_get_scoped_int(&entity->attrs, defaults, "attack_hit_frame_end", ATTACK_HIT_FRAME_END_DEFAULT);
    return entity->frame_index >= start && entity->frame_index <= end;
}

/* One-rect fallback shape for hitbox_offset_x/y + hitbox_w/h attrs (S6.10a,
 * D26), same math as entity_collision_rect_prim above -- offset is the
 * rect's center relative to entity->position. While the hitbox is active
 * (entity_hitbox_is_active, S6.10b/S6.11b), the center is further shifted
 * by ENTITY_HITBOX_REACH along entity->facing so the hitbox lands in front
 * of the entity. */
static CollisionPrimitive entity_hitbox_rect_prim(const Entity *entity, const AttrSet *defaults)
{
    float offset_x = attr_get_scoped_float(&entity->attrs, defaults, "hitbox_offset_x", 0.0F);
    float offset_y = attr_get_scoped_float(&entity->attrs, defaults, "hitbox_offset_y", 0.0F);
    float width = attr_get_scoped_float(&entity->attrs, defaults, "hitbox_w", 0.0F);
    float height = attr_get_scoped_float(&entity->attrs, defaults, "hitbox_h", 0.0F);
    float reach_x = 0.0F;
    float reach_y = 0.0F;
    if (entity_hitbox_is_active(entity, defaults)) {
        reach_x = entity->facing.x * ENTITY_HITBOX_REACH;
        reach_y = entity->facing.y * ENTITY_HITBOX_REACH;
    }
    return (CollisionPrimitive){
        .kind = COLLIDER_RECT,
        .offset = {offset_x + (width / 2.0F) + reach_x, offset_y + (height / 2.0F) + reach_y},
        .angle_offset = 0.0F,
        .rect = {.half_w = width / 2.0F, .half_h = height / 2.0F},
    };
}

CollisionShape entity_hitbox_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage)
{
    if (entity->hitbox.prims.count > 0) {
        return entity->hitbox;
    }
    *prim_storage = entity_hitbox_rect_prim(entity, defaults);
    return (CollisionShape){.prims = {.data = prim_storage, .count = 1, .capacity = 1}};
}

/* One-rect fallback shape for hurtbox_offset_x/y + hurtbox_w/h attrs
 * (S6.10a, D26), same math as entity_hitbox_rect_prim above. Only used by
 * entity_hurtbox_region when both hurtbox_w/h are authored positive --
 * see that function's doc comment for the further collision_region
 * fallback. */
static CollisionPrimitive entity_hurtbox_rect_prim(const Entity *entity, const AttrSet *defaults)
{
    float offset_x = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_offset_x", 0.0F);
    float offset_y = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_offset_y", 0.0F);
    float width = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_w", 0.0F);
    float height = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_h", 0.0F);
    return (CollisionPrimitive){
        .kind = COLLIDER_RECT,
        .offset = {offset_x + (width / 2.0F), offset_y + (height / 2.0F)},
        .angle_offset = 0.0F,
        .rect = {.half_w = width / 2.0F, .half_h = height / 2.0F},
    };
}

CollisionShape entity_hurtbox_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage)
{
    if (entity->hurtbox.prims.count > 0) {
        return entity->hurtbox;
    }
    float width = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_w", 0.0F);
    float height = attr_get_scoped_float(&entity->attrs, defaults, "hurtbox_h", 0.0F);
    if (width > 0.0F && height > 0.0F) {
        *prim_storage = entity_hurtbox_rect_prim(entity, defaults);
        return (CollisionShape){.prims = {.data = prim_storage, .count = 1, .capacity = 1}};
    }
    return entity_collision_region(entity, defaults, prim_storage);
}

bool entity_apply_damage(Entity *target, const AttrSet *target_defaults, float raw_damage, Allocator *alloc)
{
    if (target->iframe_timer > 0.0F) {
        return false;
    }
    float defense = attr_get_scoped_float(&target->attrs, target_defaults, "defense", 0.0F);
    float damage_dealt = raw_damage - defense;
    if (damage_dealt < 1.0F) {
        damage_dealt = 1.0F;
    }
    float current_health = attr_get_scoped_float(&target->attrs, target_defaults, "health", 0.0F);
    if (!attr_set_float(alloc, &target->attrs, "health", current_health - damage_dealt)) {
        return false;
    }
    target->iframe_timer =
        attr_get_scoped_float(&target->attrs, target_defaults, "iframes", ENTITY_DEFAULT_IFRAME_SECONDS);
    return true;
}

/* Below this length (px), target->position and from_position are treated
 * as coincident -- entity_apply_knockback falls back to target->facing
 * rather than normalizing a near-zero vector. Same rationale/magnitude as
 * game.c's CHASE_STEER_EPSILON. */
#define KNOCKBACK_DIRECTION_EPSILON 0.01F

void entity_apply_knockback(Entity *target, Vector2 from_position, float distance)
{
    if (distance <= 0.0F) {
        return;
    }
    float delta_x = target->position.x - from_position.x;
    float delta_y = target->position.y - from_position.y;
    float length = sqrtf((delta_x * delta_x) + (delta_y * delta_y));
    Vector2 direction = target->facing;
    if (length > KNOCKBACK_DIRECTION_EPSILON) {
        direction = (Vector2){delta_x / length, delta_y / length};
    }
    float speed = (2.0F * distance) / KNOCKBACK_SECONDS;
    target->knockback_velocity = (Vector2){direction.x * speed, direction.y * speed};
    target->knockback_timer = KNOCKBACK_SECONDS;
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

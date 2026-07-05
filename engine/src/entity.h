#pragma once

#include "alloc.h"
#include "attribute.h"
#include "collision.h"
#include "str.h"
#include "vec.h" // IWYU pragma: export

#include "raylib.h"

#include <stdbool.h>

/* Blueprint properties needed to initialize an entity. Caller owns all
 * pointed-to data for the duration of entity_init — nothing is borrowed
 * beyond that call. */
typedef struct {
    Strv blueprint_name;
    Texture2D *texture;
} EntitySpec;

typedef struct {
    Texture2D *texture;

    AttrSet persisted_attrs;
    AttrSet attrs;

    /* Collision/trigger shapes. Empty (prims.count == 0) means absent —
     * pre-S4.5 there is no [[blueprint.collision]] TOML authoring, so both
     * stay empty and entity_collision_region falls back to a one-rect shape
     * built from the collision_offset/collision_w/collision_h attrs. */
    CollisionShape collision_region;
    CollisionShape trigger_region;
    /* Combat regions (S6.10a, D26/D28). Same "empty means absent" contract
     * as collision_region/trigger_region above, but there is no
     * [[blueprint.hitbox]]/[[blueprint.hurtbox]] TOML composite authoring
     * yet -- mirrors trigger_region's own current gap (also never
     * populated from TOML). entity_hitbox_region/entity_hurtbox_region
     * fall back to one-rect attr-derived shapes instead; see those
     * functions below. TODO: authored composite hitbox/hurtbox, same as
     * the still-open trigger_region composite gap. */
    CollisionShape hitbox;
    CollisionShape hurtbox;

    Str blueprint_name;
    Str tag;

    /* Scalar fields (4 bytes each, packed together) */
    int id;
    int anim_row;
    float frame_timer;
    int frame_index;
    int parent_index;
    /* Runtime phase accumulator for the npc_patrol behavior (S6.9b, D30) --
     * a plain runtime field, same footing as frame_timer/frame_index above:
     * NOT emitted to TOML (see toml_emitter.c's [[level.entity]] writer,
     * which only writes blueprint/pos/persisted_attrs). A persisted attr
     * updated every frame would grow gamedata.toml on every save instead. */
    float patrol_phase;
    /* Combat runtime timers (S6.10a, D26), same footing as patrol_phase
     * above -- NOT emitted to TOML, decremented every frame by
     * tick_combat_timers (game.c). iframe_timer counts down the
     * invincibility window after a hit lands (see entity_apply_damage);
     * hitbox_active_timer counts down how much longer this entity's own
     * hitbox stays live. Both start at 0 (inert) via entity_init's
     * memset; the S6.10b attack activator is what sets
     * hitbox_active_timer to a positive value. */
    float iframe_timer;
    float hitbox_active_timer;

    /* Vectors (8 bytes each) */
    Vector2 position;
    Vector2 offset;

    /* Bools (1 byte each, packed at end) */
    bool flip;
    bool moving;
} Entity;

/* Initialize an entity from a spec. Copies blueprint_name.
 * Returns false on allocation failure. */
[[nodiscard]] bool entity_init(Entity *entity, EntitySpec spec, Vector2 position, Allocator *alloc);

/* Build the collision region for an entity: entity->collision_region if an
 * authored composite shape is present, otherwise a one-rect shape derived
 * from the collision_offset_x/y and collision_w/h attrs (scoped lookup, so
 * blueprint edits are reflected immediately). prim_storage is caller-owned
 * scratch space for the fallback single primitive, avoiding a heap/arena
 * allocation for this common case — the returned CollisionShape borrows it,
 * so prim_storage must outlive any use of the shape. */
CollisionShape entity_collision_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage);

/* Build the trigger region for an entity: entity->trigger_region if an
 * authored composite shape is present, otherwise the entity's collision
 * region (entity_collision_region) — trigger zones default to the physical
 * body until a dedicated trigger shape is authored. prim_storage is
 * caller-owned scratch space for the collision_region fallback's single
 * primitive; see entity_collision_region for lifetime rules. */
CollisionShape entity_trigger_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage);

/* Compute the collision rectangle for an entity, derived from
 * entity_collision_region. Uses scoped attr lookup so blueprint edits to
 * collision_offset/size are reflected immediately. */
Rectangle entity_collision_rect(const Entity *entity, const AttrSet *defaults);

/* Build the hitbox region for an entity (S6.10a, D26/D28): entity->hitbox
 * if an authored composite is present, otherwise a one-rect shape derived
 * from the hitbox_offset_x/y and hitbox_w/h attrs (scoped lookup, default
 * 0 -- an entity with none of these authored has an inert, zero-size
 * hitbox). prim_storage has the same caller-owned-scratch-space lifetime
 * rule as entity_collision_region's. */
CollisionShape entity_hitbox_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage);

/* Build the hurtbox region for an entity (S6.10a, D26/D28): entity->hurtbox
 * if an authored composite is present; else a one-rect shape derived from
 * the hurtbox_offset_x/y and hurtbox_w/h attrs, if both hurtbox_w and
 * hurtbox_h are authored positive; else entity_collision_region -- the
 * entity's physical body is the default damageable area until a dedicated
 * hurtbox is authored. prim_storage has the same lifetime rule as
 * entity_collision_region's. */
CollisionShape entity_hurtbox_region(const Entity *entity, const AttrSet *defaults, CollisionPrimitive *prim_storage);

/* Default invincibility window (seconds) applied by entity_apply_damage
 * when the target has no scoped `iframes` attr of its own (S6.10a, D26). */
#define ENTITY_DEFAULT_IFRAME_SECONDS 0.8F

/* Apply one hit of raw_damage to target (S6.10a, D26). No-op (returns
 * false) if target is currently invincible (`iframe_timer > 0`).
 * Otherwise deducts max(1, raw_damage - target's scoped `defense` attr,
 * default 0) from target's scoped `health` attr (written via
 * attr_set_float, mirroring how rule.c's execute_set_attr_action writes
 * health) and resets target->iframe_timer to target's scoped `iframes`
 * attr (default ENTITY_DEFAULT_IFRAME_SECONDS). Does NOT check the
 * resulting health or push TRIGGER_DEFEAT itself -- the caller owns the
 * trigger_events queue and the entity's view index, so it must read
 * health before/after this call and push the event itself, gating on
 * "old health was > 0" the same way execute_set_attr_action/
 * execute_add_attr_action (rule.c) do, so an already-defeated entity
 * hit again doesn't re-fire defeat. */
[[nodiscard]] bool
entity_apply_damage(Entity *target, const AttrSet *target_defaults, float raw_damage, Allocator *alloc);

/* Find an entity by tag within the same composition tree as source.
 * Handles implicit tags: "self", "parent", "root".
 * Returns nullptr if not found. */
const Entity *entity_find_by_tag(const Entity *source, const char *tag, const Entity *entities, int entity_count);

/* Mutable version of entity_find_by_tag. */
Entity *entity_find_by_tag_mut(Entity *source, const char *tag, Entity *entities, int entity_count);

/* Effective visibility: own visible AND all ancestors visible. */
bool entity_is_visible(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

/* Effective active state: own active AND all ancestors active. */
bool entity_is_active(int entity_index, const Entity *entities, const AttrSet *const *entity_defaults);

/* Compute the draw position for an entity. Uses scoped attr lookup so
 * blueprint edits to sprite_offset are reflected immediately. */
Vector2 entity_draw_position(const Entity *entity, const AttrSet *defaults);

VEC_DECL(entity, Entity)

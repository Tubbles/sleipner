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
    /* anim_row/frame_timer/frame_index: render-consumed animation state,
     * written every frame by the generic animation pass (advance_entity_
     * animation, game.c) for any entity whose blueprint authors
     * [[blueprint.animation]] clips (S6.11a, D31) -- derived from the
     * entity's own `state`/`direction` attrs, not written directly by
     * behaviors any more. NOT emitted to TOML, same footing as
     * patrol_phase below. */
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
    /* Countdown for the knockback impulse below (S6.10c, D26), same
     * runtime-only footing as iframe_timer/hitbox_active_timer above --
     * NOT emitted to TOML, decremented every frame by the knockback tick
     * pass (game.c) alongside tick_combat_timers. Starts at 0 (inert) via
     * entity_init's memset; entity_apply_knockback sets it to
     * KNOCKBACK_SECONDS on a landed hit. */
    float knockback_timer;
    /* Countdown for the `projectile` behavior's self-destruct (S6.10d,
     * D26), same runtime-only footing as knockback_timer above -- NOT
     * emitted to TOML, starts at 0 (inert) via entity_init's memset.
     * behavior_projectile (game.c) treats "still exactly 0" as "never
     * seeded yet" and initializes it from the scoped `projectile_lifetime`
     * attr on the entity's first update, then decrements every later
     * frame; reaching 0 again soft-destroys the entity (`active` attr set
     * false), so a projectile fired into empty space eventually
     * disappears instead of flying forever. */
    float projectile_lifetime_timer;

    /* Vectors (8 bytes each) */
    Vector2 position;
    Vector2 offset;
    /* Facing direction (S6.10b, D26): unit cardinal vector this entity is
     * currently oriented toward. Same runtime-only footing as
     * patrol_phase above -- zeroed by entity_init's memset, then
     * explicitly set to {0, 1} (down) so an attack fired before any
     * movement still has a defined direction. behavior_player (game.c)
     * updates it to the cardinal unit vector of the current movement
     * whenever the player moves; entity_hitbox_region reads it to place
     * the attack hitbox in front of the entity while hitbox_active_timer
     * is live (see ENTITY_HITBOX_REACH below). */
    Vector2 facing;
    /* Initial knockback velocity in px/s (S6.10c, D26): set by
     * entity_apply_knockback alongside knockback_timer above, decays
     * linearly to zero as knockback_timer counts down -- see the
     * knockback tick pass (game.c) for the per-frame integration. Zeroed
     * by entity_init's memset. */
    Vector2 knockback_velocity;

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

/* Reach (px) the one-rect hitbox fallback shifts its center along
 * entity->facing while entity->hitbox_active_timer > 0 (S6.10b, D26) --
 * on top of the plain hitbox_offset_x/y attrs, so an active attack's
 * hitbox sits in front of the entity instead of centered on it. Inactive
 * (hitbox_active_timer <= 0) callers see the plain attrs-derived offset,
 * unchanged from before this feature landed. Only applied to the
 * attr-derived fallback, not an authored entity->hitbox composite (no
 * [[blueprint.hitbox]] TOML authoring exists yet -- see TODO.md). */
#define ENTITY_HITBOX_REACH 8.0F

/* Build the hitbox region for an entity (S6.10a/b, D26/D28): entity->hitbox
 * if an authored composite is present, otherwise a one-rect shape derived
 * from the hitbox_offset_x/y and hitbox_w/h attrs (scoped lookup, default
 * 0 -- an entity with none of these authored has an inert, zero-size
 * hitbox), shifted by ENTITY_HITBOX_REACH along entity->facing while the
 * hitbox is active. prim_storage has the same caller-owned-scratch-space
 * lifetime rule as entity_collision_region's. */
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

/* How long a knockback impulse takes to decay to zero (S6.10c, D26).
 * entity_apply_knockback front-loads the whole displacement into an
 * initial knockback_velocity sized so integrating its linear decay to 0
 * over this many seconds covers exactly the requested distance -- see the
 * knockback tick pass (game.c) for the per-frame integration. */
#define KNOCKBACK_SECONDS 0.15F

/* Apply a knockback impulse to target (S6.10c, D26): sets
 * target->knockback_velocity/knockback_timer so target travels `distance`
 * pixels directly away from from_position, decaying to a stop over
 * KNOCKBACK_SECONDS (see the knockback tick pass, game.c, for how the two
 * fields are consumed frame to frame). Direction is
 * normalize(target->position - from_position); if target and
 * from_position coincide (zero-length direction), falls back to
 * target->facing so the push still has a defined direction instead of
 * dividing by zero. distance <= 0 (the common case: attacker has no
 * `knockback` attr) is a no-op -- knockback_velocity/knockback_timer are
 * left untouched. */
void entity_apply_knockback(Entity *target, Vector2 from_position, float distance);

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

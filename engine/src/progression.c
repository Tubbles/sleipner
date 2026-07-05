#include "progression.h"

#include "alloc.h"
#include "attribute.h"
#include "debug.h"
#include "diag.h"
#include "entity.h"
#include "level.h"
#include "map.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

MAP_IMPL(strv_level_delta, Strv, LevelDelta, strv_hash, strv_eq)
VEC_IMPL(entity_delta, EntityDelta)

/* Snapshot one entity's id/position/attrs/active into *out. Returns false
 * (out left however far the copy got) only if attr_set_copy's underlying
 * allocation fails -- essentially unreachable against the arena allocator
 * progression_capture_level_delta is always called with in practice. */
static bool capture_entity_delta(Diag *diag, Allocator *progression_alloc, const Entity *entity, EntityDelta *out)
{
    *out = (EntityDelta){
        .id = entity->id,
        .position = entity->position,
        .active = attr_get_bool(&entity->attrs, "active", true),
    };
    if (!attr_set_copy(progression_alloc, &out->attrs, &entity->attrs)) {
        debug_log(diag->debug, "progression: level delta capture failed for entity %d", entity->id);
        return false;
    }
    return true;
}

void progression_capture_level_delta(Diag *diag,
                                     ProgressionState *progression,
                                     Allocator *progression_alloc,
                                     const Level *level)
{
    LevelDelta delta = {.entities = vec_entity_delta_new(*progression_alloc)};
    for (int index = 0; index < level->entities.count; index++) {
        EntityDelta entity_delta;
        if (!capture_entity_delta(diag, progression_alloc, &level->entities.data[index], &entity_delta)) {
            continue;
        }
        (void)vec_entity_delta_push(&delta.entities, entity_delta);
    }

    /* level->name lives in gamedata_arena, rewound by the very reload
     * this capture precedes, so the key must be copied into
     * progression_alloc here -- never stored as a bare view of the
     * caller's Level. Unlike item_give (rule.c), this always allocates a
     * fresh key rather than checking for an existing entry first: on a
     * recapture of an already-known level, map_set's existing-key path
     * (map.h's MAP_IMPL) finds the match by content and overwrites only
     * .value, so this key ends up orphaned in progression_alloc's arena --
     * one small Str per transition, bounded the same way the overwritten
     * LevelDelta itself already is (see this function's own doc comment). */
    progression->level_deltas.alloc = *progression_alloc;
    Str owned_name = str_new(*progression_alloc);
    if (!str_from_cstr(&owned_name, level->name.ptr)) {
        debug_log(diag->debug, "progression: level delta key allocation failed for '%s'", level->name.ptr);
        return;
    }
    if (!map_strv_level_delta_set(&progression->level_deltas, str_to_strv(owned_name), delta)) {
        debug_log(diag->debug, "progression: level delta insert failed for '%s'", level->name.ptr);
    }
}

void progression_apply_level_delta(Diag *diag,
                                   const ProgressionState *progression,
                                   Level *level,
                                   Allocator *gamedata_alloc)
{
    const LevelDelta *delta = map_strv_level_delta_get(&progression->level_deltas, str_to_strv(level->name));
    if (!delta) {
        return;
    }
    for (int index = 0; index < delta->entities.count; index++) {
        const EntityDelta *entity_delta = &delta->entities.data[index];
        int entity_index = level_find_entity_by_id(level, entity_delta->id);
        if (entity_index < 0) {
            /* Spawned entity from a previous visit, or an id the fresh
             * parse no longer authors -- nothing to restore it onto (v1
             * limitation: spawned entities don't persist across
             * transitions, see TODO.md). */
            continue;
        }
        Entity *entity = &level->entities.data[entity_index];
        entity->position = entity_delta->position;
        if (!attr_set_copy(gamedata_alloc, &entity->attrs, &entity_delta->attrs)) {
            debug_log(diag->debug, "progression: level delta attrs restore failed for entity %d", entity_delta->id);
            continue;
        }
        (void)attr_set_bool(gamedata_alloc, &entity->attrs, "active", entity_delta->active);
    }
}

void progression_clear_level_deltas(ProgressionState *progression)
{
    map_strv_level_delta_free(&progression->level_deltas);
}

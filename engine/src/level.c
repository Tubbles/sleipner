#include "level.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "error.h"
#include "str.h"
#include "toml_str.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

static bool parse_instance_attr(struct EngineContext *ctx, AttrSet *attrs, toml_table_t *table, const char *key)
{
    toml_datum_t bool_value = toml_bool_in(table, key);
    if (bool_value.ok) {
        return attr_set_bool(ctx, attrs, key, (bool)bool_value.u.b);
    }

    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        return attr_set_int(ctx, attrs, key, (int)int_value.u.i);
    }

    toml_datum_t double_value = toml_double_in(table, key);
    if (double_value.ok) {
        return attr_set_float(ctx, attrs, key, (float)double_value.u.d);
    }

    toml_datum_t string_value = toml_string_in(table, key);
    if (string_value.ok) {
        bool result = attr_set_string(ctx, attrs, (AttrStringPair){.name = key, .value = string_value.u.s});
        free(string_value.u.s);
        return result;
    }
    return true;
}

static void parse_instance_overrides(struct EngineContext *ctx, Entity *entity, toml_table_t *entity_table)
{
    int total_keys = toml_table_nkval(entity_table) + toml_table_narr(entity_table) + toml_table_ntab(entity_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *key = toml_key_in(entity_table, key_index);
        if (!key || strcmp(key, "blueprint") == 0) {
            continue;
        }
        if (!parse_instance_attr(ctx, &entity->attrs, entity_table, key)) {
            debug_log(ctx, "ent: attr '%s' failed to set (set full)", key);
            break;
        }
    }
}

#define MAX_CHILD_DEPTH 4

static int entity_depth(const Entity *entities, int entity_index)
{
    int depth = 0;
    int current = entity_index;
    while (entities[current].parent_index >= 0) {
        current = entities[current].parent_index;
        depth++;
    }
    return depth;
}

static bool spawn_children_for(struct EngineContext *ctx,
                               Level *level,
                               int parent_index,
                               const BlueprintTable *blueprints,
                               TextureLookupFn texture_lookup,
                               void *texture_user_data)
{
    const Entity *parent = &level->entities[parent_index];
    const Blueprint *parent_blueprint = parent->blueprint;

    for (int index = 0; index < parent_blueprint->children.count; index++) {
        const BlueprintChild *child_def = &parent_blueprint->children.data[index];

        const Blueprint *child_blueprint = blueprint_find(blueprints, child_def->blueprint_name.ptr);
        if (!child_blueprint) {
            error_set(ctx, "child blueprint '%s' not found", child_def->blueprint_name.ptr);
            error_wrap(ctx, "parent '%s' child[%d]", parent_blueprint->name.ptr, index);
            return false;
        }

        if (level->entity_count >= MAX_LEVEL_ENTITIES) {
            error_set(ctx, "entity limit reached (%d)", MAX_LEVEL_ENTITIES);
            return false;
        }

        Texture2D *texture = texture_lookup(child_blueprint->texture_name.ptr, texture_user_data);
        Vector2 child_position = {parent->position.x + child_def->offset.x, parent->position.y + child_def->offset.y};

        Entity *child = &level->entities[level->entity_count];
        if (!entity_init_from_blueprint(ctx, child, child_blueprint, child_position, texture)) {
            error_set(ctx, "entity_init_from_blueprint failed for child blueprint '%s'", child_blueprint->name.ptr);
            return false;
        }
        child->parent_index = parent_index;
        child->offset = child_def->offset;
        if (child_def->tag.len > 0 && !str_from_strv(ctx, &child->tag, str_to_strv(child_def->tag))) {
            return false;
        }
        level->entity_count++;
    }

    return true;
}

/* Instantiate children for the entity at start_index and all descendants.
 * Iterative: scans forward through newly appended children, which may
 * themselves have children. Parents always appear before children. */
static bool instantiate_children(struct EngineContext *ctx,
                                 Level *level,
                                 int start_index,
                                 const BlueprintTable *blueprints,
                                 TextureLookupFn texture_lookup,
                                 void *texture_user_data)
{
    int scan_index = start_index;
    while (scan_index < level->entity_count) {
        const Entity *entity = &level->entities[scan_index];
        if (entity->blueprint && entity->blueprint->children.count > 0) {
            int depth = entity_depth(level->entities, scan_index);
            if (depth >= MAX_CHILD_DEPTH) {
                error_set(ctx, "child nesting exceeds max depth %d", MAX_CHILD_DEPTH);
                return false;
            }
            if (!spawn_children_for(ctx, level, scan_index, blueprints, texture_lookup, texture_user_data)) {
                return false;
            }
        }
        scan_index++;
    }
    return true;
}

static void parse_entity(struct EngineContext *ctx,
                         Level *level,
                         int entity_index,
                         toml_table_t *entity_table,
                         const BlueprintTable *blueprints,
                         TextureLookupFn texture_lookup,
                         void *texture_user_data)
{
    toml_datum_t bp_name = toml_string_in(entity_table, "blueprint");
    if (!bp_name.ok) {
        debug_log(ctx, "ent[%d]: no 'blueprint' key", entity_index);
        return;
    }
    const Blueprint *blueprint = blueprint_find(blueprints, bp_name.u.s);
    if (!blueprint) {
        debug_log(ctx, "ent[%d]: blueprint '%s' not found", entity_index, bp_name.u.s);
        free(bp_name.u.s);
        return;
    }
    free(bp_name.u.s);

    toml_array_t *pos = toml_array_in(entity_table, "pos");
    if (!pos || toml_array_nelem(pos) != 2) {
        debug_log(ctx, "ent[%d]: missing or bad 'pos' array", entity_index);
        return;
    }
    float position_x = 0;
    float position_y = 0;
    toml_datum_t pos_x = toml_int_at(pos, 0);
    toml_datum_t pos_y = toml_int_at(pos, 1);
    if (pos_x.ok) {
        position_x = (float)pos_x.u.i;
    }
    if (pos_y.ok) {
        position_y = (float)pos_y.u.i;
    }

    Texture2D *texture = texture_lookup(blueprint->texture_name.ptr, texture_user_data);
    if (!texture) {
        debug_log(ctx, "ent[%d]: texture '%s' not found", entity_index, blueprint->texture_name.ptr);
        return;
    }

    int parent_index = level->entity_count;
    Entity *entity = &level->entities[parent_index];
    if (!entity_init_from_blueprint(ctx, entity, blueprint, (Vector2){position_x, position_y}, texture)) {
        debug_log(ctx, "ent[%d]: entity_init_from_blueprint failed", entity_index);
        return;
    }
    parse_instance_overrides(ctx, entity, entity_table);
    level->entity_count++;

    if (!instantiate_children(ctx, level, parent_index, blueprints, texture_lookup, texture_user_data)) {
        debug_log(ctx, "ent[%d]: failed to instantiate children: %s", entity_index, error_get(ctx));
    }
}

static toml_table_t *find_level_table(toml_array_t *levels, const char *level_name)
{
    int level_count = toml_array_nelem(levels);
    for (int index = 0; index < level_count; index++) {
        toml_table_t *candidate = toml_table_at(levels, index);
        if (!level_name) {
            return candidate;
        }
        toml_datum_t name = toml_string_in(candidate, "name");
        if (name.ok) {
            bool match = strcmp(name.u.s, level_name) == 0;
            free(name.u.s);
            if (match) {
                return candidate;
            }
        }
    }
    return NULL;
}

void level_free(struct EngineContext *ctx, Level *level)
{
    str_free(ctx, &level->name);
    str_free(ctx, &level->music_name);
    for (int index = 0; index < level->entity_count; index++) {
        str_free(ctx, &level->entities[index].blueprint_name);
        str_free(ctx, &level->entities[index].tag);
        attr_set_free(ctx, &level->entities[index].attrs);
    }
}

bool level_load(struct EngineContext *ctx,
                Level *level,
                void *toml_root,
                const char *level_name,
                const BlueprintTable *blueprints,
                TextureLookupFn texture_lookup,
                void *texture_user_data)
{
    level_free(ctx, level);
    memset(level, 0, sizeof(*level));

    toml_array_t *levels = toml_array_in(toml_root, "level");
    if (!levels) {
        error_set(ctx, "no [[level]] array in TOML");
        return false;
    }

    toml_table_t *level_table = find_level_table(levels, level_name);
    if (!level_table) {
        error_set(ctx, "level '%s' not found", level_name ? level_name : "(first)");
        return false;
    }

    /* Level name */
    toml_datum_t name = toml_string_in(level_table, "name");
    if (name.ok && !str_from_toml_datum(ctx, &level->name, &name)) {
        return false;
    }

    /* Level music */
    toml_datum_t music = toml_string_in(level_table, "music");
    if (music.ok && !str_from_toml_datum(ctx, &level->music_name, &music)) {
        return false;
    }

    /* Level size */
    toml_array_t *size = toml_array_in(level_table, "size");
    if (size && toml_array_nelem(size) == 2) {
        toml_datum_t width = toml_int_at(size, 0);
        toml_datum_t height = toml_int_at(size, 1);
        if (width.ok) {
            level->width = (int)width.u.i;
        }
        if (height.ok) {
            level->height = (int)height.u.i;
        }
    }

    /* Parse entities */
    toml_array_t *entities = toml_array_in(level_table, "entity");
    if (!entities) {
        return true;
    }

    int entity_count = toml_array_nelem(entities);
    debug_log(ctx, "level: %d entity entries in TOML", entity_count);
    for (int index = 0; index < entity_count && level->entity_count < MAX_LEVEL_ENTITIES; index++) {
        toml_table_t *entity_table = toml_table_at(entities, index);
        if (entity_table) {
            parse_entity(ctx, level, index, entity_table, blueprints, texture_lookup, texture_user_data);
        } else {
            debug_log(ctx, "ent[%d]: toml_table_at returned NULL", index);
        }
    }

    return true;
}

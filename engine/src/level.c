#include "level.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "error.h"
#include "entity.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

static bool parse_instance_attr(AttrSet *attrs, toml_table_t *table, const char *key)
{
    toml_datum_t bool_value = toml_bool_in(table, key);
    if (bool_value.ok) {
        return attr_set_bool(attrs, key, (bool)bool_value.u.b);
    }

    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        return attr_set_int(attrs, key, (int)int_value.u.i);
    }

    toml_datum_t double_value = toml_double_in(table, key);
    if (double_value.ok) {
        return attr_set_float(attrs, key, (float)double_value.u.d);
    }

    toml_datum_t string_value = toml_string_in(table, key);
    if (string_value.ok) {
        bool result = attr_set_string(attrs, (AttrStringPair){.name = key, .value = string_value.u.s});
        free(string_value.u.s);
        return result;
    }
    return true;
}

static void parse_instance_overrides(Entity *entity, toml_table_t *entity_table)
{
    int total_keys = toml_table_nkval(entity_table) + toml_table_narr(entity_table) + toml_table_ntab(entity_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *key = toml_key_in(entity_table, key_index);
        if (!key || strcmp(key, "blueprint") == 0) {
            continue;
        }
        if (!parse_instance_attr(&entity->attrs, entity_table, key)) {
            debug_log("ent: attr '%s' failed to set (set full)", key);
            break;
        }
    }
}

static void parse_entity(Level *level,
                         int entity_index,
                         toml_table_t *entity_table,
                         const BlueprintTable *blueprints,
                         TextureLookupFn texture_lookup,
                         void *texture_user_data)
{
    toml_datum_t bp_name = toml_string_in(entity_table, "blueprint");
    if (!bp_name.ok) {
        debug_log("ent[%d]: no 'blueprint' key", entity_index);
        return;
    }
    const Blueprint *blueprint = blueprint_find(blueprints, bp_name.u.s);
    if (!blueprint) {
        debug_log("ent[%d]: blueprint '%s' not found", entity_index, bp_name.u.s);
        free(bp_name.u.s);
        return;
    }
    free(bp_name.u.s);

    toml_array_t *pos = toml_array_in(entity_table, "pos");
    if (!pos || toml_array_nelem(pos) != 2) {
        debug_log("ent[%d]: missing or bad 'pos' array", entity_index);
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

    Texture2D *texture = texture_lookup(blueprint->texture_name, texture_user_data);
    if (!texture) {
        debug_log("ent[%d]: texture '%s' not found", entity_index, blueprint->texture_name);
        return;
    }

    Entity *entity = &level->entities[level->entity_count];
    entity_init_from_blueprint(entity, blueprint, (Vector2){position_x, position_y}, texture);
    parse_instance_overrides(entity, entity_table);
    level->entity_count++;
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

bool level_load(Level *level,
                void *toml_root,
                const char *level_name,
                const BlueprintTable *blueprints,
                TextureLookupFn texture_lookup,
                void *texture_user_data)
{
    memset(level, 0, sizeof(*level));

    toml_array_t *levels = toml_array_in(toml_root, "level");
    if (!levels) {
        error_set("no [[level]] array in TOML");
        return false;
    }

    toml_table_t *level_table = find_level_table(levels, level_name);
    if (!level_table) {
        error_set("level '%s' not found", level_name ? level_name : "(first)");
        return false;
    }

    /* Level name */
    toml_datum_t name = toml_string_in(level_table, "name");
    if (name.ok) {
        strncpy(level->name, name.u.s, MAX_LEVEL_NAME - 1);
        free(name.u.s);
    }

    /* Level music */
    toml_datum_t music = toml_string_in(level_table, "music");
    if (music.ok) {
        strncpy(level->music_name, music.u.s, MAX_MUSIC_NAME - 1);
        free(music.u.s);
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
    debug_log("level: %d entity entries in TOML", entity_count);
    for (int index = 0; index < entity_count && level->entity_count < MAX_LEVEL_ENTITIES; index++) {
        toml_table_t *entity_table = toml_table_at(entities, index);
        if (entity_table) {
            parse_entity(level, index, entity_table, blueprints, texture_lookup, texture_user_data);
        } else {
            debug_log("ent[%d]: toml_table_at returned NULL", index);
        }
    }

    return true;
}

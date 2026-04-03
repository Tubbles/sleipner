#include "level.h"

#include "alloc.h"
#include "diag.h"
#include "attribute.h"
#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "error.h"
#include "str.h"
#include "strv.h"
#include "toml_str.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

VEC_IMPL(level, Level)

static bool parse_instance_attr(Allocator *alloc, AttrSet *attrs, toml_table_t *table, const char *key)
{
    toml_datum_t bool_value = toml_bool_in(table, key);
    if (bool_value.ok) {
        return attr_set_bool(alloc, attrs, key, bool_value.u.b);
    }

    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        return attr_set_int(alloc, attrs, key, (int)int_value.u.i);
    }

    toml_datum_t double_value = toml_double_in(table, key);
    if (double_value.ok) {
        return attr_set_float(alloc, attrs, key, (float)double_value.u.d);
    }

    toml_datum_t string_value = toml_string_in(table, key);
    if (string_value.ok) {
        bool result = attr_set_string(alloc, attrs, (AttrStringPair){.name = key, .value = string_value.u.s});
        free(string_value.u.s);
        return result;
    }
    return true;
}

static void parse_instance_overrides(DebugState *dbg, Allocator *alloc, Entity *entity, toml_table_t *entity_table)
{
    int total_keys = toml_table_nkval(entity_table) + toml_table_narr(entity_table) + toml_table_ntab(entity_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *key = toml_key_in(entity_table, key_index);
        if (!key || strcmp(key, "blueprint") == 0) {
            continue;
        }
        if (!parse_instance_attr(alloc, &entity->attrs, entity_table, key)) {
            debug_log(dbg, "ent: attr '%s' failed to set (set full)", key);
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

static bool spawn_children_for(ErrorState *err,
                               Allocator *alloc,
                               Level *level,
                               int parent_index,
                               const BlueprintTable *blueprints,
                               TextureLookupFn texture_lookup,
                               void *texture_user_data)
{
    /* Look up the parent blueprint by name (saved before any push that could
     * reallocate the entities vec and invalidate pointers). */
    const Blueprint *parent_blueprint =
        blueprint_find(blueprints, level->entities.data[parent_index].blueprint_name.ptr);
    if (!parent_blueprint || parent_blueprint->children.count == 0) {
        return true;
    }
    Vector2 parent_position = level->entities.data[parent_index].position;

    for (int index = 0; index < parent_blueprint->children.count; index++) {
        const BlueprintChild *child_def = &parent_blueprint->children.data[index];

        const Blueprint *child_blueprint = blueprint_find(blueprints, child_def->blueprint_name.ptr);
        if (!child_blueprint) {
            error_set(err, "child blueprint '%s' not found", child_def->blueprint_name.ptr);
            error_wrap(err, "parent '%s' child[%d]", attr_get_string(&parent_blueprint->attrs, "name"), index);
            return false;
        }

        const char *child_texture_name = attr_get_string(&child_blueprint->attrs, "texture");
        Texture2D *texture = child_texture_name ? texture_lookup(child_texture_name, texture_user_data) : nullptr;
        Vector2 child_position = {parent_position.x + child_def->offset.x, parent_position.y + child_def->offset.y};
        EntitySpec child_spec = {
            .blueprint_name = strv_from_cstr(attr_get_string(&child_blueprint->attrs, "name")),
            .collision_offset = blueprint_get_collision_offset(child_blueprint),
            .collision_size = blueprint_get_collision_size(child_blueprint),
            .texture = texture,
        };

        Entity child = {0};
        if (!entity_init(&child, child_spec, child_position, alloc)) {
            error_set(err, "entity_init failed for child blueprint '%s'",
                      attr_get_string(&child_blueprint->attrs, "name"));
            return false;
        }
        if (!attr_get(&child_blueprint->attrs, "solid")) {
            bool is_solid = (child_spec.collision_size.x > 0.0F) || (child_spec.collision_size.y > 0.0F);
            (void)attr_set_bool(alloc, &child.attrs, "solid", is_solid);
        }
        child.id = level->next_entity_id++;
        child.parent_index = parent_index;
        child.offset = child_def->offset;
        if (child_def->tag.len > 0 && !str_from_strv(alloc, &child.tag, str_to_strv(child_def->tag))) {
            return false;
        }
        if (!vec_entity_push(&level->entities, child)) {
            error_set(err, "spawn_children_for: out of memory");
            return false;
        }
    }

    return true;
}

/* Instantiate children for the entity at start_index and all descendants.
 * Iterative: scans forward through newly appended children, which may
 * themselves have children. Parents always appear before children. */
static bool instantiate_children(ErrorState *err,
                                 Allocator *alloc,
                                 Level *level,
                                 int start_index,
                                 const BlueprintTable *blueprints,
                                 TextureLookupFn texture_lookup,
                                 void *texture_user_data)
{
    int scan_index = start_index;
    while (scan_index < level->entities.count) {
        const Blueprint *scan_blueprint =
            blueprint_find(blueprints, level->entities.data[scan_index].blueprint_name.ptr);
        if (scan_blueprint && scan_blueprint->children.count > 0) {
            int depth = entity_depth(level->entities.data, scan_index);
            if (depth >= MAX_CHILD_DEPTH) {
                error_set(err, "child nesting exceeds max depth %d", MAX_CHILD_DEPTH);
                return false;
            }
            if (!spawn_children_for(err, alloc, level, scan_index, blueprints, texture_lookup, texture_user_data)) {
                return false;
            }
        }
        scan_index++;
    }
    return true;
}

static void parse_entity(Diag *diag,
                         Allocator *alloc,
                         Level *level,
                         int entity_index,
                         toml_table_t *entity_table,
                         const BlueprintTable *blueprints,
                         TextureLookupFn texture_lookup,
                         void *texture_user_data)
{
    toml_datum_t bp_name = toml_string_in(entity_table, "blueprint");
    if (!bp_name.ok) {
        debug_log(diag->debug, "ent[%d]: no 'blueprint' key", entity_index);
        return;
    }
    const Blueprint *blueprint = blueprint_find(blueprints, bp_name.u.s);
    if (!blueprint) {
        debug_log(diag->debug, "ent[%d]: blueprint '%s' not found", entity_index, bp_name.u.s);
        free(bp_name.u.s);
        return;
    }
    free(bp_name.u.s);

    toml_array_t *pos = toml_array_in(entity_table, "pos");
    if (!pos || toml_array_nelem(pos) != 2) {
        debug_log(diag->debug, "ent[%d]: missing or bad 'pos' array", entity_index);
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

    const char *texture_name = attr_get_string(&blueprint->attrs, "texture");
    Texture2D *texture = texture_name ? texture_lookup(texture_name, texture_user_data) : nullptr;

    EntitySpec spec = {
        .blueprint_name = strv_from_cstr(attr_get_string(&blueprint->attrs, "name")),
        .collision_offset = blueprint_get_collision_offset(blueprint),
        .collision_size = blueprint_get_collision_size(blueprint),
        .texture = texture,
    };
    int parent_index = level->entities.count;
    Entity entity_temp = {0};
    if (!entity_init(&entity_temp, spec, (Vector2){position_x, position_y}, alloc)) {
        debug_log(diag->debug, "ent[%d]: entity_init failed", entity_index);
        return;
    }
    if (!attr_get(&blueprint->attrs, "solid")) {
        bool is_solid = (spec.collision_size.x > 0.0F) || (spec.collision_size.y > 0.0F);
        (void)attr_set_bool(alloc, &entity_temp.attrs, "solid", is_solid);
    }
    entity_temp.id = level->next_entity_id++;
    parse_instance_overrides(diag->debug, alloc, &entity_temp, entity_table);
    if (!vec_entity_push(&level->entities, entity_temp)) {
        debug_log(diag->debug, "ent[%d]: out of memory", entity_index);
        return;
    }

    if (!instantiate_children(diag->error, alloc, level, parent_index, blueprints, texture_lookup, texture_user_data)) {
        debug_log(diag->debug, "ent[%d]: failed to instantiate children: %s", entity_index, error_get(diag->error));
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
    return nullptr;
}

bool level_spawn_entity(Diag *diag,
                        Level *level,
                        const Blueprint *blueprint,
                        Vector2 position,
                        const BlueprintTable *blueprints,
                        TextureLookupFn texture_lookup,
                        void *texture_user_data,
                        Allocator *alloc)
{
    const char *texture_name = attr_get_string(&blueprint->attrs, "texture");
    Texture2D *texture = texture_name ? texture_lookup(texture_name, texture_user_data) : nullptr;
    EntitySpec spec = {
        .blueprint_name = strv_from_cstr(attr_get_string(&blueprint->attrs, "name")),
        .collision_offset = blueprint_get_collision_offset(blueprint),
        .collision_size = blueprint_get_collision_size(blueprint),
        .texture = texture,
    };
    int entity_index = level->entities.count;
    Entity entity = {0};
    if (!entity_init(&entity, spec, position, alloc)) {
        error_wrap(diag->error, "level_spawn_entity");
        return false;
    }
    if (!attr_get(&blueprint->attrs, "solid")) {
        bool is_solid = (spec.collision_size.x > 0.0F) || (spec.collision_size.y > 0.0F);
        (void)attr_set_bool(alloc, &entity.attrs, "solid", is_solid);
    }
    entity.id = level->next_entity_id++;
    if (!vec_entity_push(&level->entities, entity)) {
        error_set(diag->error, "level_spawn_entity: out of memory");
        return false;
    }
    if (!instantiate_children(diag->error, alloc, level, entity_index, blueprints, texture_lookup, texture_user_data)) {
        error_wrap(diag->error, "level_spawn_entity");
        return false;
    }
    return true;
}

void level_free(Allocator *alloc, Level *level)
{
    str_free(alloc, &level->name);
    str_free(alloc, &level->music_name);
    str_free(alloc, &level->background_tile);
    for (int index = 0; index < level->entities.count; index++) {
        str_free(alloc, &level->entities.data[index].blueprint_name);
        str_free(alloc, &level->entities.data[index].tag);
        attr_set_free(alloc, &level->entities.data[index].attrs);
    }
    vec_entity_free(&level->entities);
}

static bool parse_level_metadata(Allocator *alloc, Level *level, toml_table_t *level_table)
{
    /* Level name */
    toml_datum_t name = toml_string_in(level_table, "name");
    if (name.ok && !str_from_toml_datum(alloc, &level->name, &name)) {
        return false;
    }

    /* Level music */
    toml_datum_t music = toml_string_in(level_table, "music");
    if (music.ok && !str_from_toml_datum(alloc, &level->music_name, &music)) {
        return false;
    }

    /* Background tile — defaults to "grass.png" */
    toml_datum_t bg_tile = toml_string_in(level_table, "background_tile");
    if (bg_tile.ok) {
        if (!str_from_toml_datum(alloc, &level->background_tile, &bg_tile)) {
            return false;
        }
    } else {
        if (!str_from_cstr(alloc, &level->background_tile, "grass.png")) {
            return false;
        }
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

    /* Background tint — defaults to WHITE, override with [r, g, b] */
    toml_array_t *tint = toml_array_in(level_table, "background_tint");
    if (tint && toml_array_nelem(tint) == 3) {
        toml_datum_t red = toml_int_at(tint, 0);
        toml_datum_t green = toml_int_at(tint, 1);
        toml_datum_t blue = toml_int_at(tint, 2);
        if (red.ok && green.ok && blue.ok) {
            level->background_tint =
                (Color){(unsigned char)red.u.i, (unsigned char)green.u.i, (unsigned char)blue.u.i, 255};
        }
    }

    return true;
}

bool level_load(Diag *diag,
                Level *level,
                void *toml_root,
                const char *level_name,
                const BlueprintTable *blueprints,
                TextureLookupFn texture_lookup,
                void *texture_user_data,
                Allocator *alloc)
{
    /* Reset the level struct — arena_reset in the caller already freed the old data. */
    memset(level, 0, sizeof(*level));
    level->background_tint = WHITE;
    level->entities.alloc = *alloc;

    toml_array_t *levels = toml_array_in(toml_root, "level");
    if (!levels) {
        error_set(diag->error, "no [[level]] array in TOML");
        return false;
    }

    toml_table_t *level_table = find_level_table(levels, level_name);
    if (!level_table) {
        error_set(diag->error, "level '%s' not found", level_name ? level_name : "(first)");
        return false;
    }

    if (!parse_level_metadata(alloc, level, level_table)) {
        return false;
    }

    /* Parse entities */
    toml_array_t *entities = toml_array_in(level_table, "entity");
    if (!entities) {
        return true;
    }

    int entity_count = toml_array_nelem(entities);
    debug_log(diag->debug, "level: %d entity entries in TOML", entity_count);
    for (int index = 0; index < entity_count; index++) {
        toml_table_t *entity_table = toml_table_at(entities, index);
        if (entity_table) {
            parse_entity(diag, alloc, level, index, entity_table, blueprints, texture_lookup, texture_user_data);
        } else {
            debug_log(diag->debug, "ent[%d]: toml_table_at returned nullptr", index);
        }
    }

    return true;
}

bool level_load_others(Diag *diag,
                       vec_level *others,
                       void *toml_root,
                       const char *exclude_name,
                       const BlueprintTable *blueprints,
                       TextureLookupFn texture_lookup,
                       void *texture_user_data,
                       Allocator *alloc)
{
    toml_array_t *levels = toml_array_in(toml_root, "level");
    if (!levels) {
        return true;
    }

    int level_count = toml_array_nelem(levels);
    for (int index = 0; index < level_count; index++) {
        toml_table_t *level_table = toml_table_at(levels, index);
        if (!level_table) {
            continue;
        }

        toml_datum_t name = toml_string_in(level_table, "name");
        if (name.ok && exclude_name && strcmp(name.u.s, exclude_name) == 0) {
            free(name.u.s);
            continue;
        }
        if (name.ok) {
            free(name.u.s);
        }

        Level level = {0};
        level.background_tint = WHITE;
        level.entities.alloc = *alloc;

        if (!parse_level_metadata(alloc, &level, level_table)) {
            return false;
        }

        toml_array_t *entities = toml_array_in(level_table, "entity");
        if (entities) {
            int entity_count = toml_array_nelem(entities);
            for (int entity_index = 0; entity_index < entity_count; entity_index++) {
                toml_table_t *entity_table = toml_table_at(entities, entity_index);
                if (entity_table) {
                    parse_entity(diag, alloc, &level, entity_index, entity_table, blueprints, texture_lookup,
                                 texture_user_data);
                }
            }
        }

        if (!vec_level_push(others, level)) {
            error_set(diag->error, "level_load_others: out of memory");
            return false;
        }
    }
    return true;
}

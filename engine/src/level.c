#include "level.h"

#include "alloc.h"
#include "diag.h"
#include "attribute.h"
#include "blueprint.h"
#include "collision.h"
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
        if (!key || strcmp(key, "blueprint") == 0 || strcmp(key, "pos") == 0) {
            continue;
        }
        if (!parse_instance_attr(alloc, &entity->persisted_attrs, entity_table, key)) {
            debug_log(dbg, "ent: attr '%s' failed to set (set full)", key);
            break;
        }
    }
}

/* Merge every attribute from src into dest (overwriting existing entries by name).
 * Used to initialize the runtime attrs layer from persisted_attrs at load time. */
static bool copy_attr_set(Allocator *alloc, AttrSet *dest, const AttrSet *src)
{
    for (int index = 0; index < src->entries.count; index++) {
        const Attribute *src_attr = &src->entries.data[index];
        const char *name = src_attr->name.ptr;
        bool success = false;
        switch (src_attr->type) {
        case ATTR_FLOAT:
            success = attr_set_float(alloc, dest, name, src_attr->value.f);
            break;
        case ATTR_INT:
            success = attr_set_int(alloc, dest, name, src_attr->value.i);
            break;
        case ATTR_BOOL:
            success = attr_set_bool(alloc, dest, name, src_attr->value.b);
            break;
        case ATTR_STRING:
            success = attr_set_string(alloc, dest, (AttrStringPair){.name = name, .value = src_attr->value.str.ptr});
            break;
        }
        if (!success) {
            return false;
        }
    }
    return true;
}

/* Deep-copy blueprint->collision into entity->collision_region, so
 * entity_collision_region returns the authored composite instead of falling
 * back to the one-rect shape derived from collision_offset/collision_w/h.
 * Deep copy (not aliasing the blueprint's vec) so a later editor edit to the
 * blueprint's collision shape can't reach into (and reallocate/corrupt) an
 * already-instantiated entity's region. A blueprint with no composite
 * (prims.count == 0) leaves entity->collision_region empty, preserving the
 * one-rect fallback. */
static bool copy_blueprint_collision(Allocator *alloc, Entity *entity, const Blueprint *blueprint)
{
    if (blueprint->collision.prims.count == 0) {
        return true;
    }
    entity->collision_region.prims.alloc = *alloc;
    for (int index = 0; index < blueprint->collision.prims.count; index++) {
        if (!vec_collision_prim_push(&entity->collision_region.prims, blueprint->collision.prims.data[index])) {
            return false;
        }
    }
    return true;
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
            .texture = texture,
        };

        Entity child = {0};
        if (!entity_init(&child, child_spec, child_position, alloc)) {
            error_set(err, "entity_init failed for child blueprint '%s'",
                      attr_get_string(&child_blueprint->attrs, "name"));
            return false;
        }
        child.id = level->next_entity_id++;
        child.parent_index = parent_index;
        child.offset = child_def->offset;
        if (child_def->tag.len > 0) {
            child.tag = str_new(*alloc);
            if (!str_from_strv(&child.tag, str_to_strv(child_def->tag))) {
                return false;
            }
        }
        if (!copy_blueprint_collision(alloc, &child, child_blueprint)) {
            error_set(err, "spawn_children_for: collision copy failed for blueprint '%s'",
                      attr_get_string(&child_blueprint->attrs, "name"));
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

/* Find a direct child of parent_index tagged with `tag`. Returns -1 if no
 * instantiated child carries that tag — the caller logs and skips rather
 * than failing the whole load. */
static int find_child_by_tag(const Entity *entities, int parent_index, const char *tag, int entity_count)
{
    for (int index = 0; index < entity_count; index++) {
        if (entities[index].parent_index == parent_index && entities[index].tag.len > 0 &&
            strcmp(entities[index].tag.ptr, tag) == 0) {
            return index;
        }
    }
    return -1;
}

/* Parse every key of a `[level.entity.children.<tag>]` table into persisted_attrs,
 * skipping the "children" key (that's the nested-grandchild sub-table, not an attr). */
static void
parse_child_override_attrs(DebugState *dbg, Allocator *alloc, AttrSet *persisted_attrs, toml_table_t *child_table)
{
    int total_keys = toml_table_nkval(child_table) + toml_table_narr(child_table) + toml_table_ntab(child_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *key = toml_key_in(child_table, key_index);
        if (!key || strcmp(key, "children") == 0) {
            continue;
        }
        if (!parse_instance_attr(alloc, persisted_attrs, child_table, key)) {
            debug_log(dbg, "ent: child override attr '%s' failed to set (set full)", key);
            break;
        }
    }
}

/* Apply persisted-attr overrides from a `children` sub-table (keyed by tag) onto
 * the matching instantiated children of parent_index, then mirror persisted ->
 * runtime attrs the same way root entities are handled (see copy_attr_set).
 * Recurses into each child's own nested "children" table for grandchildren.
 * Bounded by MAX_CHILD_DEPTH — instantiate_children never nests deeper. */
// NOLINTBEGIN(misc-no-recursion) -- bounded by MAX_CHILD_DEPTH, see instantiate_children.
static void apply_child_override_table(
    DebugState *dbg, Allocator *alloc, Level *level, int parent_index, toml_table_t *children_table)
{
    int total_keys =
        toml_table_nkval(children_table) + toml_table_narr(children_table) + toml_table_ntab(children_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *tag = toml_key_in(children_table, key_index);
        if (!tag) {
            continue;
        }
        toml_table_t *child_table = toml_table_in(children_table, tag);
        if (!child_table) {
            continue;
        }

        int child_index = find_child_by_tag(level->entities.data, parent_index, tag, level->entities.count);
        if (child_index < 0) {
            debug_log(dbg, "ent: children override tag '%s' matches no instantiated child", tag);
            continue;
        }

        Entity *child = &level->entities.data[child_index];
        parse_child_override_attrs(dbg, alloc, &child->persisted_attrs, child_table);
        if (!copy_attr_set(alloc, &child->attrs, &child->persisted_attrs)) {
            debug_log(dbg, "ent: children override tag '%s': persisted->runtime attr copy failed", tag);
        }

        toml_table_t *grandchildren = toml_table_in(child_table, "children");
        if (grandchildren) {
            apply_child_override_table(dbg, alloc, level, child_index, grandchildren);
        }
    }
}
// NOLINTEND(misc-no-recursion)

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
        .texture = texture,
    };
    int parent_index = level->entities.count;
    Entity entity_temp = {0};
    if (!entity_init(&entity_temp, spec, (Vector2){position_x, position_y}, alloc)) {
        debug_log(diag->debug, "ent[%d]: entity_init failed", entity_index);
        return;
    }
    entity_temp.id = level->next_entity_id++;
    parse_instance_overrides(diag->debug, alloc, &entity_temp, entity_table);
    if (!copy_attr_set(alloc, &entity_temp.attrs, &entity_temp.persisted_attrs)) {
        debug_log(diag->debug, "ent[%d]: persisted->runtime attr copy failed", entity_index);
        return;
    }
    if (!copy_blueprint_collision(alloc, &entity_temp, blueprint)) {
        debug_log(diag->debug, "ent[%d]: collision copy failed", entity_index);
        return;
    }
    if (!vec_entity_push(&level->entities, entity_temp)) {
        debug_log(diag->debug, "ent[%d]: out of memory", entity_index);
        return;
    }

    if (!instantiate_children(diag->error, alloc, level, parent_index, blueprints, texture_lookup, texture_user_data)) {
        debug_log(diag->debug, "ent[%d]: failed to instantiate children: %s", entity_index, error_get(diag->error));
    }

    toml_table_t *children_table = toml_table_in(entity_table, "children");
    if (children_table) {
        apply_child_override_table(diag->debug, alloc, level, parent_index, children_table);
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

bool level_spawn_single_child(Diag *diag,
                              Level *level,
                              int parent_index,
                              const BlueprintChild *child_def,
                              const BlueprintTable *blueprints,
                              TextureLookupFn texture_lookup,
                              void *texture_user_data,
                              Allocator *alloc)
{
    const Blueprint *child_blueprint = blueprint_find(blueprints, child_def->blueprint_name.ptr);
    if (!child_blueprint) {
        error_set(diag->error, "child blueprint '%s' not found", child_def->blueprint_name.ptr);
        return false;
    }
    Vector2 parent_position = level->entities.data[parent_index].position;
    const char *child_texture_name = attr_get_string(&child_blueprint->attrs, "texture");
    Texture2D *texture = child_texture_name ? texture_lookup(child_texture_name, texture_user_data) : nullptr;
    Vector2 child_position = {parent_position.x + child_def->offset.x, parent_position.y + child_def->offset.y};
    EntitySpec child_spec = {
        .blueprint_name = strv_from_cstr(attr_get_string(&child_blueprint->attrs, "name")),
        .texture = texture,
    };
    Entity child = {0};
    if (!entity_init(&child, child_spec, child_position, alloc)) {
        error_wrap(diag->error, "level_spawn_single_child");
        return false;
    }
    child.id = level->next_entity_id++;
    child.parent_index = parent_index;
    child.offset = child_def->offset;
    if (child_def->tag.len > 0) {
        child.tag = str_new(*alloc);
    }
    if (child_def->tag.len > 0 && !str_from_strv(&child.tag, str_to_strv(child_def->tag))) {
        error_set(diag->error, "level_spawn_single_child: tag copy failed");
        return false;
    }
    if (!copy_blueprint_collision(alloc, &child, child_blueprint)) {
        error_set(diag->error, "level_spawn_single_child: collision copy failed");
        return false;
    }
    int child_entity_index = level->entities.count;
    if (!vec_entity_push(&level->entities, child)) {
        error_set(diag->error, "level_spawn_single_child: out of memory");
        return false;
    }
    if (!instantiate_children(diag->error, alloc, level, child_entity_index, blueprints, texture_lookup,
                              texture_user_data)) {
        error_wrap(diag->error, "level_spawn_single_child");
        return false;
    }
    return true;
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
        .texture = texture,
    };
    int entity_index = level->entities.count;
    Entity entity = {0};
    if (!entity_init(&entity, spec, position, alloc)) {
        error_wrap(diag->error, "level_spawn_entity");
        return false;
    }
    entity.id = level->next_entity_id++;
    if (!copy_blueprint_collision(alloc, &entity, blueprint)) {
        error_set(diag->error, "level_spawn_entity: collision copy failed");
        return false;
    }
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

int level_tiles_wide(int width)
{
    return (width + TILE_SIZE - 1) / TILE_SIZE;
}

int level_tiles_high(int height)
{
    return (height + TILE_SIZE - 1) / TILE_SIZE;
}

int level_tile_index(int row, int col, int tiles_wide)
{
    return (row * tiles_wide) + col;
}

void level_free(Allocator *alloc, Level *level)
{
    str_free(&level->name);
    str_free(&level->music_name);
    str_free(&level->background_tile);
    vec_int_free(&level->tiles_ground);
    vec_int_free(&level->tiles_overlay);
    for (int index = 0; index < level->entities.count; index++) {
        str_free(&level->entities.data[index].blueprint_name);
        str_free(&level->entities.data[index].tag);
        attr_set_free(alloc, &level->entities.data[index].persisted_attrs);
        attr_set_free(alloc, &level->entities.data[index].attrs);
        vec_collision_prim_free(&level->entities.data[index].collision_region.prims);
    }
    vec_entity_free(&level->entities);
}

int level_find_entity_by_id(const Level *level, int entity_id)
{
    for (int index = 0; index < level->entities.count; index++) {
        if (level->entities.data[index].id == entity_id) {
            return index;
        }
    }
    return -1;
}

static void parse_toml_int_pair(toml_table_t *table, const char *key, int output[static 2])
{
    toml_array_t *array = toml_array_in(table, key);
    if (!array || toml_array_nelem(array) != 2) {
        return;
    }
    toml_datum_t first = toml_int_at(array, 0);
    toml_datum_t second = toml_int_at(array, 1);
    if (first.ok) {
        output[0] = (int)first.u.i;
    }
    if (second.ok) {
        output[1] = (int)second.u.i;
    }
}

static void parse_toml_color(toml_table_t *table, const char *key, Color *out)
{
    toml_array_t *array = toml_array_in(table, key);
    if (!array || toml_array_nelem(array) != 3) {
        return;
    }
    toml_datum_t red = toml_int_at(array, 0);
    toml_datum_t green = toml_int_at(array, 1);
    toml_datum_t blue = toml_int_at(array, 2);
    if (red.ok && green.ok && blue.ok) {
        *out = (Color){(unsigned char)red.u.i, (unsigned char)green.u.i, (unsigned char)blue.u.i, 255};
    }
}

static bool parse_level_metadata(Allocator *alloc, Level *level, toml_table_t *level_table)
{
    /* Level name */
    toml_datum_t name = toml_string_in(level_table, "name");
    if (name.ok) {
        level->name = str_new(*alloc);
        if (!str_from_toml_datum(&level->name, &name)) {
            return false;
        }
    }

    /* Level music */
    toml_datum_t music = toml_string_in(level_table, "music");
    if (music.ok) {
        level->music_name = str_new(*alloc);
        if (!str_from_toml_datum(&level->music_name, &music)) {
            return false;
        }
    }

    /* Background tile — defaults to "grass.png" */
    toml_datum_t bg_tile = toml_string_in(level_table, "background_tile");
    if (bg_tile.ok) {
        level->background_tile = str_new(*alloc);
        if (!str_from_toml_datum(&level->background_tile, &bg_tile)) {
            return false;
        }
    } else {
        level->background_tile = str_new(*alloc);
        if (!str_from_cstr(&level->background_tile, "grass.png")) {
            return false;
        }
    }

    /* Level size */
    int size_pair[] = {0, 0};
    parse_toml_int_pair(level_table, "size", size_pair);
    level->width = size_pair[0];
    level->height = size_pair[1];

    /* Floor size — area covered by background tiles. Defaults to level size. */
    int floor_pair[] = {level->width, level->height};
    parse_toml_int_pair(level_table, "floor_size", floor_pair);
    level->floor_width = floor_pair[0];
    level->floor_height = floor_pair[1];

    /* Background tint — defaults to WHITE, override with [r, g, b] */
    parse_toml_color(level_table, "background_tint", &level->background_tint);

    return true;
}

/* True if `rows` is a row-major array-of-arrays with exactly level->tiles_high
 * rows, each exactly level->tiles_wide elements long. Takes `level` (rather
 * than the two dimensions as separate int params) to sidestep
 * bugprone-easily-swappable-parameters — tiles_wide/tiles_high are never
 * meaningful independently of the level they describe. */
static bool tile_layer_dims_match(toml_array_t *rows, const Level *level)
{
    if (toml_array_nelem(rows) != level->tiles_high) {
        return false;
    }
    for (int row = 0; row < level->tiles_high; row++) {
        toml_array_t *row_array = toml_array_at(rows, row);
        if (!row_array || toml_array_nelem(row_array) != level->tiles_wide) {
            return false;
        }
    }
    return true;
}

/* Parse a `key` row-array (`[[tile ids...], ...]`) from level_table into
 * `layer` as a flat, row-major vec_int sized level->tiles_wide *
 * level->tiles_high. A missing key leaves layer empty — the ground layer
 * then falls back to the flat background_tile fill, and the overlay layer
 * draws nothing. A row-array present but with the wrong dimensions is
 * logged and the WHOLE layer is skipped (left empty) rather than padded or
 * truncated: guessing at missing/extra tile data risks silently drawing
 * the wrong tiles, while an empty layer just falls back to existing,
 * well-understood behavior. */
static void parse_tile_layer(
    DebugState *dbg, Allocator *alloc, vec_int *layer, toml_table_t *level_table, const char *key, const Level *level)
{
    *layer = vec_int_new(*alloc);
    toml_array_t *rows = toml_array_in(level_table, key);
    if (!rows) {
        return;
    }
    if (!tile_layer_dims_match(rows, level)) {
        debug_log(dbg, "level: '%s' dimensions do not match level tile grid %dx%d — skipping layer", key,
                  level->tiles_wide, level->tiles_high);
        return;
    }
    for (int row = 0; row < level->tiles_high; row++) {
        toml_array_t *row_array = toml_array_at(rows, row);
        for (int col = 0; col < level->tiles_wide; col++) {
            toml_datum_t value = toml_int_at(row_array, col);
            (void)vec_int_push(layer, value.ok ? (int)value.u.i : 0);
        }
    }
}

/* Compute the level's tile-grid dimensions from its already-parsed
 * width/height, then parse the ground and overlay tile layers against
 * those dimensions. Must run after parse_level_metadata. */
static void parse_level_tiles(DebugState *dbg, Allocator *alloc, Level *level, toml_table_t *level_table)
{
    level->tiles_wide = level_tiles_wide(level->width);
    level->tiles_high = level_tiles_high(level->height);
    parse_tile_layer(dbg, alloc, &level->tiles_ground, level_table, "tiles_ground", level);
    parse_tile_layer(dbg, alloc, &level->tiles_overlay, level_table, "tiles_overlay", level);
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
    parse_level_tiles(diag->debug, alloc, level, level_table);

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
        parse_level_tiles(diag->debug, alloc, &level, level_table);

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

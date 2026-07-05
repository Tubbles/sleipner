#include "blueprint.h"

#include "diag.h"

#include "alloc.h"
#include "arena.h"
#include "atlas.h"
#include "attribute.h"
#include "collision.h"
#include "debug.h"
#include "error.h"
#include "rule.h"
#include "str.h"
#include "toml_str.h"
#include "vec.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)
VEC_IMPL(anim_clip, AnimClip)

static bool parse_float_array(toml_array_t *array, float *out, int expected_count)
{
    if (!array || toml_array_nelem(array) != expected_count) {
        return false;
    }
    for (int index = 0; index < expected_count; index++) {
        toml_datum_t value = toml_double_at(array, index);
        if (!value.ok) {
            /* Try integer */
            toml_datum_t int_value = toml_int_at(array, index);
            if (!int_value.ok) {
                return false;
            }
            out[index] = (float)int_value.u.i;
        } else {
            out[index] = (float)value.u.d;
        }
    }
    return true;
}

/* Keys with dedicated parsing — not treated as custom attributes */
static bool is_known_key(const char *key)
{
    static const char *known[] = {
        "name",  "src",  "collision_offset", "collision_size", "sprite_offset", "health", "animation",
        "child", "rule", "collision",        nullptr};
    for (int index = 0; known[index]; index++) {
        if (strcmp(key, known[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_custom_attr(Allocator *alloc, AttrSet *attrs, toml_table_t *table, const char *key)
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

static bool inherit_attributes(Allocator *alloc, Blueprint *child, const Blueprint *parent)
{
    bool changed = false;
    child->attrs.entries.alloc = *alloc;

    for (int attr_index = 0; attr_index < parent->attrs.entries.count; attr_index++) {
        const Attribute *parent_attr = &parent->attrs.entries.data[attr_index];
        if (attr_get(&child->attrs, parent_attr->name.ptr)) {
            continue;
        }
        /* Deep copy: allocate independent Strs for name and (if ATTR_STRING) value. */
        Attribute new_entry = *parent_attr;
        new_entry.name = str_new(*alloc);
        if (!str_from_strv(&new_entry.name, str_to_strv(parent_attr->name))) {
            continue;
        }
        if (parent_attr->type == ATTR_STRING) {
            new_entry.value.str = str_new(*alloc);
            if (!str_from_strv(&new_entry.value.str, str_to_strv(parent_attr->value.str))) {
                str_free(&new_entry.name);
                continue;
            }
        }
        if (!vec_attribute_push(&child->attrs.entries, new_entry)) {
            str_free(&new_entry.name);
            if (parent_attr->type == ATTR_STRING) {
                str_free(&new_entry.value.str);
            }
            continue;
        }
        changed = true;
    }

    return changed;
}

static bool inherit_from_parent(Allocator *alloc, Blueprint *child, const BlueprintTable *table)
{
    const char *extends = attr_get_string(&child->attrs, "extends");
    if (!extends) {
        return false;
    }

    const Blueprint *parent = blueprint_find(table, extends);
    if (!parent) {
        return false;
    }

    return inherit_attributes(alloc, child, parent);
}

static void resolve_inheritance(Allocator *alloc, BlueprintTable *table)
{
    for (int pass = 0; pass < table->entries.count; pass++) {
        bool changed = false;

        for (int index = 0; index < table->entries.count; index++) {
            changed = inherit_from_parent(alloc, &table->entries.data[index], table) || changed;
        }

        if (!changed) {
            break;
        }
    }
}

static bool parse_geometry(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    float source_values[4] = {0};
    toml_array_t *source = toml_array_in(entry, "src");
    if (parse_float_array(source, source_values, 4)) {
        if (!attr_set_float(alloc, &blueprint->attrs, "src_x", source_values[0])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "src_y", source_values[1])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "src_w", source_values[2])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "src_h", source_values[3])) {
            return false;
        }
    }

    float offset_values[2] = {0};
    toml_array_t *collision_offset = toml_array_in(entry, "collision_offset");
    if (parse_float_array(collision_offset, offset_values, 2)) {
        if (!attr_set_float(alloc, &blueprint->attrs, "collision_offset_x", offset_values[0])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "collision_offset_y", offset_values[1])) {
            return false;
        }
    }

    float size_values[2] = {0};
    toml_array_t *collision_size = toml_array_in(entry, "collision_size");
    if (parse_float_array(collision_size, size_values, 2)) {
        if (!attr_set_float(alloc, &blueprint->attrs, "collision_w", size_values[0])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "collision_h", size_values[1])) {
            return false;
        }
    }

    float sprite_offset_values[2] = {0};
    toml_array_t *sprite_offset = toml_array_in(entry, "sprite_offset");
    if (parse_float_array(sprite_offset, sprite_offset_values, 2)) {
        if (!attr_set_float(alloc, &blueprint->attrs, "sprite_offset_x", sprite_offset_values[0])) {
            return false;
        }
        if (!attr_set_float(alloc, &blueprint->attrs, "sprite_offset_y", sprite_offset_values[1])) {
            return false;
        }
    }

    return true;
}

/* Read an optional float field that may be authored as either a TOML float
 * or int (e.g. `angle = 45` vs `angle = 45.0`); defaults to `default_value`. */
static float parse_collision_scalar(toml_table_t *table, const char *key, float default_value)
{
    toml_datum_t value = toml_double_in(table, key);
    if (value.ok) {
        return (float)value.u.d;
    }
    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        return (float)int_value.u.i;
    }
    return default_value;
}

static bool parse_collision_kind(const char *kind_str, ColliderKind *out_kind)
{
    if (strcmp(kind_str, "rect") == 0) {
        *out_kind = COLLIDER_RECT;
        return true;
    }
    if (strcmp(kind_str, "circle") == 0) {
        *out_kind = COLLIDER_CIRCLE;
        return true;
    }
    if (strcmp(kind_str, "triangle") == 0) {
        *out_kind = COLLIDER_TRIANGLE;
        return true;
    }
    return false;
}

static bool parse_collision_rect(toml_table_t *entry, CollisionPrimitive *prim)
{
    float size_values[2] = {0};
    if (!parse_float_array(toml_array_in(entry, "size"), size_values, 2)) {
        return false;
    }
    prim->rect.half_w = size_values[0] / 2.0F;
    prim->rect.half_h = size_values[1] / 2.0F;
    prim->angle_offset = parse_collision_scalar(entry, "angle", 0.0F);
    return true;
}

static bool parse_collision_circle(toml_table_t *entry, CollisionPrimitive *prim)
{
    toml_datum_t radius = toml_double_in(entry, "radius");
    if (radius.ok) {
        prim->circle.radius = (float)radius.u.d;
        return true;
    }
    toml_datum_t radius_int = toml_int_in(entry, "radius");
    if (!radius_int.ok) {
        return false;
    }
    prim->circle.radius = (float)radius_int.u.i;
    return true;
}

static bool parse_collision_triangle(toml_table_t *entry, CollisionPrimitive *prim)
{
    toml_array_t *verts = toml_array_in(entry, "verts");
    if (!verts || toml_array_nelem(verts) != 3) {
        return false;
    }
    for (int index = 0; index < 3; index++) {
        float vert_values[2] = {0};
        if (!parse_float_array(toml_array_at(verts, index), vert_values, 2)) {
            return false;
        }
        prim->triangle.verts[index] = (Vector2){vert_values[0], vert_values[1]};
    }
    return true;
}

static bool parse_collision_geometry(toml_table_t *entry, CollisionPrimitive *prim)
{
    switch (prim->kind) {
    case COLLIDER_RECT:
        return parse_collision_rect(entry, prim);
    case COLLIDER_CIRCLE:
        return parse_collision_circle(entry, prim);
    case COLLIDER_TRIANGLE:
        return parse_collision_triangle(entry, prim);
    }
    return false;
}

/* Parse one [[blueprint.collision]] entry into `prim`. Returns false (kind
 * missing/unknown, or the kind-specific geometry fields are absent/malformed)
 * so the caller can skip just this primitive without failing the whole
 * blueprint load. */
static bool parse_single_collision_prim(DebugState *dbg, toml_table_t *entry, CollisionPrimitive *prim)
{
    memset(prim, 0, sizeof(*prim));

    toml_datum_t kind_str = toml_string_in(entry, "kind");
    if (!kind_str.ok) {
        debug_log(dbg, "bp collision: missing 'kind'");
        return false;
    }
    bool kind_ok = parse_collision_kind(kind_str.u.s, &prim->kind);
    if (!kind_ok) {
        debug_log(dbg, "bp collision: unknown kind '%s'", kind_str.u.s);
    }
    free(kind_str.u.s);
    if (!kind_ok) {
        return false;
    }

    float offset_values[2] = {0};
    (void)parse_float_array(toml_array_in(entry, "offset"), offset_values, 2);
    prim->offset = (Vector2){offset_values[0], offset_values[1]};

    return parse_collision_geometry(entry, prim);
}

/* Parse the [[blueprint.collision]] array-of-tables into blueprint->collision.
 * Absent array leaves the shape empty (no composite — one-rect attr
 * fallback applies). Invalid entries are logged and skipped individually;
 * only an allocation failure aborts the whole blueprint load. */
static bool parse_collision_shape(DebugState *dbg, Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *collision_array = toml_array_in(entry, "collision");
    if (!collision_array) {
        return true;
    }

    int count = toml_array_nelem(collision_array);
    blueprint->collision.prims.alloc = *alloc;

    for (int index = 0; index < count; index++) {
        toml_table_t *prim_entry = toml_table_at(collision_array, index);
        if (!prim_entry) {
            continue;
        }
        CollisionPrimitive prim;
        if (!parse_single_collision_prim(dbg, prim_entry, &prim)) {
            debug_log(dbg, "bp collision[%d]: skipped (invalid)", index);
            continue;
        }
        if (!vec_collision_prim_push(&blueprint->collision.prims, prim)) {
            return false;
        }
    }

    return true;
}

/* Parse one [[blueprint.animation]] entry into `clip`. Returns false (missing
 * `state`) so the caller can skip just this clip without failing the whole
 * blueprint load, mirroring parse_single_collision_prim's per-entry
 * tolerance. `row`/`frames` default to 0/1 when absent; `speed` reuses
 * parse_collision_scalar so it may be authored as either a TOML int or
 * float, same as collision's `angle`/`radius`. */
static bool parse_single_anim_clip(Allocator *alloc, toml_table_t *entry, AnimClip *clip)
{
    memset(clip, 0, sizeof(*clip));

    toml_datum_t state = toml_string_in(entry, "state");
    if (!state.ok) {
        return false;
    }
    clip->state = str_new(*alloc);
    if (!str_from_toml_datum(&clip->state, &state)) {
        return false;
    }

    toml_datum_t row = toml_int_in(entry, "row");
    clip->row = row.ok ? (int)row.u.i : 0;

    toml_datum_t frames = toml_int_in(entry, "frames");
    clip->frames = frames.ok ? (int)frames.u.i : 1;

    clip->speed = parse_collision_scalar(entry, "speed", 0.0F);

    return true;
}

/* Parse the [[blueprint.animation]] array-of-tables into blueprint->animation
 * (S6.11a, D31). Absent array leaves it empty -- no animation state machine,
 * the entity renders through the static get_source_rect path (main.c).
 * Invalid entries (missing `state`) are logged and skipped individually,
 * mirroring parse_collision_shape's per-entry tolerance; only an allocation
 * failure aborts the whole blueprint load. */
static bool parse_animation_clips(DebugState *dbg, Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *animation_array = toml_array_in(entry, "animation");
    if (!animation_array) {
        return true;
    }

    int count = toml_array_nelem(animation_array);
    blueprint->animation.alloc = *alloc;

    for (int index = 0; index < count; index++) {
        toml_table_t *clip_entry = toml_table_at(animation_array, index);
        if (!clip_entry) {
            continue;
        }
        AnimClip clip;
        if (!parse_single_anim_clip(alloc, clip_entry, &clip)) {
            debug_log(dbg, "bp animation[%d]: skipped (invalid)", index);
            continue;
        }
        if (!vec_anim_clip_push(&blueprint->animation, clip)) {
            return false;
        }
    }

    return true;
}

static bool parse_health(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *health = toml_array_in(entry, "health");
    if (health && toml_array_nelem(health) == 2) {
        toml_datum_t current = toml_int_at(health, 0);
        toml_datum_t max = toml_int_at(health, 1);
        if (current.ok && !attr_set_int(alloc, &blueprint->attrs, "health", (int)current.u.i)) {
            return false;
        }
        if (max.ok && !attr_set_int(alloc, &blueprint->attrs, "max_health", (int)max.u.i)) {
            return false;
        }
    }
    return true;
}

static bool parse_animation(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    toml_table_t *animation = toml_table_in(entry, "animation");
    if (!animation) {
        return true;
    }

    toml_datum_t frames = toml_int_in(animation, "frames");
    if (frames.ok && !attr_set_int(alloc, &blueprint->attrs, "anim_frames", (int)frames.u.i)) {
        return false;
    }
    toml_datum_t size = toml_int_in(animation, "size");
    if (size.ok && !attr_set_int(alloc, &blueprint->attrs, "anim_size", (int)size.u.i)) {
        return false;
    }
    toml_datum_t speed = toml_int_in(animation, "speed");
    if (speed.ok && !attr_set_int(alloc, &blueprint->attrs, "anim_speed", (int)speed.u.i)) {
        return false;
    }
    toml_datum_t row = toml_int_in(animation, "row");
    return !row.ok || attr_set_int(alloc, &blueprint->attrs, "anim_row", (int)row.u.i);
}

static bool parse_custom_attrs(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    int key_count = toml_table_nkval(entry);
    for (int key_index = 0; key_index < key_count; key_index++) {
        const char *key = toml_key_in(entry, key_index);
        if (!key || is_known_key(key)) {
            continue;
        }
        if (!parse_custom_attr(alloc, &blueprint->attrs, entry, key)) {
            return false;
        }
    }
    return true;
}

static bool parse_single_child(Allocator *alloc, BlueprintChild *child, toml_table_t *entry)
{
    memset(child, 0, sizeof(*child));

    toml_datum_t blueprint_name = toml_string_in(entry, "blueprint");
    if (!blueprint_name.ok) {
        return false;
    }
    child->blueprint_name = str_new(*alloc);
    if (!str_from_toml_datum(&child->blueprint_name, &blueprint_name)) {
        return false;
    }

    toml_datum_t tag = toml_string_in(entry, "tag");
    if (tag.ok) {
        child->tag = str_new(*alloc);
        if (!str_from_toml_datum(&child->tag, &tag)) {
            return false;
        }
    }

    toml_array_t *offset = toml_array_in(entry, "offset");
    float offset_values[2] = {0};
    if (parse_float_array(offset, offset_values, 2)) {
        child->offset = (Vector2){offset_values[0], offset_values[1]};
    }

    return true;
}

static bool parse_children(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *children = toml_array_in(entry, "child");
    if (!children) {
        return true;
    }

    int count = toml_array_nelem(children);
    blueprint->children.alloc = *alloc;

    for (int index = 0; index < count; index++) {
        toml_table_t *child_entry = toml_table_at(children, index);
        if (!child_entry) {
            continue;
        }
        BlueprintChild child_entry_data = {0};
        if (!parse_single_child(alloc, &child_entry_data, child_entry)) {
            return false;
        }
        if (!vec_blueprint_child_push(&blueprint->children, child_entry_data)) {
            return false;
        }
    }

    return true;
}

static bool
parse_single_blueprint(Diag *diag, Allocator *alloc, Blueprint *blueprint, toml_table_t *entry, Arena *arena)
{
    memset(blueprint, 0, sizeof(*blueprint));

    toml_datum_t name = toml_string_in(entry, "name");
    if (!name.ok) {
        return false;
    }
    bool name_ok = attr_set_string(alloc, &blueprint->attrs, (AttrStringPair){.name = "name", .value = name.u.s});
    free(name.u.s);
    if (!name_ok) {
        return false;
    }

    if (!parse_geometry(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_collision_shape(diag->debug, alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_health(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_animation(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_animation_clips(diag->debug, alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_custom_attrs(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_children(alloc, blueprint, entry)) {
        return false;
    }
    if (!rules_parse(diag, alloc, &blueprint->rules, entry, arena)) {
        error_wrap(diag->error, "blueprint '%s'", attr_get_string(&blueprint->attrs, "name"));
        return false;
    }
    return true;
}

static void blueprint_cleanup(Allocator *alloc, Blueprint *blp)
{
    for (int child_index = 0; child_index < blp->children.count; child_index++) {
        str_free(&blp->children.data[child_index].blueprint_name);
        str_free(&blp->children.data[child_index].tag);
    }
    vec_blueprint_child_free(&blp->children);
    vec_collision_prim_free(&blp->collision.prims);
    for (int clip_index = 0; clip_index < blp->animation.count; clip_index++) {
        str_free(&blp->animation.data[clip_index].state);
    }
    vec_anim_clip_free(&blp->animation);
    attr_set_free(alloc, &blp->attrs);
}

void blueprint_table_free(Allocator *alloc, BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        blueprint_cleanup(alloc, &table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
    *table = (BlueprintTable){0};
}

Rectangle blueprint_get_source(const Blueprint *blp)
{
    return (Rectangle){
        attr_get_float(&blp->attrs, "src_x", 0.0F),
        attr_get_float(&blp->attrs, "src_y", 0.0F),
        attr_get_float(&blp->attrs, "src_w", 0.0F),
        attr_get_float(&blp->attrs, "src_h", 0.0F),
    };
}

Vector2 blueprint_get_collision_offset(const Blueprint *blp)
{
    return (Vector2){
        attr_get_float(&blp->attrs, "collision_offset_x", 0.0F),
        attr_get_float(&blp->attrs, "collision_offset_y", 0.0F),
    };
}

Vector2 blueprint_get_collision_size(const Blueprint *blp)
{
    return (Vector2){
        attr_get_float(&blp->attrs, "collision_w", 0.0F),
        attr_get_float(&blp->attrs, "collision_h", 0.0F),
    };
}

Vector2 blueprint_get_sprite_offset(const Blueprint *blp)
{
    return (Vector2){
        attr_get_float(&blp->attrs, "sprite_offset_x", 0.0F),
        attr_get_float(&blp->attrs, "sprite_offset_y", 0.0F),
    };
}

int blueprints_load(Diag *diag, BlueprintTable *table, void *toml_root, Arena *arena)
{
    Allocator alloc = allocator_arena(arena);
    /* Reset the table struct — arena_reset in the caller already freed the old data. */
    *table = (BlueprintTable){0};
    table->entries.alloc = alloc;

    toml_array_t *blueprints = toml_array_in(toml_root, "blueprint");
    if (!blueprints) {
        debug_log(diag->debug, "bp: no [[blueprint]] array in TOML root");
        return 0;
    }

    int count = toml_array_nelem(blueprints);
    debug_log(diag->debug, "bp: found %d blueprint entries in TOML", count);

    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(blueprints, index);
        if (!entry) {
            debug_log(diag->debug, "bp[%d]: toml_table_at returned nullptr", index);
            continue;
        }

        int nkval = toml_table_nkval(entry);
        int narr = toml_table_narr(entry);
        int ntab = toml_table_ntab(entry);
        debug_log(diag->debug, "bp[%d]: keys=%d arrays=%d tables=%d", index, nkval, narr, ntab);

        int total_keys = nkval + narr + ntab;
        for (int key_index = 0; key_index < total_keys; key_index++) {
            const char *key = toml_key_in(entry, key_index);
            debug_log(diag->debug, "bp[%d]: key[%d]='%s'", index, key_index, key ? key : "(null)");
        }

        Blueprint temp = {0};
        if (parse_single_blueprint(diag, &alloc, &temp, entry, arena)) {
            debug_log(diag->debug, "bp[%d]: parsed '%s' tex='%s'", index, attr_get_string(&temp.attrs, "name"),
                      attr_get_string(&temp.attrs, "texture"));
            (void)vec_blueprint_push(&table->entries, temp);
        } else {
            blueprint_cleanup(&alloc, &temp);
            toml_datum_t name = toml_string_in(entry, "name");
            debug_log(diag->debug, "bp[%d]: FAILED to parse (name=%s)", index, name.ok ? name.u.s : "missing");
            if (name.ok) {
                free(name.u.s);
            }
        }
    }

    resolve_inheritance(&alloc, table);

    /* Strip 'extends' attr — it has no runtime meaning after inheritance */
    for (int index = 0; index < table->entries.count; index++) {
        attr_remove(&alloc, &table->entries.data[index].attrs, "extends");
    }

    return table->entries.count;
}

static void
resolve_blueprint_sprite(DebugState *dbg, Allocator *alloc, Blueprint *blueprint, const vec_atlas_region *atlas_regions)
{
    const char *sprite_name = attr_get_string(&blueprint->attrs, "sprite");
    if (!sprite_name) {
        return;
    }

    const AtlasRegion *region = atlas_find_region(atlas_regions, sprite_name);
    if (!region) {
        debug_log(dbg, "bp '%s': sprite '%s' not found in atlas, keeping existing src/texture",
                  attr_get_string(&blueprint->attrs, "name"), sprite_name);
        return;
    }

    bool attrs_applied =
        attr_set_float(alloc, &blueprint->attrs, "src_x", region->src.x) &&
        attr_set_float(alloc, &blueprint->attrs, "src_y", region->src.y) &&
        attr_set_float(alloc, &blueprint->attrs, "src_w", region->src.width) &&
        attr_set_float(alloc, &blueprint->attrs, "src_h", region->src.height) &&
        attr_set_string(alloc, &blueprint->attrs, (AttrStringPair){.name = "texture", .value = region->texture.ptr});
    if (!attrs_applied) {
        debug_log(dbg, "bp '%s': failed to apply sprite '%s' attrs", attr_get_string(&blueprint->attrs, "name"),
                  sprite_name);
    }
}

void blueprint_resolve_sprites(Diag *diag,
                               Allocator *alloc,
                               BlueprintTable *table,
                               const vec_atlas_region *atlas_regions)
{
    for (int index = 0; index < table->entries.count; index++) {
        resolve_blueprint_sprite(diag->debug, alloc, &table->entries.data[index], atlas_regions);
    }
}

const Blueprint *blueprint_find(const BlueprintTable *table, const char *name)
{
    for (int index = 0; index < table->entries.count; index++) {
        const char *bp_name = attr_get_string(&table->entries.data[index].attrs, "name");
        if (bp_name && strcmp(bp_name, name) == 0) {
            return &table->entries.data[index];
        }
    }
    return nullptr;
}

const AnimClip *blueprint_find_anim_clip(const Blueprint *blueprint, const char *state)
{
    for (int index = 0; index < blueprint->animation.count; index++) {
        if (strcmp(blueprint->animation.data[index].state.ptr, state) == 0) {
            return &blueprint->animation.data[index];
        }
    }
    return nullptr;
}

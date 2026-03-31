#include "blueprint.h"
#include "alloc.h"
#include "arena.h"
#include "attribute.h"
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
    static const char *known[] = {"name", "src",  "collision_offset", "collision_size", "health", "animation", "child",
                                  "rule", nullptr};
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
        new_entry.name = (Str){0};
        if (!str_from_strv(alloc, &new_entry.name, str_to_strv(parent_attr->name))) {
            continue;
        }
        if (parent_attr->type == ATTR_STRING) {
            new_entry.value.str = (Str){0};
            if (!str_from_strv(alloc, &new_entry.value.str, str_to_strv(parent_attr->value.str))) {
                str_free(alloc, &new_entry.name);
                continue;
            }
        }
        if (!vec_attribute_push(&child->attrs.entries, new_entry)) {
            str_free(alloc, &new_entry.name);
            if (parent_attr->type == ATTR_STRING) {
                str_free(alloc, &new_entry.value.str);
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
        if (alloc && alloc->ctx) {
            error_set(alloc->ctx, "child missing required 'blueprint' key");
        }
        return false;
    }
    if (!str_from_toml_datum(alloc, &child->blueprint_name, &blueprint_name)) {
        return false;
    }

    toml_datum_t tag = toml_string_in(entry, "tag");
    if (tag.ok && !str_from_toml_datum(alloc, &child->tag, &tag)) {
        return false;
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
            if (alloc && alloc->ctx) {
                error_wrap(alloc->ctx, "blueprint '%s' child[%d]", attr_get_string(&blueprint->attrs, "name"), index);
            }
            return false;
        }
        if (!vec_blueprint_child_push(&blueprint->children, child_entry_data)) {
            if (alloc && alloc->ctx) {
                error_set(alloc->ctx, "blueprint '%s' child[%d]: out of memory",
                          attr_get_string(&blueprint->attrs, "name"), index);
            }
            return false;
        }
    }

    return true;
}

static bool parse_single_blueprint(Allocator *alloc, Blueprint *blueprint, toml_table_t *entry, Arena *arena)
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
    if (!parse_health(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_custom_attrs(alloc, blueprint, entry)) {
        return false;
    }
    if (!parse_children(alloc, blueprint, entry)) {
        return false;
    }
    struct EngineContext *ctx = alloc->ctx;
    if (!rules_parse(ctx, alloc, &blueprint->rules, entry, arena)) {
        if (ctx) {
            error_wrap(ctx, "blueprint '%s'", attr_get_string(&blueprint->attrs, "name"));
        }
        return false;
    }
    return true;
}

static void blueprint_cleanup(Allocator *alloc, Blueprint *blp)
{
    for (int child_index = 0; child_index < blp->children.count; child_index++) {
        str_free(alloc, &blp->children.data[child_index].blueprint_name);
        str_free(alloc, &blp->children.data[child_index].tag);
    }
    vec_blueprint_child_free(&blp->children);
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

int blueprints_load(struct EngineContext *ctx, BlueprintTable *table, void *toml_root, Arena *arena)
{
    Allocator alloc = allocator_arena(ctx, arena);
    /* Reset the table struct — arena_reset in the caller already freed the old data. */
    *table = (BlueprintTable){0};
    table->entries.alloc = alloc;

    toml_array_t *blueprints = toml_array_in(toml_root, "blueprint");
    if (!blueprints) {
        debug_log(ctx, "bp: no [[blueprint]] array in TOML root");
        return 0;
    }

    int count = toml_array_nelem(blueprints);
    debug_log(ctx, "bp: found %d blueprint entries in TOML", count);

    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(blueprints, index);
        if (!entry) {
            debug_log(ctx, "bp[%d]: toml_table_at returned nullptr", index);
            continue;
        }

        int nkval = toml_table_nkval(entry);
        int narr = toml_table_narr(entry);
        int ntab = toml_table_ntab(entry);
        debug_log(ctx, "bp[%d]: keys=%d arrays=%d tables=%d", index, nkval, narr, ntab);

        int total_keys = nkval + narr + ntab;
        for (int key_index = 0; key_index < total_keys; key_index++) {
            const char *key = toml_key_in(entry, key_index);
            debug_log(ctx, "bp[%d]: key[%d]='%s'", index, key_index, key ? key : "(null)");
        }

        Blueprint temp = {0};
        if (parse_single_blueprint(&alloc, &temp, entry, arena)) {
            debug_log(ctx, "bp[%d]: parsed '%s' tex='%s'", index, attr_get_string(&temp.attrs, "name"),
                      attr_get_string(&temp.attrs, "texture"));
            (void)vec_blueprint_push(&table->entries, temp);
        } else {
            blueprint_cleanup(&alloc, &temp);
            toml_datum_t name = toml_string_in(entry, "name");
            debug_log(ctx, "bp[%d]: FAILED to parse (name=%s)", index, name.ok ? name.u.s : "missing");
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

#include "blueprint.h"
#include "arena.h"
#include "attribute.h"
#include "debug.h"
#include "error.h"
#include "rule.h"
#include "str.h"
#include "strv.h"
#include "toml_str.h"
#include "vec.h"

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
    static const char *known[] = {"name",           "texture", "src",    "collision_offset",
                                  "collision_size", "extends", "health", "animation",
                                  "child",          "rule",    NULL};
    for (int index = 0; known[index]; index++) {
        if (strcmp(key, known[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_custom_attr(struct EngineContext *ctx, AttrSet *attrs, toml_table_t *table, const char *key)
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

static bool inherit_rendering_fields(struct EngineContext *ctx, Blueprint *child, const Blueprint *parent)
{
    bool changed = false;

    if (child->texture_name.len == 0 && parent->texture_name.len > 0) {
        if (!str_from_strv(ctx, &child->texture_name, str_to_strv(parent->texture_name))) {
            return changed;
        }
        changed = true;
    }
    if (child->source.width == 0 && child->source.height == 0 &&
        (parent->source.width != 0 || parent->source.height != 0)) {
        child->source = parent->source;
        changed = true;
    }
    if (child->collision_size.x == 0 && child->collision_size.y == 0 &&
        (parent->collision_size.x != 0 || parent->collision_size.y != 0)) {
        child->collision_offset = parent->collision_offset;
        child->collision_size = parent->collision_size;
        changed = true;
    }

    return changed;
}

static bool inherit_attributes(struct EngineContext *ctx, Blueprint *child, const Blueprint *parent)
{
    bool changed = false;

    for (int attr_index = 0; attr_index < parent->attrs.entries.count; attr_index++) {
        const Attribute *parent_attr = &parent->attrs.entries.data[attr_index];
        if (attr_get(&child->attrs, parent_attr->name.ptr)) {
            continue;
        }
        /* Deep copy: allocate independent Strs for name and (if ATTR_STRING) value. */
        Attribute new_entry = *parent_attr;
        new_entry.name = (Str){0};
        if (!str_from_strv(ctx, &new_entry.name, str_to_strv(parent_attr->name))) {
            continue;
        }
        if (parent_attr->type == ATTR_STRING) {
            new_entry.value.str = (Str){0};
            if (!str_from_strv(ctx, &new_entry.value.str, str_to_strv(parent_attr->value.str))) {
                str_free(ctx, &new_entry.name);
                continue;
            }
        }
        if (!vec_attribute_push(&child->attrs.entries, new_entry, NULL)) {
            str_free(ctx, &new_entry.name);
            if (parent_attr->type == ATTR_STRING) {
                str_free(ctx, &new_entry.value.str);
            }
            continue;
        }
        changed = true;
    }

    return changed;
}

static bool inherit_from_parent(struct EngineContext *ctx, Blueprint *child, const BlueprintTable *table)
{
    if (child->extends_name.len == 0) {
        return false;
    }

    const Blueprint *parent = blueprint_find(table, child->extends_name.ptr);
    if (!parent) {
        return false;
    }

    bool changed = inherit_rendering_fields(ctx, child, parent);
    changed = (bool)(inherit_attributes(ctx, child, parent) || changed);
    return changed;
}

static void resolve_inheritance(struct EngineContext *ctx, BlueprintTable *table)
{
    for (int pass = 0; pass < table->entries.count; pass++) {
        bool changed = false;

        for (int index = 0; index < table->entries.count; index++) {
            changed = (bool)(inherit_from_parent(ctx, &table->entries.data[index], table) || changed);
        }

        if (!changed) {
            break;
        }
    }
}

static bool parse_optional_strings(struct EngineContext *ctx, Blueprint *blueprint, toml_table_t *entry)
{
    toml_datum_t extends = toml_string_in(entry, "extends");
    if (extends.ok && !str_from_toml_datum(ctx, &blueprint->extends_name, &extends)) {
        return false;
    }

    toml_datum_t texture = toml_string_in(entry, "texture");
    if (texture.ok) {
        return str_from_toml_datum(ctx, &blueprint->texture_name, &texture);
    }
    return true;
}

static void parse_geometry(Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *source = toml_array_in(entry, "src");
    float source_values[4] = {0};
    if (parse_float_array(source, source_values, 4)) {
        blueprint->source = (Rectangle){source_values[0], source_values[1], source_values[2], source_values[3]};
    }

    toml_array_t *collision_offset = toml_array_in(entry, "collision_offset");
    float offset_values[2] = {0};
    if (parse_float_array(collision_offset, offset_values, 2)) {
        blueprint->collision_offset = (Vector2){offset_values[0], offset_values[1]};
    }

    toml_array_t *collision_size = toml_array_in(entry, "collision_size");
    float size_values[2] = {0};
    if (parse_float_array(collision_size, size_values, 2)) {
        blueprint->collision_size = (Vector2){size_values[0], size_values[1]};
    }
}

static bool parse_health(struct EngineContext *ctx, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *health = toml_array_in(entry, "health");
    if (health && toml_array_nelem(health) == 2) {
        toml_datum_t current = toml_int_at(health, 0);
        toml_datum_t max = toml_int_at(health, 1);
        if (current.ok && !attr_set_int(ctx, &blueprint->attrs, "health", (int)current.u.i)) {
            return false;
        }
        if (max.ok && !attr_set_int(ctx, &blueprint->attrs, "max_health", (int)max.u.i)) {
            return false;
        }
    }
    return true;
}

static bool parse_custom_attrs(struct EngineContext *ctx, Blueprint *blueprint, toml_table_t *entry)
{
    int key_count = toml_table_nkval(entry);
    for (int key_index = 0; key_index < key_count; key_index++) {
        const char *key = toml_key_in(entry, key_index);
        if (!key || is_known_key(key)) {
            continue;
        }
        if (!parse_custom_attr(ctx, &blueprint->attrs, entry, key)) {
            return false;
        }
    }
    return true;
}

static bool parse_single_child(struct EngineContext *ctx, BlueprintChild *child, toml_table_t *entry)
{
    memset(child, 0, sizeof(*child));

    toml_datum_t blueprint_name = toml_string_in(entry, "blueprint");
    if (!blueprint_name.ok) {
        error_set(ctx, "child missing required 'blueprint' key");
        return false;
    }
    if (!str_from_toml_datum(ctx, &child->blueprint_name, &blueprint_name)) {
        return false;
    }

    toml_datum_t tag = toml_string_in(entry, "tag");
    if (tag.ok && !str_from_toml_datum(ctx, &child->tag, &tag)) {
        return false;
    }

    toml_array_t *offset = toml_array_in(entry, "offset");
    float offset_values[2] = {0};
    if (parse_float_array(offset, offset_values, 2)) {
        child->offset = (Vector2){offset_values[0], offset_values[1]};
    }

    return true;
}

static bool parse_children(struct EngineContext *ctx, Blueprint *blueprint, toml_table_t *entry)
{
    toml_array_t *children = toml_array_in(entry, "child");
    if (!children) {
        return true;
    }

    int count = toml_array_nelem(children);

    for (int index = 0; index < count; index++) {
        toml_table_t *child_entry = toml_table_at(children, index);
        if (!child_entry) {
            continue;
        }
        BlueprintChild child_entry_data = {0};
        if (!parse_single_child(ctx, &child_entry_data, child_entry)) {
            error_wrap(ctx, "blueprint '%s' child[%d]", blueprint->name.ptr, index);
            return false;
        }
        if (!vec_blueprint_child_push(&blueprint->children, child_entry_data, NULL)) {
            error_set(ctx, "blueprint '%s' child[%d]: out of memory", blueprint->name.ptr, index);
            return false;
        }
    }

    return true;
}

static bool parse_single_blueprint(struct EngineContext *ctx, Blueprint *blueprint, toml_table_t *entry, Arena *arena)
{
    memset(blueprint, 0, sizeof(*blueprint));

    toml_datum_t name = toml_string_in(entry, "name");
    if (!name.ok) {
        return false;
    }
    if (!str_from_toml_datum(ctx, &blueprint->name, &name)) {
        return false;
    }

    if (!parse_optional_strings(ctx, blueprint, entry)) {
        return false;
    }
    parse_geometry(blueprint, entry);
    if (!parse_health(ctx, blueprint, entry)) {
        return false;
    }
    if (!parse_custom_attrs(ctx, blueprint, entry)) {
        return false;
    }
    if (!parse_children(ctx, blueprint, entry)) {
        return false;
    }
    if (!rules_parse(ctx, &blueprint->rules, entry, arena)) {
        error_wrap(ctx, "blueprint '%s'", blueprint->name.ptr);
        return false;
    }
    return true;
}

static void blueprint_cleanup(struct EngineContext *ctx, Blueprint *blp)
{
    str_free(ctx, &blp->name);
    str_free(ctx, &blp->extends_name);
    str_free(ctx, &blp->texture_name);
    for (int child_index = 0; child_index < blp->children.count; child_index++) {
        str_free(ctx, &blp->children.data[child_index].blueprint_name);
        str_free(ctx, &blp->children.data[child_index].tag);
    }
    vec_blueprint_child_free(&blp->children, NULL);
    attr_set_free(ctx, &blp->attrs);
}

void blueprint_table_free(struct EngineContext *ctx, BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        blueprint_cleanup(ctx, &table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries, NULL);
    *table = (BlueprintTable){0};
}

int blueprints_load(struct EngineContext *ctx, BlueprintTable *table, void *toml_root, Arena *arena)
{
    blueprint_table_free(ctx, table);

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
            debug_log(ctx, "bp[%d]: toml_table_at returned NULL", index);
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
        if (parse_single_blueprint(ctx, &temp, entry, arena)) {
            debug_log(ctx, "bp[%d]: parsed '%s' tex='%s'", index, temp.name.ptr, temp.texture_name.ptr);
            (void)vec_blueprint_push(&table->entries, temp, NULL);
        } else {
            blueprint_cleanup(ctx, &temp);
            toml_datum_t name = toml_string_in(entry, "name");
            debug_log(ctx, "bp[%d]: FAILED to parse (name=%s)", index, name.ok ? name.u.s : "missing");
            if (name.ok) {
                free(name.u.s);
            }
        }
    }

    resolve_inheritance(ctx, table);

    return table->entries.count;
}

const Blueprint *blueprint_find(const BlueprintTable *table, const char *name)
{
    for (int index = 0; index < table->entries.count; index++) {
        if ((int)strv_eq_cstr(str_to_strv(table->entries.data[index].name), name)) {
            return &table->entries.data[index];
        }
    }
    return NULL;
}

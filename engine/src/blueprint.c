#include "blueprint.h"

#include "toml.h"

#include <stdlib.h>
#include <string.h>

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

static void parse_custom_attr(AttrSet *attrs, toml_table_t *table, const char *key)
{
    toml_datum_t bool_value = toml_bool_in(table, key);
    if (bool_value.ok) {
        attr_set_bool(attrs, key, (bool)bool_value.u.b);
        return;
    }

    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        attr_set_int(attrs, key, (int)int_value.u.i);
        return;
    }

    toml_datum_t double_value = toml_double_in(table, key);
    if (double_value.ok) {
        attr_set_float(attrs, key, (float)double_value.u.d);
        return;
    }

    toml_datum_t string_value = toml_string_in(table, key);
    if (string_value.ok) {
        attr_set_string(attrs, key, string_value.u.s);
        free(string_value.u.s);
    }
}

static void resolve_inheritance(BlueprintTable *table)
{
    for (int pass = 0; pass < table->count; pass++) {
        bool changed = false;

        for (int index = 0; index < table->count; index++) {
            Blueprint *child = &table->entries[index];
            if (child->extends_name[0] == '\0') {
                continue;
            }

            const Blueprint *parent = blueprint_find(table, child->extends_name);
            if (!parent) {
                continue;
            }

            /* Inherit rendering fields if missing */
            if (child->texture_name[0] == '\0' && parent->texture_name[0] != '\0') {
                strncpy(child->texture_name, parent->texture_name, MAX_TEXTURE_NAME - 1);
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

            /* Inherit attributes not overridden by child */
            for (int attr_index = 0; attr_index < parent->attrs.count; attr_index++) {
                const Attribute *parent_attr = &parent->attrs.entries[attr_index];
                if (!attr_get(&child->attrs, parent_attr->name) && child->attrs.count < MAX_ATTRS) {
                    child->attrs.entries[child->attrs.count] = *parent_attr;
                    child->attrs.count++;
                    changed = true;
                }
            }
        }

        if (!changed) {
            break;
        }
    }
}

int blueprints_load(BlueprintTable *table, void *toml_root, Arena *arena)
{
    (void)arena;
    table->count = 0;

    toml_array_t *blueprints = toml_array_in(toml_root, "blueprint");
    if (!blueprints) {
        return 0;
    }

    int count = toml_array_nelem(blueprints);
    for (int index = 0; index < count && table->count < MAX_BLUEPRINTS; index++) {
        toml_table_t *entry = toml_table_at(blueprints, index);
        if (!entry) {
            continue;
        }

        Blueprint *blueprint = &table->entries[table->count];
        memset(blueprint, 0, sizeof(*blueprint));

        /* name (required) */
        toml_datum_t name = toml_string_in(entry, "name");
        if (!name.ok) {
            continue;
        }
        strncpy(blueprint->name, name.u.s, MAX_BLUEPRINT_NAME - 1);
        free(name.u.s);

        /* extends (optional) */
        toml_datum_t extends = toml_string_in(entry, "extends");
        if (extends.ok) {
            strncpy(blueprint->extends_name, extends.u.s, MAX_BLUEPRINT_NAME - 1);
            free(extends.u.s);
        }

        /* texture (optional) */
        toml_datum_t texture = toml_string_in(entry, "texture");
        if (texture.ok) {
            strncpy(blueprint->texture_name, texture.u.s, MAX_TEXTURE_NAME - 1);
            free(texture.u.s);
        }

        /* src = [x, y, w, h] (optional) */
        toml_array_t *source = toml_array_in(entry, "src");
        float source_values[4] = {0};
        if (parse_float_array(source, source_values, 4)) {
            blueprint->source = (Rectangle){source_values[0], source_values[1], source_values[2], source_values[3]};
        }

        /* collision_offset = [x, y] (optional) */
        toml_array_t *collision_offset = toml_array_in(entry, "collision_offset");
        float offset_values[2] = {0};
        if (parse_float_array(collision_offset, offset_values, 2)) {
            blueprint->collision_offset = (Vector2){offset_values[0], offset_values[1]};
        }

        /* collision_size = [w, h] (optional) */
        toml_array_t *collision_size = toml_array_in(entry, "collision_size");
        float size_values[2] = {0};
        if (parse_float_array(collision_size, size_values, 2)) {
            blueprint->collision_size = (Vector2){size_values[0], size_values[1]};
        }

        /* health = [current, max] (optional, stored as two int attrs) */
        toml_array_t *health = toml_array_in(entry, "health");
        if (health && toml_array_nelem(health) == 2) {
            toml_datum_t current = toml_int_at(health, 0);
            toml_datum_t max = toml_int_at(health, 1);
            if (current.ok) {
                attr_set_int(&blueprint->attrs, "health", (int)current.u.i);
            }
            if (max.ok) {
                attr_set_int(&blueprint->attrs, "max_health", (int)max.u.i);
            }
        }

        /* Parse all other keys as custom attributes */
        int key_count = toml_table_nkval(entry);
        for (int key_index = 0; key_index < key_count; key_index++) {
            const char *key = toml_key_in(entry, key_index);
            if (!key || is_known_key(key)) {
                continue;
            }
            parse_custom_attr(&blueprint->attrs, entry, key);
        }

        table->count++;
    }

    resolve_inheritance(table);

    return table->count;
}

const Blueprint *blueprint_find(const BlueprintTable *table, const char *name)
{
    for (int index = 0; index < table->count; index++) {
        if (strcmp(table->entries[index].name, name) == 0) {
            return &table->entries[index];
        }
    }
    return NULL;
}

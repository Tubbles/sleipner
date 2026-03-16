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

        table->count++;
    }

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

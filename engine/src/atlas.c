#include "atlas.h"

#include "alloc.h"
#include "arena.h"
#include "debug.h"
#include "diag.h"
#include "str.h"
#include "toml_str.h"
#include "vec.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

VEC_IMPL(atlas_region, AtlasRegion)

static bool parse_atlas_src(toml_table_t *entry, Rectangle *out)
{
    toml_array_t *src = toml_array_in(entry, "src");
    if (!src || toml_array_nelem(src) != 4) {
        return false;
    }
    float values[4] = {0};
    for (int index = 0; index < 4; index++) {
        toml_datum_t value = toml_int_at(src, index);
        if (!value.ok) {
            return false;
        }
        values[index] = (float)value.u.i;
    }
    *out = (Rectangle){values[0], values[1], values[2], values[3]};
    return true;
}

static bool region_name_taken(const vec_atlas_region *regions, const char *name)
{
    for (int index = 0; index < regions->count; index++) {
        if (strcmp(regions->data[index].name.ptr, name) == 0) {
            return true;
        }
    }
    return false;
}

static void
parse_atlas_entry(DebugState *dbg, Allocator *alloc, vec_atlas_region *regions, toml_table_t *entry, int index)
{
    toml_datum_t name = toml_string_in(entry, "name");
    if (!name.ok) {
        debug_log(dbg, "atlas.region[%d]: missing 'name'", index);
        return;
    }

    toml_datum_t texture = toml_string_in(entry, "texture");
    if (!texture.ok) {
        debug_log(dbg, "atlas.region[%d]: missing 'texture'", index);
        free(name.u.s);
        return;
    }

    Rectangle src = {0};
    if (!parse_atlas_src(entry, &src)) {
        debug_log(dbg, "atlas.region[%d]: missing or bad 'src' array", index);
        free(name.u.s);
        free(texture.u.s);
        return;
    }

    if (region_name_taken(regions, name.u.s)) {
        debug_log(dbg, "atlas.region[%d]: duplicate name '%s', skipping", index, name.u.s);
        free(name.u.s);
        free(texture.u.s);
        return;
    }

    AtlasRegion region = {.src = src};
    region.name = str_new(*alloc);
    if (!str_from_toml_datum(&region.name, &name)) {
        free(texture.u.s);
        return;
    }
    region.texture = str_new(*alloc);
    if (!str_from_toml_datum(&region.texture, &texture)) {
        return;
    }
    (void)vec_atlas_region_push(regions, region);
}

int atlas_load(Diag *diag, vec_atlas_region *regions, void *toml_root, Arena *arena)
{
    Allocator alloc = allocator_arena(arena);
    *regions = vec_atlas_region_new(alloc);

    toml_table_t *atlas_table = toml_table_in(toml_root, "atlas");
    if (!atlas_table) {
        debug_log(diag->debug, "atlas: no [atlas] table in TOML");
        return 0;
    }

    toml_array_t *entries = toml_array_in(atlas_table, "region");
    if (!entries) {
        debug_log(diag->debug, "atlas: no [[atlas.region]] array in TOML");
        return 0;
    }

    int count = toml_array_nelem(entries);
    debug_log(diag->debug, "atlas: found %d atlas.region entries in TOML", count);
    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(entries, index);
        if (entry) {
            parse_atlas_entry(diag->debug, &alloc, regions, entry, index);
        } else {
            debug_log(diag->debug, "atlas.region[%d]: toml_table_at returned nullptr", index);
        }
    }
    return count;
}

void atlas_free(vec_atlas_region *regions)
{
    for (int index = 0; index < regions->count; index++) {
        str_free(&regions->data[index].name);
        str_free(&regions->data[index].texture);
    }
    vec_atlas_region_free(regions);
}

const AtlasRegion *atlas_find_region(const vec_atlas_region *regions, const char *name)
{
    for (int index = 0; index < regions->count; index++) {
        if (strcmp(regions->data[index].name.ptr, name) == 0) {
            return &regions->data[index];
        }
    }
    return nullptr;
}

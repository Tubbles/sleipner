#include "tileset.h"

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

VEC_IMPL(tileset_entry, TilesetEntry)

/* Push empty placeholder entries until the vec holds at least `min_count`
 * elements, so tileset->data[tile_id] is always a valid index for any id
 * already assigned or about to be. */
static void tileset_ensure_length(vec_tileset_entry *tileset, int min_count)
{
    while (tileset->count < min_count) {
        (void)vec_tileset_entry_push(tileset, (TilesetEntry){0});
    }
}

static bool parse_tileset_src(toml_table_t *entry, Rectangle *out)
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

static void
parse_tileset_entry(DebugState *dbg, Allocator *alloc, vec_tileset_entry *tileset, toml_table_t *entry, int index)
{
    toml_datum_t tile_id = toml_int_in(entry, "id");
    if (!tile_id.ok || tile_id.u.i < 1) {
        debug_log(dbg, "tileset[%d]: missing or invalid 'id'", index);
        return;
    }

    toml_datum_t texture = toml_string_in(entry, "texture");
    if (!texture.ok) {
        debug_log(dbg, "tileset[%d]: missing 'texture'", index);
        return;
    }

    Rectangle src = {0};
    if (!parse_tileset_src(entry, &src)) {
        debug_log(dbg, "tileset[%d]: missing or bad 'src' array", index);
        free(texture.u.s);
        return;
    }

    tileset_ensure_length(tileset, (int)tile_id.u.i + 1);
    TilesetEntry tile_entry = {.src = src};
    tile_entry.texture = str_new(*alloc);
    if (!str_from_toml_datum(&tile_entry.texture, &texture)) {
        return;
    }
    tileset->data[tile_id.u.i] = tile_entry;
}

int tileset_load(Diag *diag, vec_tileset_entry *tileset, void *toml_root, Arena *arena)
{
    Allocator alloc = allocator_arena(arena);
    *tileset = vec_tileset_entry_new(alloc);
    tileset_ensure_length(tileset, 1); /* index 0: unused placeholder */

    toml_array_t *entries = toml_array_in(toml_root, "tileset");
    if (!entries) {
        debug_log(diag->debug, "tileset: no [[tileset]] array in TOML");
        return 0;
    }

    int count = toml_array_nelem(entries);
    debug_log(diag->debug, "tileset: found %d tileset entries in TOML", count);
    for (int index = 0; index < count; index++) {
        toml_table_t *entry = toml_table_at(entries, index);
        if (entry) {
            parse_tileset_entry(diag->debug, &alloc, tileset, entry, index);
        } else {
            debug_log(diag->debug, "tileset[%d]: toml_table_at returned nullptr", index);
        }
    }
    return count;
}

void tileset_free(vec_tileset_entry *tileset)
{
    for (int index = 0; index < tileset->count; index++) {
        str_free(&tileset->data[index].texture);
    }
    vec_tileset_entry_free(tileset);
}

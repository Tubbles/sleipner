#pragma once

#include "arena.h"
#include "diag.h"
#include "str.h"
#include "vec.h"

#include "raylib.h"

/* Maps a tile id to a texture name + source rectangle within that texture.
 * Tile id 0 is reserved for "empty" and never gets an entry — see D36. */
typedef struct {
    Str texture;
    Rectangle src;
} TilesetEntry;

VEC_DECL(tileset_entry, TilesetEntry)

/* Parse every [[tileset]] entry from a tomlc99 root table into `tileset`, a
 * vec indexed directly by tile id: index 0 is an unused placeholder (so
 * `tileset->data[tile_id]` needs no off-by-one correction), and placeholder
 * entries are pushed as needed to reach any higher id. Entries with a
 * missing/invalid id, texture, or src are logged and skipped. A missing
 * [[tileset]] array leaves `tileset` holding just the index-0 placeholder.
 * Returns the number of entries successfully parsed. */
int tileset_load(Diag *diag, vec_tileset_entry *tileset, void *toml_root, Arena *arena);

/* Free every entry's owned Str texture, then the vec itself. */
void tileset_free(vec_tileset_entry *tileset);

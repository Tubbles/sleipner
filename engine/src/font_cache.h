#pragma once

#include "alloc.h"
#include "assets.h"
#include "game.h"
#include "raylib.h"

/* Shared (asset, size) -> Font cache.
 *
 * raylib's Font is a value struct holding a GPU texture plus malloc'd
 * glyphs/recs arrays. Copying a Font copies those pointers, not the
 * data they point to, so two copies UnloadFont'd independently double
 * free. To keep that safe, the cache is the SOLE owner of every Font
 * it hands out: callers (font previews, ui_font, menu, settings) hold
 * non-owning copies and must never call UnloadFont on them. Only
 * font_cache_cleanup unloads, once per unique cache entry. */

/* Pure lookup: index of the (data, size) entry, or -1 if not cached.
 * No raylib calls — safe to exercise headlessly in unit tests. */
int font_cache_find(const vec_font_cache *cache, const unsigned char *data, int size);

/* Return the Font for (asset.data, size). Loads and caches it on a
 * miss via LoadFontFromMemory; returns the existing entry on a hit.
 * Either way the returned Font is a non-owning copy. */
Font font_cache_get(vec_font_cache *cache, EmbeddedAsset asset, int size, Allocator *alloc);

/* Unload every cached Font exactly once, then clear the cache. */
void font_cache_cleanup(vec_font_cache *cache);

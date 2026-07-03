#include "font_cache.h"

#include "alloc.h"
#include "assets.h"
#include "game.h"
#include "raylib.h"
#include "vec.h"

VEC_IMPL(font_cache, FontCacheEntry)

int font_cache_find(const vec_font_cache *cache, const unsigned char *data, int size)
{
    for (int index = 0; index < cache->count; index++) {
        if (cache->data[index].data == data && cache->data[index].size == size) {
            return index;
        }
    }
    return -1;
}

Font font_cache_get(vec_font_cache *cache, EmbeddedAsset asset, int size, Allocator *alloc)
{
    int index = font_cache_find(cache, asset.data, size);
    if (index >= 0) {
        return cache->data[index].font;
    }
    Font font = LoadFontFromMemory(".ttf", asset.data, asset.size, size, nullptr, 0);
    cache->alloc = *alloc;
    (void)vec_font_cache_push(cache, (FontCacheEntry){.data = asset.data, .size = size, .font = font});
    return font;
}

void font_cache_cleanup(vec_font_cache *cache)
{
    for (int index = 0; index < cache->count; index++) {
        if (IsFontValid(cache->data[index].font)) {
            UnloadFont(cache->data[index].font);
        }
    }
    vec_font_cache_clear(cache);
}

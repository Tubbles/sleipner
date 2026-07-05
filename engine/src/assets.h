#pragma once

#include <stdint.h>

typedef struct {
    const unsigned char *data;
    int size;
} EmbeddedAsset;

/* Each embedded asset provides _start and _end symbols from the linker.
 * Use the ASSET() macro to construct an EmbeddedAsset from a symbol name.
 */
#define DECLARE_ASSET(name)                                                                                            \
    extern const unsigned char embed_##name##_start[];                                                                 \
    extern const unsigned char embed_##name##_end[]

#define ASSET(name)                                                                                                    \
    (EmbeddedAsset)                                                                                                    \
    {                                                                                                                  \
        .data = embed_##name##_start, .size = (int)((uintptr_t)embed_##name##_end - (uintptr_t)embed_##name##_start)   \
    }

/* Sprites */
DECLARE_ASSET(player_png);
DECLARE_ASSET(grass_png);
DECLARE_ASSET(tree_png);
DECLARE_ASSET(chest_png);
DECLARE_ASSET(house_png);
DECLARE_ASSET(fence_png);
DECLARE_ASSET(floor_png);

/* Music */
DECLARE_ASSET(bgm_mp3);

/* SFX (placeholders pending real sound design, see U1) */
DECLARE_ASSET(pickup_wav);
DECLARE_ASSET(hit_wav);

/* Fonts */
DECLARE_ASSET(earth_illusion_ttf);
DECLARE_ASSET(golden_apple_ttf);
DECLARE_ASSET(menucard_ttf);
DECLARE_ASSET(nudge_orb_ttf);
DECLARE_ASSET(cardboardcrown_ttf);
DECLARE_ASSET(royalfibre_ttf);

/* Shaders */
DECLARE_ASSET(blur_fs);

/* Input mappings */
DECLARE_ASSET(gamecontrollerdb_txt);

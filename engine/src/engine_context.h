#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include "audio.h"
#include "debug.h"
#include "error.h"
#include "game.h"
#include "raylib.h"
#include "vec.h" // IWYU pragma: export

typedef struct {
    Sound sounds[SOUND_COUNT];
} AudioState;

#define MAX_TEXTURE_FILENAME 64

typedef struct {
    char filename[MAX_TEXTURE_FILENAME];
    Texture2D texture;
} TextureEntry;

VEC_DECL(texture_entry, TextureEntry)

#define FONT_NAME_LEN 64
#define SCREEN_WIDTH_DEFAULT 800
#define SCREEN_HEIGHT_DEFAULT 600

typedef struct {
    char name[FONT_NAME_LEN];
    Font font;
    bool valid;
} FontPreviewEntry;

VEC_DECL(font_preview, FontPreviewEntry)

typedef struct {
    vec_texture_entry textures;
    vec_font_preview font_previews;
} AssetRegistry;

/* The root engine context holding all state to avoid static variables. */
typedef struct EngineContext {
    GameState game;
    ErrorState error;
    DebugState debug;
    AudioState audio;
    AssetRegistry assets;
    long gamedata_mtime;
    int screen_width;
    int screen_height;
} EngineContext;

#endif

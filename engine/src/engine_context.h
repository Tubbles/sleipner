#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include "audio.h"
#include "game.h"
#include "raylib.h"
#include "vec.h" // IWYU pragma: export
#include <stdio.h>

#define ERROR_MSG_LEN 512
#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

typedef struct {
    char error_buffer[ERROR_MSG_LEN];
} ErrorState;

typedef struct {
    char log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
    int log_head;
    int log_count;
    FILE *trace_file;
} DebugState;

typedef struct {
    Sound sounds[SOUND_COUNT];
} AudioState;

#define MAX_TEXTURES 64
#define MAX_TEXTURE_FILENAME 64

typedef struct {
    char filename[MAX_TEXTURE_FILENAME];
    Texture2D texture;
} TextureEntry;

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
    TextureEntry texture_registry[MAX_TEXTURES];
    int texture_registry_count;
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

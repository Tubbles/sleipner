#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include "audio.h"
#include "game.h"
#include "raylib.h"
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

#define MAX_PREVIEW_FONTS 16
#define FONT_NAME_LEN 64

typedef struct {
    char name[FONT_NAME_LEN];
    Font font;
    bool valid;
} FontPreviewEntry;

typedef struct {
    TextureEntry texture_registry[MAX_TEXTURES];
    int texture_registry_count;
    FontPreviewEntry font_preview_entries[MAX_PREVIEW_FONTS];
    int font_preview_count;
} AssetRegistry;

/* The root engine context holding all state to avoid static variables. */
typedef struct EngineContext {
    GameState game;
    ErrorState error;
    DebugState debug;
    AudioState audio;
    AssetRegistry assets;
    long gamedata_mtime;
} EngineContext;

#endif

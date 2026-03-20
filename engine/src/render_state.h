#ifndef RENDER_STATE_H
#define RENDER_STATE_H

#include "raylib.h"

#define MAX_TEXTURES 64
#define MAX_TEXTURE_FILENAME 64

typedef struct {
    char filename[MAX_TEXTURE_FILENAME];
    Texture2D texture;
} TextureEntry;

#define MAX_PREVIEW_FONTS 16
#define FONT_PREVIEW_SIZE 32
#define FONT_NAME_LEN 64

typedef struct {
    char name[FONT_NAME_LEN];
    Font font;
    bool valid;
} FontPreviewEntry;

#endif
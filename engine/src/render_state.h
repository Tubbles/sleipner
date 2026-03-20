#ifndef RENDER_STATE_H
#define RENDER_STATE_H

#include "raylib.h"

/**
 * Render State Management
 * 
 * This module contains data structures for managing render-related state
 * that was previously stored in static variables. All render state is now
 * explicitly owned by GameState for better encapsulation and testability.
 */

#define MAX_TEXTURES 64
#define MAX_TEXTURE_FILENAME 64

/**
 * Texture registry entry - maps texture filenames to loaded Texture2D handles
 */
typedef struct {
    char filename[MAX_TEXTURE_FILENAME];  /**< Texture filename (for lookup) */
    Texture2D texture;                   /**< Loaded texture handle */
} TextureEntry;

#define MAX_PREVIEW_FONTS 16
#define FONT_PREVIEW_SIZE 32
#define FONT_NAME_LEN 64

/**
 * Font preview entry - debug panel font display
 */
typedef struct {
    char name[FONT_NAME_LEN];  /**< Font display name */
    Font font;                /**< Loaded font handle */
    bool valid;               /**< Whether font loaded successfully */
} FontPreviewEntry;

/**
 * Note: These structures are owned by GameState and should not be accessed
 * as global state. All functions operating on them should take GameState*
 * as their first parameter.
 */

#endif
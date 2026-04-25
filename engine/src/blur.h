#pragma once

#include "raylib.h"

#include <stdbool.h>

/* Two-pass Gaussian blur over a captured frame.
 *
 * Used by the pause menu to draw a frozen, blurred snapshot of the scene
 * underneath the menu list. Capture once at menu open; do not re-capture
 * while the menu is held. Output lives in `result` and is a
 * RenderTexture2D the caller can blit to the screen with DrawTexturePro
 * (remember the Y-axis flip raylib applies to RenderTexture2D source
 * rectangles — see usage in main.c's existing target blit). */

typedef struct {
    Shader shader;
    RenderTexture2D ping;
    RenderTexture2D pong;
    int width;
    int height;
    int direction_loc;
    int texel_loc;
    bool valid;
} BlurPipeline;

/* Allocate render targets at the supplied size and load the blur shader.
 * No-op (leaves valid=false) when the GL context is not ready, so headless
 * test runs that touch nothing visual neither crash nor link in extra
 * platform code. Width/height should match the source texture dimensions
 * the caller will pass to blur_capture; mismatched sizes still work but
 * waste fill. */
void blur_init(BlurPipeline *blur, int width, int height);

/* Re-allocate render targets if width/height changed. Cheap no-op when
 * dimensions match. Call on any window-resize event before the next
 * blur_capture; capturing into a stale-sized target produces letterbox
 * artefacts. */
void blur_resize(BlurPipeline *blur, int width, int height);

/* Run a horizontal then vertical 9-tap Gaussian over `source`. The result
 * lives in `blur->ping.texture` until the next call to blur_capture
 * overwrites it. Safe to call before BeginDrawing or inside BeginDrawing
 * — internally uses BeginTextureMode for both passes. */
void blur_capture(BlurPipeline *blur, Texture2D source);

/* Draw the most-recently-captured blurred frame stretched to `dst`. The
 * source texture is sampled with a Y-axis flip to compensate for
 * raylib's render-texture orientation. */
void blur_draw(const BlurPipeline *blur, Rectangle dst);

/* Free GL resources. Safe to call on a zero-initialised or already-freed
 * pipeline. */
void blur_cleanup(BlurPipeline *blur);

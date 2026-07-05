#include "blur.h"

#include "assets.h"
#include "raylib.h"

#include <stddef.h>

void blur_init(BlurPipeline *blur, int width, int height)
{
    *blur = (BlurPipeline){0};
    if (!IsWindowReady()) {
        return;
    }
    blur->shader = LoadShaderFromMemory(NULL, (const char *)ASSET(blur_fs).data);
    blur->direction_loc = GetShaderLocation(blur->shader, "direction");
    blur->texel_loc = GetShaderLocation(blur->shader, "texelSize");
    blur->ping = LoadRenderTexture(width, height);
    blur->pong = LoadRenderTexture(width, height);
    blur->width = width;
    blur->height = height;
    blur->valid = true;
}

void blur_resize(BlurPipeline *blur, int width, int height)
{
    if (!blur->valid) {
        return;
    }
    if (blur->width == width && blur->height == height) {
        return;
    }
    UnloadRenderTexture(blur->ping);
    UnloadRenderTexture(blur->pong);
    blur->ping = LoadRenderTexture(width, height);
    blur->pong = LoadRenderTexture(width, height);
    blur->width = width;
    blur->height = height;
}

static void blur_pass(const BlurPipeline *blur, RenderTexture2D dst, Texture2D src, float dir_x, float dir_y)
{
    float texel[2] = {1.0F / (float)blur->width, 1.0F / (float)blur->height};
    float direction[2] = {dir_x, dir_y};
    SetShaderValue(blur->shader, blur->texel_loc, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(blur->shader, blur->direction_loc, direction, SHADER_UNIFORM_VEC2);
    BeginTextureMode(dst);
    ClearBackground(BLACK);
    BeginShaderMode(blur->shader);
    /* Source render-texture is Y-flipped per raylib convention; sample with
     * a negative-height source rect to draw it right-side-up into the dst. */
    DrawTexturePro(src, (Rectangle){0, 0, (float)src.width, -(float)src.height},
                   (Rectangle){0, 0, (float)blur->width, (float)blur->height}, (Vector2){0, 0}, 0.0F, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void blur_capture(BlurPipeline *blur, Texture2D source)
{
    if (!blur->valid) {
        return;
    }
    blur_pass(blur, blur->pong, source, 1.0F, 0.0F);
    blur_pass(blur, blur->ping, blur->pong.texture, 0.0F, 1.0F);
}

void blur_draw(const BlurPipeline *blur, Rectangle dst)
{
    if (!blur->valid) {
        return;
    }
    DrawTexturePro(blur->ping.texture, (Rectangle){0, 0, (float)blur->width, -(float)blur->height}, dst,
                   (Vector2){0, 0}, 0.0F, WHITE);
}

void blur_cleanup(BlurPipeline *blur)
{
    if (!blur->valid) {
        return;
    }
    UnloadRenderTexture(blur->ping);
    UnloadRenderTexture(blur->pong);
    UnloadShader(blur->shader);
    *blur = (BlurPipeline){0};
}

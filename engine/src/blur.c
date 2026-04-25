#include "blur.h"

#include "raylib.h"

#include <stddef.h>

/* GLSL ES 1.0 — required by the Android raylib codepath; desktop GL
 * accepts it too. Single shader, axis chosen via the `direction` uniform
 * so a horizontal pass uses (1,0) and a vertical pass uses (0,1).
 *
 * 9-tap discrete Gaussian, sigma ≈ 1.8. Soft enough to obscure scene
 * detail at the menu's distance, sharp enough to keep two passes from
 * smearing into a flat colour. Weights sum to 1.0. */
static const char *const blur_fragment_shader =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 texelSize;\n"
    "uniform vec2 direction;\n"
    "void main() {\n"
    "    vec2 offset = texelSize * direction;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    sum += texture2D(texture0, fragTexCoord - 4.0 * offset).rgb * 0.0162162162;\n"
    "    sum += texture2D(texture0, fragTexCoord - 3.0 * offset).rgb * 0.0540540541;\n"
    "    sum += texture2D(texture0, fragTexCoord - 2.0 * offset).rgb * 0.1216216216;\n"
    "    sum += texture2D(texture0, fragTexCoord - 1.0 * offset).rgb * 0.1945945946;\n"
    "    sum += texture2D(texture0, fragTexCoord                ).rgb * 0.2270270270;\n"
    "    sum += texture2D(texture0, fragTexCoord + 1.0 * offset).rgb * 0.1945945946;\n"
    "    sum += texture2D(texture0, fragTexCoord + 2.0 * offset).rgb * 0.1216216216;\n"
    "    sum += texture2D(texture0, fragTexCoord + 3.0 * offset).rgb * 0.0540540541;\n"
    "    sum += texture2D(texture0, fragTexCoord + 4.0 * offset).rgb * 0.0162162162;\n"
    "    gl_FragColor = vec4(sum, 1.0) * fragColor;\n"
    "}\n";

void blur_init(BlurPipeline *blur, int width, int height)
{
    *blur = (BlurPipeline){0};
    if (!IsWindowReady()) {
        return;
    }
    blur->shader = LoadShaderFromMemory(NULL, blur_fragment_shader);
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

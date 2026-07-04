#include "render.h"
#include "particle.h"
#include "rect.h"
#include "shape.h"

#include "raylib.h"
#include <math.h>

#define ARENA_INSET 20
#define ARENA_ROUND 0.05F
#define ARENA_SEGMENTS 16

#define STAR_POINTS 5.0F
#define STAR_INNER_RATIO 0.4F

#define ALPHA_MAX 255.0F
#define PARTICLE_MAX_LIFE 1.5F

#define QUARTER_PI 0.25F
#define THREE_QUARTER_PI 0.75F

static const Color arena_color = {245, 245, 220, 255};

void render_background(RectU32 bounds)
{
    ClearBackground(BLACK);
    Rectangle arena = {ARENA_INSET, ARENA_INSET, (float)bounds.width - (ARENA_INSET * 2),
                       (float)bounds.height - (ARENA_INSET * 2)};
    DrawRectangleRounded(arena, ARENA_ROUND, ARENA_SEGMENTS, arena_color);
}

// size and rotation are the star's radius and spin angle, the two
// scalars that define its silhouette alongside pos and color.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void draw_star(Vector2 pos, float size, float rotation, Color color)
{
    float outer = size;
    float inner = size * STAR_INNER_RATIO;

    for (int index = 0; index < (int)STAR_POINTS; index++) {
        float angle1 = rotation + ((float)index * 2.0F * PI / STAR_POINTS) - (PI / 2.0F);
        float angle2 = angle1 + (PI / STAR_POINTS);
        float angle3 = angle1 + (2.0F * PI / STAR_POINTS);

        Vector2 outer1 = {pos.x + (cosf(angle1) * outer), pos.y + (sinf(angle1) * outer)};
        Vector2 inner1 = {pos.x + (cosf(angle2) * inner), pos.y + (sinf(angle2) * inner)};
        Vector2 outer2 = {pos.x + (cosf(angle3) * outer), pos.y + (sinf(angle3) * outer)};

        DrawTriangle(pos, inner1, outer1, color);
        DrawTriangle(pos, outer2, inner1, color);
    }
}

// size and rotation are the square's half-extent and spin angle,
// mirroring draw_star's shape-transform pair above.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void draw_rotated_square(Vector2 pos, float size, float rotation, Color color)
{
    Vector2 corners[4];
    float angles[4] = {-QUARTER_PI * PI, QUARTER_PI * PI, THREE_QUARTER_PI * PI, -THREE_QUARTER_PI * PI};

    for (int index = 0; index < 4; index++) {
        float angle = rotation + angles[index];
        corners[index] = (Vector2){pos.x + (cosf(angle) * size), pos.y + (sinf(angle) * size)};
    }

    DrawTriangle(corners[2], corners[1], corners[0], color);
    DrawTriangle(corners[3], corners[2], corners[0], color);
}

// size and rotation are the triangle's radius and spin angle, the same
// shape-transform pair as the other draw_rotated_* helpers.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void draw_rotated_triangle(Vector2 pos, float size, float rotation, Color color)
{
    Vector2 verts[3];
    for (int index = 0; index < 3; index++) {
        float angle = rotation + ((float)index * 2.0F * PI / 3.0F) - (PI / 2.0F);
        verts[index] = (Vector2){pos.x + (cosf(angle) * size), pos.y + (sinf(angle) * size)};
    }
    DrawTriangle(verts[2], verts[1], verts[0], color);
}

// rotation and scale are the shape's spin angle and size multiplier,
// forwarded verbatim to whichever draw_* helper kind selects.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void render_shape(ShapeKind kind, Vector2 pos, float rotation, float scale, Color color)
{
    float size = SHAPE_BASE_SIZE * scale;

    switch (kind) {
    case SHAPE_CIRCLE:
        DrawCircleV(pos, size, color);
        break;
    case SHAPE_SQUARE:
        draw_rotated_square(pos, size, rotation, color);
        break;
    case SHAPE_TRIANGLE:
        draw_rotated_triangle(pos, size, rotation, color);
        break;
    case SHAPE_STAR:
        draw_star(pos, size, rotation, color);
        break;
    default:
        break;
    }
}

void render_particles(const Particle *particles, int count)
{
    for (int index = 0; index < count; index++) {
        const Particle *particle = &particles[index];
        unsigned char alpha = (unsigned char)(ALPHA_MAX * (particle->lifetime / PARTICLE_MAX_LIFE));
        if (alpha > 255) {
            alpha = 255;
        }
        Color col = {particle->color.r, particle->color.g, particle->color.b, alpha};
        DrawCircleV(particle->position, particle->size, col);
    }
}

CameraSplit render_split_camera_target(Vector2 camera_target)
{
    Vector2 integer_target = {floorf(camera_target.x), floorf(camera_target.y)};
    Vector2 fractional_offset = {camera_target.x - integer_target.x, camera_target.y - integer_target.y};
    return (CameraSplit){integer_target, fractional_offset};
}

Rectangle render_upscale_dest_rect(Vector2 fractional_offset, int pixel_scale, int screen_width, int screen_height)
{
    return (Rectangle){
        -fractional_offset.x * (float)pixel_scale,
        -fractional_offset.y * (float)pixel_scale,
        (float)screen_width + (float)pixel_scale,
        (float)screen_height + (float)pixel_scale,
    };
}

Vector2 render_world_to_screen_position(Vector2 world_position,
                                        RectU32 game_bounds,
                                        Vector2 camera_target,
                                        int screen_width,
                                        int screen_height,
                                        int pixel_scale)
{
    CameraSplit split = render_split_camera_target(camera_target);
    Vector2 low_res_offset = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F};
    Vector2 low_res_position = {world_position.x - split.integer_target.x + low_res_offset.x,
                                world_position.y - split.integer_target.y + low_res_offset.y};
    Rectangle dest = render_upscale_dest_rect(split.fractional_offset, pixel_scale, screen_width, screen_height);
    float scale_x = dest.width / (float)game_bounds.width;
    float scale_y = dest.height / (float)game_bounds.height;
    return (Vector2){dest.x + (low_res_position.x * scale_x), dest.y + (low_res_position.y * scale_y)};
}

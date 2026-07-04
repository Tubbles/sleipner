#pragma once

#include "raylib.h"
#include "particle.h"
#include "rect.h"
#include "shape.h"

void render_background(RectU32 bounds);
void render_shape(ShapeKind kind, Vector2 pos, float rotation, float scale, Color color);
void render_particles(const Particle *particles, int count);

/* Sub-pixel camera split used by the gameplay render's upscale blit.
 * integer_target is the pixel-perfect Camera2D.target for the low-res
 * render; fractional_offset is the [0, 1) remainder applied as a
 * sub-pixel shift at the upscale blit (render_upscale_dest_rect), so
 * camera panning is smooth instead of jumping in pixel_scale-sized
 * steps as camera_target crosses integer boundaries. */
typedef struct {
    Vector2 integer_target;
    Vector2 fractional_offset;
} CameraSplit;

CameraSplit render_split_camera_target(Vector2 camera_target);

/* Destination rectangle for blitting the low-res render texture up to
 * the screen: oversized by pixel_scale on the right/bottom and shifted
 * left/up by fractional_offset * pixel_scale, so the sub-pixel camera
 * remainder from render_split_camera_target shows up as a smooth shift
 * rather than empty edges or a pixel_scale-sized pop. */
Rectangle render_upscale_dest_rect(Vector2 fractional_offset, int pixel_scale, int screen_width, int screen_height);

/* Final on-screen position a world-space point is drawn at, composing
 * render_split_camera_target and render_upscale_dest_rect exactly the
 * way the gameplay render's DrawTexturePro call does. Not called by the
 * render path itself (the GPU blit does the equivalent scaling), but
 * gives tests a way to check the continuity property the render path
 * relies on: a small change in camera_target must never produce a
 * large jump in the final on-screen position (see render_test.c). */
Vector2 render_world_to_screen_position(Vector2 world_position,
                                        RectU32 game_bounds,
                                        Vector2 camera_target,
                                        int screen_width,
                                        int screen_height,
                                        int pixel_scale);

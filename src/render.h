#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"   // IWYU pragma: export
#include "particle.h" // IWYU pragma: export
#include "shape.h"    // IWYU pragma: export

void render_background(int width, int height);
void render_shape(ShapeKind kind, Vector2 pos, float rotation, float scale, Color color);
void render_particles(const Particle *particles, int count);

#endif

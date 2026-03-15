#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "particle.h"
#include "rect.h"
#include "shape.h"

void render_background(RectU32 bounds);
void render_shape(ShapeKind kind, Vector2 pos, float rotation, float scale, Color color);
void render_particles(const Particle *particles, int count);

#endif

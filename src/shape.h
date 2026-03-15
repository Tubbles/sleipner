#ifndef SHAPE_H
#define SHAPE_H

#include "raylib.h" // IWYU pragma: export

typedef enum { SHAPE_CIRCLE, SHAPE_SQUARE, SHAPE_TRIANGLE, SHAPE_STAR, SHAPE_COUNT } ShapeKind;

#define SHAPE_BASE_SIZE 40.0f

Rectangle shape_bounds(ShapeKind kind, Vector2 pos, float scale);

#endif

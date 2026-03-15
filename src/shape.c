#include "shape.h"
#include "raylib.h"
#include <stdbool.h>

Rectangle shape_bounds(ShapeKind kind, Vector2 pos, float scale)
{
    float size = SHAPE_BASE_SIZE * scale;
    (void)kind;
    return (Rectangle){
        .x = pos.x - size,
        .y = pos.y - size,
        .width = size * 2.0F,
        .height = size * 2.0F,
    };
}

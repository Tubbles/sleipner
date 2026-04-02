#ifndef COLLISION_H
#define COLLISION_H

#include "raylib.h"
#include "rect.h"
#include "vec.h"
#include <stdbool.h>

// cppcheck-suppress noForwardDecl-noForwardDecl
struct EngineContext;

typedef enum { COLLIDER_RECT, COLLIDER_CIRCLE, COLLIDER_TRIANGLE } ColliderKind;

typedef struct {
    ColliderKind kind;
    Vector2 offset;     /* relative to entity center, rotated by entity angle */
    float angle_offset; /* degrees, added to entity rotation (rect only) */
    union {
        struct {
            float half_w, half_h;
        } rect;
        struct {
            float radius;
        } circle;
        struct {
            Vector2 verts[3];
        } triangle; /* relative to offset */
    };
} CollisionPrimitive;

VEC_DECL(collision_prim, CollisionPrimitive)

typedef struct {
    vec_collision_prim prims;
} CollisionShape;

/* Pairwise primitive collision resolvers — return push vector for first arg */
Vector2 resolve_rect_rect(
    Vector2 pos_a, float angle_a, float hw_a, float hh_a, Vector2 pos_b, float angle_b, float hw_b, float hh_b);
Vector2 resolve_circle_circle(Vector2 pos_a, float r_a, Vector2 pos_b, float r_b);
Vector2 resolve_rect_circle(Vector2 rect_pos, float angle, float half_w, float half_h, Vector2 circ_pos, float radius);
Vector2 resolve_circle_rect(Vector2 circ_pos, float radius, Vector2 rect_pos, float angle, float half_w, float half_h);

/* Triangle resolvers */
Vector2 resolve_tri_tri(Vector2 pos_a, const Vector2 *verts_a, Vector2 pos_b, const Vector2 *verts_b);
Vector2
resolve_tri_rect(Vector2 tri_pos, const Vector2 *verts, Vector2 rect_pos, float angle, float half_w, float half_h);
Vector2
resolve_rect_tri(Vector2 rect_pos, float angle, float half_w, float half_h, Vector2 tri_pos, const Vector2 *verts);
Vector2 resolve_tri_circle(Vector2 tri_pos, const Vector2 *verts, Vector2 circ_pos, float radius);
Vector2 resolve_circle_tri(Vector2 circ_pos, float radius, Vector2 tri_pos, const Vector2 *verts);

/* Composite shape resolution */
Vector2 prim_world_pos(Vector2 entity_pos, float entity_angle, Vector2 offset);
Vector2 resolve_prim_pair(const CollisionPrimitive *prim_a,
                          Vector2 world_pos_a,
                          float angle_a,
                          const CollisionPrimitive *prim_b,
                          Vector2 world_pos_b,
                          float angle_b);
Vector2 resolve_composite(const CollisionShape *shape_a,
                          Vector2 pos_a,
                          float angle_a,
                          const CollisionShape *shape_b,
                          Vector2 pos_b,
                          float angle_b);
bool composite_overlap(const CollisionShape *shape_a,
                       Vector2 pos_a,
                       float angle_a,
                       const CollisionShape *shape_b,
                       Vector2 pos_b,
                       float angle_b);
Vector2 resolve_composite_wall(const CollisionShape *shape, Vector2 pos, float angle, Rectangle wall);
void resolve_arena_composite(const CollisionShape *shape, Vector2 *pos, float angle, RectU32 arena);

/* Utility functions used by resolvers */
void obb_corners(Vector2 center, float angle_deg, float half_w, float half_h, Vector2 *out);

void project_corners(const Vector2 *corners, int count, Vector2 axis, float *out_min, float *out_max);

#endif

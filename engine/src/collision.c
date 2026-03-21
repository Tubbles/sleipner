#include "collision.h"
#include "rect.h"
#include "vec.h"

#include "raylib.h"

VEC_IMPL(collision_prim, CollisionPrimitive)

#include <float.h>
#include <math.h>

#define ARENA_PAD 20.0F
#define ARENA_ROUNDNESS 0.05F
#define FLOAT_EPSILON 0.001F
#define WALL_THICKNESS 200.0F
#define TRI_EDGE_AXES 3
#define TRI_TRI_AXES 6
#define TRI_RECT_AXES 5

void project_corners(const Vector2 *corners, int count, Vector2 axis, float *out_min, float *out_max)
{
    *out_min = (corners[0].x * axis.x) + (corners[0].y * axis.y);
    *out_max = *out_min;
    for (int index = 1; index < count; index++) {
        float projection = (corners[index].x * axis.x) + (corners[index].y * axis.y);
        if (projection < *out_min) {
            *out_min = projection;
        }
        if (projection > *out_max) {
            *out_max = projection;
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void obb_corners(Vector2 center, float angle_deg, float half_w, float half_h, Vector2 *out)
{
    float radians = angle_deg * DEG2RAD;
    float cos_val = cosf(radians);
    float sin_val = sinf(radians);
    Vector2 axis_x = {cos_val, sin_val};
    Vector2 axis_y = {-sin_val, cos_val};
    out[0] = (Vector2){center.x - (half_w * axis_x.x) - (half_h * axis_y.x),
                       center.y - (half_w * axis_x.y) - (half_h * axis_y.y)};
    out[1] = (Vector2){center.x + (half_w * axis_x.x) - (half_h * axis_y.x),
                       center.y + (half_w * axis_x.y) - (half_h * axis_y.y)};
    out[2] = (Vector2){center.x + (half_w * axis_x.x) + (half_h * axis_y.x),
                       center.y + (half_w * axis_x.y) + (half_h * axis_y.y)};
    out[3] = (Vector2){center.x - (half_w * axis_x.x) + (half_h * axis_y.x),
                       center.y - (half_w * axis_x.y) + (half_h * axis_y.y)};
}

/* --- Pairwise primitive collision resolvers --- */

static Vector2 sat_resolve_corners(
    const Vector2 *corners_a, int count_a, const Vector2 *corners_b, int count_b, const Vector2 *axes, int num_axes)
{
    float min_overlap = FLT_MAX;
    Vector2 push_axis = {0};

    for (int index = 0; index < num_axes; index++) {
        float a_min;
        float a_max;
        float b_min;
        float b_max;
        project_corners(corners_a, count_a, axes[index], &a_min, &a_max);
        project_corners(corners_b, count_b, axes[index], &b_min, &b_max);

        float overlap = fminf(a_max - b_min, b_max - a_min);
        if (overlap <= 0) {
            return (Vector2){0, 0};
        }

        if (overlap < min_overlap) {
            min_overlap = overlap;
            float a_mid = (a_min + a_max) * 0.5F;
            float b_mid = (b_min + b_max) * 0.5F;
            float sign = (a_mid > b_mid) ? 1.0F : -1.0F;
            push_axis = (Vector2){axes[index].x * sign, axes[index].y * sign};
        }
    }

    return (Vector2){push_axis.x * min_overlap, push_axis.y * min_overlap};
}

Vector2 resolve_rect_rect(
    Vector2 pos_a, float angle_a, float hw_a, float hh_a, Vector2 pos_b, float angle_b, float hw_b, float hh_b)
{
    Vector2 corners_a[4];
    Vector2 corners_b[4];
    obb_corners(pos_a, angle_a, hw_a, hh_a, corners_a);
    obb_corners(pos_b, angle_b, hw_b, hh_b, corners_b);

    float rad_a = angle_a * DEG2RAD;
    float rad_b = angle_b * DEG2RAD;
    Vector2 axes[4] = {
        {cosf(rad_a), sinf(rad_a)},
        {-sinf(rad_a), cosf(rad_a)},
        {cosf(rad_b), sinf(rad_b)},
        {-sinf(rad_b), cosf(rad_b)},
    };

    return sat_resolve_corners(corners_a, 4, corners_b, 4, axes, 4);
}

Vector2 resolve_circle_circle(Vector2 pos_a, float r_a, Vector2 pos_b, float r_b)
{
    float delta_x = pos_a.x - pos_b.x;
    float delta_y = pos_a.y - pos_b.y;
    float dist = sqrtf((delta_x * delta_x) + (delta_y * delta_y));
    float sum_radii = r_a + r_b;

    if (dist >= sum_radii || dist < FLOAT_EPSILON) {
        return (Vector2){0, 0};
    }

    float overlap = sum_radii - dist;
    float normal_x = delta_x / dist;
    float normal_y = delta_y / dist;
    return (Vector2){normal_x * overlap, normal_y * overlap};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Vector2 resolve_rect_circle(Vector2 rect_pos, float angle, float half_w, float half_h, Vector2 circ_pos, float radius)
{
    /* Transform circle center into OBB local space */
    float radians = angle * DEG2RAD;
    float cos_val = cosf(radians);
    float sin_val = sinf(radians);
    float delta_x = circ_pos.x - rect_pos.x;
    float delta_y = circ_pos.y - rect_pos.y;
    float local_x = (delta_x * cos_val) + (delta_y * sin_val);
    float local_y = (-delta_x * sin_val) + (delta_y * cos_val);

    /* Clamp to rect extents to find closest point */
    float closest_x = fmaxf(-half_w, fminf(half_w, local_x));
    float closest_y = fmaxf(-half_h, fminf(half_h, local_y));

    float diff_x = local_x - closest_x;
    float diff_y = local_y - closest_y;
    float dist_sq = (diff_x * diff_x) + (diff_y * diff_y);

    if (dist_sq >= radius * radius) {
        return (Vector2){0, 0};
    }

    float dist = sqrtf(dist_sq);
    Vector2 push;

    if (dist < FLOAT_EPSILON) {
        /* Circle center is inside rect — push along shortest axis */
        float push_x = half_w - fabsf(local_x);
        float push_y = half_h - fabsf(local_y);
        float local_push_x;
        float local_push_y;
        if (push_x < push_y) {
            local_push_x = (local_x >= 0 ? 1.0F : -1.0F) * (push_x + radius);
            local_push_y = 0;
        } else {
            local_push_x = 0;
            local_push_y = (local_y >= 0 ? 1.0F : -1.0F) * (push_y + radius);
        }
        /* Push is for the rect (push rect away from circle) = negate circle push */
        push = (Vector2){-((local_push_x * cos_val) - (local_push_y * sin_val)),
                         -((local_push_x * sin_val) + (local_push_y * cos_val))};
    } else {
        float overlap = radius - dist;
        float normal_x = diff_x / dist;
        float normal_y = diff_y / dist;
        /* Push in local space points from rect toward circle, negate for rect push */
        float local_push_x = -normal_x * overlap;
        float local_push_y = -normal_y * overlap;
        push = (Vector2){(local_push_x * cos_val) - (local_push_y * sin_val),
                         (local_push_x * sin_val) + (local_push_y * cos_val)};
    }

    return push;
}

Vector2 resolve_circle_rect(Vector2 circ_pos, float radius, Vector2 rect_pos, float angle, float half_w, float half_h)
{
    Vector2 result = resolve_rect_circle(rect_pos, angle, half_w, half_h, circ_pos, radius);
    return (Vector2){-result.x, -result.y};
}

/* --- Triangle collision resolvers --- */

static void tri_world_verts(Vector2 pos, const Vector2 *local_verts, Vector2 *out)
{
    for (int index = 0; index < 3; index++) {
        out[index] = (Vector2){pos.x + local_verts[index].x, pos.y + local_verts[index].y};
    }
}

static void tri_edge_normals(const Vector2 *world_verts, Vector2 *axes)
{
    for (int index = 0; index < 3; index++) {
        int next = (index + 1) % 3;
        float edge_x = world_verts[next].x - world_verts[index].x;
        float edge_y = world_verts[next].y - world_verts[index].y;
        float length = sqrtf((edge_x * edge_x) + (edge_y * edge_y));
        if (length < FLOAT_EPSILON) {
            axes[index] = (Vector2){1, 0};
        } else {
            axes[index] = (Vector2){-edge_y / length, edge_x / length};
        }
    }
}

Vector2 resolve_tri_tri(Vector2 pos_a, const Vector2 *verts_a, Vector2 pos_b, const Vector2 *verts_b)
{
    Vector2 world_a[3];
    Vector2 world_b[3];
    tri_world_verts(pos_a, verts_a, world_a);
    tri_world_verts(pos_b, verts_b, world_b);

    Vector2 axes[TRI_TRI_AXES];
    tri_edge_normals(world_a, axes);
    tri_edge_normals(world_b, axes + TRI_EDGE_AXES);

    return sat_resolve_corners(world_a, 3, world_b, 3, axes, TRI_TRI_AXES);
}

Vector2
resolve_tri_rect(Vector2 tri_pos, const Vector2 *verts, Vector2 rect_pos, float angle, float half_w, float half_h)
{
    Vector2 world_tri[3];
    tri_world_verts(tri_pos, verts, world_tri);

    Vector2 rect_corners[4];
    obb_corners(rect_pos, angle, half_w, half_h, rect_corners);

    float radians = angle * DEG2RAD;
    Vector2 axes[TRI_RECT_AXES];
    tri_edge_normals(world_tri, axes);
    axes[TRI_EDGE_AXES] = (Vector2){cosf(radians), sinf(radians)};
    axes[TRI_EDGE_AXES + 1] = (Vector2){-sinf(radians), cosf(radians)};

    return sat_resolve_corners(world_tri, 3, rect_corners, 4, axes, TRI_RECT_AXES);
}

Vector2
resolve_rect_tri(Vector2 rect_pos, float angle, float half_w, float half_h, Vector2 tri_pos, const Vector2 *verts)
{
    Vector2 result = resolve_tri_rect(tri_pos, verts, rect_pos, angle, half_w, half_h);
    return (Vector2){-result.x, -result.y};
}

Vector2 resolve_tri_circle(Vector2 tri_pos, const Vector2 *verts, Vector2 circ_pos, float radius)
{
    Vector2 world_tri[3];
    tri_world_verts(tri_pos, verts, world_tri);

    /* Test triangle edge normals + axis from triangle vertex closest to circle */
    Vector2 axes[4];
    tri_edge_normals(world_tri, axes);

    /* Find closest vertex to circle center for the Voronoi axis */
    float best_dist = FLT_MAX;
    int best_vertex = 0;
    for (int index = 0; index < 3; index++) {
        float delta_x = circ_pos.x - world_tri[index].x;
        float delta_y = circ_pos.y - world_tri[index].y;
        float dist_sq = (delta_x * delta_x) + (delta_y * delta_y);
        if (dist_sq < best_dist) {
            best_dist = dist_sq;
            best_vertex = index;
        }
    }
    float delta_x = circ_pos.x - world_tri[best_vertex].x;
    float delta_y = circ_pos.y - world_tri[best_vertex].y;
    float length = sqrtf((delta_x * delta_x) + (delta_y * delta_y));
    if (length < FLOAT_EPSILON) {
        axes[3] = (Vector2){1, 0};
    } else {
        axes[3] = (Vector2){delta_x / length, delta_y / length};
    }

    /* Project circle as an interval on each axis */
    float min_overlap = FLT_MAX;
    Vector2 push_axis = {0};

    for (int index = 0; index < 4; index++) {
        float tri_min;
        float tri_max;
        project_corners(world_tri, 3, axes[index], &tri_min, &tri_max);

        float circ_proj = (circ_pos.x * axes[index].x) + (circ_pos.y * axes[index].y);
        float circ_min = circ_proj - radius;
        float circ_max = circ_proj + radius;

        float overlap = fminf(tri_max - circ_min, circ_max - tri_min);
        if (overlap <= 0) {
            return (Vector2){0, 0};
        }

        if (overlap < min_overlap) {
            min_overlap = overlap;
            float tri_mid = (tri_min + tri_max) * 0.5F;
            float sign = (tri_mid > circ_proj) ? 1.0F : -1.0F;
            push_axis = (Vector2){axes[index].x * sign, axes[index].y * sign};
        }
    }

    return (Vector2){push_axis.x * min_overlap, push_axis.y * min_overlap};
}

Vector2 resolve_circle_tri(Vector2 circ_pos, float radius, Vector2 tri_pos, const Vector2 *verts)
{
    Vector2 result = resolve_tri_circle(tri_pos, verts, circ_pos, radius);
    return (Vector2){-result.x, -result.y};
}

/* --- Composite shape resolution --- */

Vector2 prim_world_pos(Vector2 entity_pos, float entity_angle, Vector2 offset)
{
    if (offset.x == 0 && offset.y == 0) {
        return entity_pos;
    }

    float radians = entity_angle * DEG2RAD;
    float cos_val = cosf(radians);
    float sin_val = sinf(radians);
    return (Vector2){
        entity_pos.x + (offset.x * cos_val) - (offset.y * sin_val),
        entity_pos.y + (offset.x * sin_val) + (offset.y * cos_val),
    };
}

Vector2 resolve_prim_pair(const CollisionPrimitive *prim_a,
                          Vector2 world_pos_a,
                          float angle_a,
                          const CollisionPrimitive *prim_b,
                          Vector2 world_pos_b,
                          float angle_b)
{
    float resolved_angle_a = angle_a + prim_a->angle_offset;
    float resolved_angle_b = angle_b + prim_b->angle_offset;

    if (prim_a->kind == COLLIDER_RECT && prim_b->kind == COLLIDER_RECT) {
        return resolve_rect_rect(world_pos_a, resolved_angle_a, prim_a->rect.half_w, prim_a->rect.half_h, world_pos_b,
                                 resolved_angle_b, prim_b->rect.half_w, prim_b->rect.half_h);
    }

    if (prim_a->kind == COLLIDER_CIRCLE && prim_b->kind == COLLIDER_CIRCLE) {
        return resolve_circle_circle(world_pos_a, prim_a->circle.radius, world_pos_b, prim_b->circle.radius);
    }

    if (prim_a->kind == COLLIDER_RECT && prim_b->kind == COLLIDER_CIRCLE) {
        return resolve_rect_circle(world_pos_a, resolved_angle_a, prim_a->rect.half_w, prim_a->rect.half_h, world_pos_b,
                                   prim_b->circle.radius);
    }

    if (prim_a->kind == COLLIDER_CIRCLE && prim_b->kind == COLLIDER_RECT) {
        return resolve_circle_rect(world_pos_a, prim_a->circle.radius, world_pos_b, resolved_angle_b,
                                   prim_b->rect.half_w, prim_b->rect.half_h);
    }

    if (prim_a->kind == COLLIDER_TRIANGLE && prim_b->kind == COLLIDER_TRIANGLE) {
        return resolve_tri_tri(world_pos_a, prim_a->triangle.verts, world_pos_b, prim_b->triangle.verts);
    }

    if (prim_a->kind == COLLIDER_TRIANGLE && prim_b->kind == COLLIDER_RECT) {
        return resolve_tri_rect(world_pos_a, prim_a->triangle.verts, world_pos_b, resolved_angle_b, prim_b->rect.half_w,
                                prim_b->rect.half_h);
    }

    if (prim_a->kind == COLLIDER_RECT && prim_b->kind == COLLIDER_TRIANGLE) {
        return resolve_rect_tri(world_pos_a, resolved_angle_a, prim_a->rect.half_w, prim_a->rect.half_h, world_pos_b,
                                prim_b->triangle.verts);
    }

    if (prim_a->kind == COLLIDER_TRIANGLE && prim_b->kind == COLLIDER_CIRCLE) {
        return resolve_tri_circle(world_pos_a, prim_a->triangle.verts, world_pos_b, prim_b->circle.radius);
    }

    if (prim_a->kind == COLLIDER_CIRCLE && prim_b->kind == COLLIDER_TRIANGLE) {
        return resolve_circle_tri(world_pos_a, prim_a->circle.radius, world_pos_b, prim_b->triangle.verts);
    }

    return (Vector2){0, 0};
}

Vector2 resolve_composite(const CollisionShape *shape_a,
                          Vector2 pos_a,
                          float angle_a,
                          const CollisionShape *shape_b,
                          Vector2 pos_b,
                          float angle_b)
{
    Vector2 best = {0, 0};
    float best_mag = 0;

    for (int outer = 0; outer < shape_a->prims.count; outer++) {
        Vector2 world_pos_a = prim_world_pos(pos_a, angle_a, shape_a->prims.data[outer].offset);
        for (int inner = 0; inner < shape_b->prims.count; inner++) {
            Vector2 world_pos_b = prim_world_pos(pos_b, angle_b, shape_b->prims.data[inner].offset);
            Vector2 push = resolve_prim_pair(&shape_a->prims.data[outer], world_pos_a, angle_a,
                                             &shape_b->prims.data[inner], world_pos_b, angle_b);
            float magnitude = (push.x * push.x) + (push.y * push.y);
            if (magnitude > best_mag) {
                best_mag = magnitude;
                best = push;
            }
        }
    }
    return best;
}

bool composite_overlap(const CollisionShape *shape_a,
                       Vector2 pos_a,
                       float angle_a,
                       const CollisionShape *shape_b,
                       Vector2 pos_b,
                       float angle_b)
{
    Vector2 result = resolve_composite(shape_a, pos_a, angle_a, shape_b, pos_b, angle_b);
    return (bool)((result.x != 0.0F) || (result.y != 0.0F));
}

Vector2 resolve_composite_wall(const CollisionShape *shape, Vector2 pos, float angle, Rectangle wall)
{
    CollisionShape wall_shape = {0};
    CollisionPrimitive wall_prim = {
        .kind = COLLIDER_RECT,
        .offset = {0, 0},
        .angle_offset = 0,
    };
    wall_prim.rect.half_w = wall.width / 2;
    wall_prim.rect.half_h = wall.height / 2;
    (void)vec_collision_prim_push(&wall_shape.prims, wall_prim);

    Vector2 wall_center = {wall.x + (wall.width / 2), wall.y + (wall.height / 2)};
    Vector2 result = resolve_composite(shape, pos, angle, &wall_shape, wall_center, 0);
    vec_collision_prim_free(&wall_shape.prims);
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool is_in_corner_quadrant(int corner, float delta_x, float delta_y)
{
    switch (corner) {
    case 0:
        return (bool)((delta_x < 0) && (delta_y < 0));
    case 1:
        return (bool)((delta_x > 0) && (delta_y < 0));
    case 2:
        return (bool)((delta_x < 0) && (delta_y > 0));
    case 3:
        return (bool)((delta_x > 0) && (delta_y > 0));
    default:
        return false;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void push_point_out_of_arc(Vector2 *pos, Vector2 center, Vector2 point, float radius, int corner)
{
    float delta_x = point.x - center.x;
    float delta_y = point.y - center.y;
    if (!is_in_corner_quadrant(corner, delta_x, delta_y)) {
        return;
    }

    float dist = sqrtf((delta_x * delta_x) + (delta_y * delta_y));
    if (dist > radius && dist > FLOAT_EPSILON) {
        float push = dist - radius;
        pos->x -= (delta_x / dist) * push;
        pos->y -= (delta_y / dist) * push;
    }
}

static void resolve_arena_corner_rect(const CollisionPrimitive *prim,
                                      Vector2 world_pos,
                                      float angle,
                                      Vector2 *pos,
                                      Vector2 center,
                                      float radius,
                                      int corner)
{
    float prim_angle = angle + prim->angle_offset;
    Vector2 corners[4];
    obb_corners(world_pos, prim_angle, prim->rect.half_w, prim->rect.half_h, corners);
    for (int index = 0; index < 4; index++) {
        push_point_out_of_arc(pos, center, corners[index], radius, corner);
    }
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void resolve_arena_corner_circle(
    const CollisionPrimitive *prim, Vector2 world_pos, Vector2 *pos, Vector2 center, float radius, int corner)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    float effective_radius = radius - prim->circle.radius;
    push_point_out_of_arc(pos, center, world_pos, effective_radius, corner);
}

static void resolve_arena_corner_tri(
    const CollisionPrimitive *prim, Vector2 world_pos, Vector2 *pos, Vector2 center, float radius, int corner)
{
    Vector2 world_verts[3];
    tri_world_verts(world_pos, prim->triangle.verts, world_verts);
    for (int index = 0; index < 3; index++) {
        push_point_out_of_arc(pos, center, world_verts[index], radius, corner);
    }
}

static void resolve_arena_edges(const CollisionShape *shape, Vector2 *pos, float angle, RectU32 arena)
{
    float width = (float)arena.width;
    float height = (float)arena.height;
    Rectangle edge_walls[4] = {
        {ARENA_PAD - WALL_THICKNESS, ARENA_PAD, WALL_THICKNESS, height - (2 * ARENA_PAD)},
        {width - ARENA_PAD, ARENA_PAD, WALL_THICKNESS, height - (2 * ARENA_PAD)},
        {ARENA_PAD, ARENA_PAD - WALL_THICKNESS, width - (2 * ARENA_PAD), WALL_THICKNESS},
        {ARENA_PAD, height - ARENA_PAD, width - (2 * ARENA_PAD), WALL_THICKNESS},
    };
    for (int index = 0; index < 4; index++) {
        Vector2 push = resolve_composite_wall(shape, *pos, angle, edge_walls[index]);
        pos->x += push.x;
        pos->y += push.y;
    }
}

static void
resolve_arena_corners(const CollisionShape *shape, Vector2 *pos, float angle, RectU32 arena, float corner_radius)
{
    float width = (float)arena.width;
    float height = (float)arena.height;
    Vector2 corner_centers[4] = {
        {ARENA_PAD + corner_radius, ARENA_PAD + corner_radius},
        {width - ARENA_PAD - corner_radius, ARENA_PAD + corner_radius},
        {ARENA_PAD + corner_radius, height - ARENA_PAD - corner_radius},
        {width - ARENA_PAD - corner_radius, height - ARENA_PAD - corner_radius},
    };
    for (int corner_index = 0; corner_index < 4; corner_index++) {
        for (int prim_index = 0; prim_index < shape->prims.count; prim_index++) {
            Vector2 world_pos = prim_world_pos(*pos, angle, shape->prims.data[prim_index].offset);
            const CollisionPrimitive *prim = &shape->prims.data[prim_index];

            if (prim->kind == COLLIDER_RECT) {
                resolve_arena_corner_rect(prim, world_pos, angle, pos, corner_centers[corner_index], corner_radius,
                                          corner_index);
            } else if (prim->kind == COLLIDER_CIRCLE) {
                resolve_arena_corner_circle(prim, world_pos, pos, corner_centers[corner_index], corner_radius,
                                            corner_index);
            } else if (prim->kind == COLLIDER_TRIANGLE) {
                resolve_arena_corner_tri(prim, world_pos, pos, corner_centers[corner_index], corner_radius,
                                         corner_index);
            }
        }
    }
}

void resolve_arena_composite(const CollisionShape *shape, Vector2 *pos, float angle, RectU32 arena)
{
    float arena_width = (float)arena.width - (2 * ARENA_PAD);
    float arena_height = (float)arena.height - (2 * ARENA_PAD);
    float corner_radius = fminf(arena_width, arena_height) * ARENA_ROUNDNESS * 0.5F;

    resolve_arena_edges(shape, pos, angle, arena);
    resolve_arena_corners(shape, pos, angle, arena, corner_radius);
}

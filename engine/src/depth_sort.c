#include "depth_sort.h"

#include "alloc.h"
#include "entity.h"

#include <stdlib.h>

typedef struct {
    int index;
    float sort_y;
    float sort_x;
} DrawOrder;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by qsort
static int compare_draw_order(const void *left, const void *right)
{
    const DrawOrder *lhs = (const DrawOrder *)left;
    const DrawOrder *rhs = (const DrawOrder *)right;
    if (lhs->sort_y < rhs->sort_y) {
        return -1;
    }
    if (lhs->sort_y > rhs->sort_y) {
        return 1;
    }
    if (lhs->sort_x < rhs->sort_x) {
        return -1;
    }
    if (lhs->sort_x > rhs->sort_x) {
        return 1;
    }
    return (lhs->index > rhs->index) - (lhs->index < rhs->index);
}

int *sort_entities_by_depth(const Entity *entities, int count, Allocator *alloc)
{
    DrawOrder *order = alloc->malloc_fn(alloc->ctx, (size_t)count * sizeof(DrawOrder));

    for (int index = 0; index < count; index++) {
        const Entity *entity = &entities[index];
        float sort_y =
            entity->collision_size.y > 0.0F ? entity->collision.y + entity->collision.height : entity->position.y;
        order[index] = (DrawOrder){
            .index = index,
            .sort_y = sort_y,
            .sort_x = entity->position.x,
        };
    }

    qsort(order, (size_t)count, sizeof(DrawOrder), compare_draw_order);

    int *sorted = alloc->malloc_fn(alloc->ctx, (size_t)count * sizeof(int));
    for (int index = 0; index < count; index++) {
        sorted[index] = order[index].index;
    }
    alloc->free_fn(alloc->ctx, order);
    return sorted;
}

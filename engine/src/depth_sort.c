#include "depth_sort.h"

#include "alloc.h"
#include "entity.h"

#include <stdlib.h>

typedef struct {
    int index;
    float sort_y;
} DrawOrder;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by qsort
static int compare_draw_order(const void *left, const void *right)
{
    float left_y = ((const DrawOrder *)left)->sort_y;
    float right_y = ((const DrawOrder *)right)->sort_y;
    if (left_y < right_y) {
        return -1;
    }
    if (left_y > right_y) {
        return 1;
    }
    return 0;
}

int *sort_entities_by_depth(const Entity *entities, int count, Allocator *alloc)
{
    DrawOrder *order = alloc->malloc_fn(alloc->ctx, (size_t)count * sizeof(DrawOrder));

    for (int index = 0; index < count; index++) {
        order[index] = (DrawOrder){
            .index = index,
            .sort_y = entities[index].collision.y + entities[index].collision.height,
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

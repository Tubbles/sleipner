#pragma once

#include "alloc.h"
#include "entity.h"

/* Return an allocator-backed array of entity indices sorted by visual
 * depth (collision bottom edge, ascending Y). Caller must ensure the
 * backing allocator outlives the returned pointer. */
[[nodiscard]] int *sort_entities_by_depth(const Entity *entities, int count, Allocator *alloc);

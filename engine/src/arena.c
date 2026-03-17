#include "arena.h"

#include <stdlib.h>
#include <string.h>

bool arena_init(Arena *arena, size_t capacity)
{
    arena->buffer = malloc(capacity);
    if (!arena->buffer) {
        arena->capacity = 0;
        arena->offset = 0;
        return false;
    }
    arena->capacity = capacity;
    arena->offset = 0;
    return true;
}

void *arena_alloc(Arena *arena, AllocRequest request)
{
    size_t aligned_offset = (arena->offset + request.alignment - 1) & ~(request.alignment - 1);
    if (aligned_offset + request.size > arena->capacity) {
        return NULL;
    }
    void *pointer = arena->buffer + aligned_offset;
    arena->offset = aligned_offset + request.size;
    return pointer;
}

void arena_reset(Arena *arena)
{
    arena->offset = 0;
}

void arena_free(Arena *arena)
{
    free(arena->buffer);
    arena->buffer = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

size_t arena_used(Arena *arena)
{
    return arena->offset;
}

size_t arena_remaining(Arena *arena)
{
    return arena->capacity - arena->offset;
}

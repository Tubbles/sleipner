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

void *arena_alloc(Arena *arena, size_t size, size_t alignment)
{
    size_t aligned_offset = (arena->offset + alignment - 1) & ~(alignment - 1);
    if (aligned_offset + size > arena->capacity) {
        return NULL;
    }
    void *pointer = arena->buffer + aligned_offset;
    arena->offset = aligned_offset + size;
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

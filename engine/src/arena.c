#include "arena.h"
#include "error.h"

#include <stdlib.h>
#include <string.h>

bool arena_init(struct EngineContext *ctx, Arena *arena, size_t capacity)
{
    arena->buffer = malloc(capacity);
    if (!arena->buffer) {
        error_set(ctx, "malloc(%zu) failed", capacity);
        arena->capacity = 0;
        arena->offset = 0;
        return false;
    }
    arena->capacity = capacity;
    arena->offset = 0;
    return true;
}

void *arena_alloc(struct EngineContext *ctx, Arena *arena, AllocRequest request)
{
    size_t aligned_offset = (arena->offset + request.alignment - 1) & ~(request.alignment - 1);
    if (aligned_offset + request.size > arena->capacity) {
        error_set(ctx, "arena full: need %zu bytes, %zu remaining", request.size, arena->capacity - aligned_offset);
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

ArenaCheckpoint arena_save(const Arena *arena)
{
    return arena->offset;
}

void arena_restore(Arena *arena, ArenaCheckpoint checkpoint)
{
    arena->offset = checkpoint;
}

char *arena_strdup(struct EngineContext *ctx, Arena *arena, const char *cstr)
{
    if (!cstr) {
        return NULL;
    }
    size_t length = strlen(cstr);
    char *dest = arena_alloc(ctx, arena, (AllocRequest){.size = length + 1, .alignment = 1});
    if (!dest) {
        return NULL;
    }
    memcpy(dest, cstr, length + 1);
    return dest;
}

void *arena_memdup(struct EngineContext *ctx, Arena *arena, const void *src, size_t size)
{
    if (!src) {
        return NULL;
    }
    void *dest = arena_alloc(ctx, arena, (AllocRequest){.size = size, .alignment = 1});
    if (!dest) {
        return NULL;
    }
    memcpy(dest, src, size);
    return dest;
}

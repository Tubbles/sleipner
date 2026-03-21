#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>

struct EngineContext;
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} Arena;

typedef struct {
    size_t size;
    size_t alignment;
} AllocRequest;

/* Create an arena with the given capacity. Returns false on allocation failure. */
[[nodiscard]] bool arena_init(struct EngineContext *ctx, Arena *arena, size_t capacity);

/* Allocate from the arena per the given request.
 * Returns NULL if the arena is full. */
[[nodiscard]] void *arena_alloc(struct EngineContext *ctx, Arena *arena, AllocRequest request);

/* Reset the arena — all previous allocations become invalid. */
void arena_reset(Arena *arena);

/* Free the arena's backing buffer. */
void arena_free(Arena *arena);

/* Returns the number of bytes currently used. */
size_t arena_used(Arena *arena);

/* Returns the number of bytes remaining. */
size_t arena_remaining(Arena *arena);

#endif

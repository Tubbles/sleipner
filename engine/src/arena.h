#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct EngineContext;

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} Arena;

typedef struct {
    size_t size;
    size_t alignment;
} AllocRequest;

/* Opaque checkpoint — stores an arena offset for later restore. */
typedef size_t ArenaCheckpoint;

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

/* Save the current arena offset for later restore. */
ArenaCheckpoint arena_save(const Arena *arena);

/* Rewind the arena to a previously saved checkpoint.
 * All allocations made after the checkpoint become invalid. */
void arena_restore(Arena *arena, ArenaCheckpoint checkpoint);

/* Duplicate a null-terminated string into the arena.
 * Returns NULL if cstr is NULL or the arena is full. */
[[nodiscard]] char *arena_strdup(struct EngineContext *ctx, Arena *arena, const char *cstr);

/* Copy size bytes from src into the arena.
 * Returns NULL if src is NULL or the arena is full. */
[[nodiscard]] void *arena_memdup(struct EngineContext *ctx, Arena *arena, const void *src, size_t size);

#endif

#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct EngineContext;

typedef struct Arena {
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
size_t arena_used(const Arena *arena);

/* Returns the number of bytes remaining. */
size_t arena_remaining(const Arena *arena);

/* Save the current arena offset for later restore. */
ArenaCheckpoint arena_save(const Arena *arena);

/* Rewind the arena to a previously saved checkpoint.
 * All allocations made after the checkpoint become invalid. */
void arena_restore(Arena *arena, ArenaCheckpoint checkpoint);

/* Reallocate an existing arena allocation.
 * If old_ptr is NULL, behaves like arena_alloc.
 * If request.size <= old_size, returns old_ptr unchanged.
 * If old_ptr is at the top of the arena, extends in-place.
 * Otherwise, allocates new space, copies, and returns new pointer (old space leaked).
 * Returns NULL on allocation failure. */
[[nodiscard]] void *
arena_realloc(struct EngineContext *ctx, Arena *arena, void *old_ptr, size_t old_size, AllocRequest request);

/* Convenience wrapper: allocate `size` bytes with alignment 1.
 * The alloc_size attribute lets the compiler and static analyzer know the returned
 * region is `size` bytes, enabling bounds-checking diagnostics. */
[[nodiscard]] __attribute__((alloc_size(3))) static inline void *
arena_alloc_n(struct EngineContext *ctx, Arena *arena, size_t size)
{
    return arena_alloc(ctx, arena, (AllocRequest){.size = size, .alignment = 1});
}

#endif

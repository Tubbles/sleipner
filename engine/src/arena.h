#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct EngineContext;

/* 1 TiB virtual reservation — physical pages are demand-paged by the OS. */
#define ARENA_VIRTUAL_SIZE (1ULL << 40)

typedef struct Arena {
    uint8_t *buffer;
    size_t offset;
} Arena;

typedef struct {
    size_t size;
    size_t alignment;
} AllocRequest;

/* Opaque checkpoint — stores an arena offset for later restore. */
typedef size_t ArenaCheckpoint;

/* Save the current arena offset for later restore. */
ArenaCheckpoint arena_save(const Arena *arena);

/* Rewind the arena to a previously saved checkpoint.
 * All allocations made after the checkpoint become invalid. */
void arena_restore(Arena *arena, ArenaCheckpoint checkpoint);

/* Scope guard for scratch_arena: saves on entry, restores on block exit. */
typedef struct {
    Arena *arena;
    ArenaCheckpoint cp;
} ArenaScope;

static inline void arena_scope_pop(ArenaScope *scope)
{
    arena_restore(scope->arena, scope->cp);
}

/* Open a scratch scope in the enclosing block.  arena_restore fires automatically
 * on any exit (return, break, goto, or fall-through) via __attribute__((cleanup)). */
#define SCRATCH_SCOPE(arena_ptr)                                                                                       \
    __attribute__((cleanup(arena_scope_pop))) ArenaScope _scratch_scope_##__COUNTER__ = {(arena_ptr),                  \
                                                                                         arena_save(arena_ptr)}

/* Reserve virtual address space for an arena. Returns false on mmap failure. */
[[nodiscard]] bool arena_init(struct EngineContext *ctx, Arena *arena);

/* Allocate from the arena per the given request. Never returns NULL. */
[[nodiscard]] void *arena_alloc(struct EngineContext *ctx, Arena *arena, AllocRequest request);

/* Reset the arena — all previous allocations become invalid.
 * Releases physical pages back to the OS via MADV_DONTNEED. */
void arena_reset(Arena *arena);

/* Unmap the arena's backing reservation. */
void arena_free(Arena *arena);

/* Returns the number of bytes currently used. */
size_t arena_used(const Arena *arena);

/* Reallocate an existing arena allocation.
 * If old_ptr is NULL, behaves like arena_alloc.
 * If request.size <= old_size, returns old_ptr unchanged.
 * If old_ptr is at the top of the arena, extends in-place.
 * Otherwise, allocates new space, copies, and returns new pointer (old space leaked).
 * Never returns NULL. */
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

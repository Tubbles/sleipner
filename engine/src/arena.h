#pragma once

#include "alloc.h"
#include "error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 1 TiB virtual reservation — physical pages are demand-paged by the OS. */
#define ARENA_VIRTUAL_SIZE (1ULL << 40)

typedef struct Arena {
    uint8_t *buffer;
    size_t offset;
    uint8_t *last_alloc;
#ifdef _WIN32
    size_t committed; /* bytes committed via VirtualAlloc MEM_COMMIT */
#endif
} Arena;

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
[[nodiscard]] bool arena_init(ErrorState *err, Arena *arena);

/* Allocate from the arena. Data is aligned to _Alignof(max_align_t).
 * A size_t header is stored before the returned pointer for arena_realloc.
 * Never returns nullptr. */
[[nodiscard]] __attribute__((alloc_size(2))) void *arena_alloc(Arena *arena, size_t size);

/* Reset the arena — all previous allocations become invalid.
 * Releases physical pages back to the OS via MADV_DONTNEED. */
void arena_reset(Arena *arena);

/* Unmap the arena's backing reservation. */
void arena_free(Arena *arena);

/* Returns the number of bytes currently used. */
size_t arena_used(const Arena *arena);

/* Returns the number of bytes allocated since the given checkpoint. */
size_t arena_used_since(const Arena *arena, ArenaCheckpoint checkpoint);

/* Returns a pointer to the arena memory at the given checkpoint offset. */
void *arena_ptr_at(const Arena *arena, ArenaCheckpoint checkpoint);

/* Reallocate an existing arena allocation.
 * If ptr is nullptr, behaves like arena_alloc.
 * If new_size <= old size (read from header), returns ptr unchanged.
 * If ptr is the most recent allocation, extends in-place.
 * Otherwise, allocates new space, copies, and returns new pointer (old space leaked).
 * Never returns nullptr. */
[[nodiscard]] void *arena_realloc(Arena *arena, void *ptr, size_t new_size);

/* Construct an arena-backed allocator. free_fn is a no-op; the arena owns all memory. */
Allocator allocator_arena(Arena *arena);

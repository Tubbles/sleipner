#ifndef ALLOC_H
#define ALLOC_H

#include "arena.h"

#include <stddef.h>

struct EngineContext;

/* Generic allocator interface — function pointers to malloc, realloc, and free.
 *
 * realloc_fn receives old_size because arena-backed implementations may need it
 * to determine whether the allocation is at the top and can be extended in-place.
 * alignment is bundled in AllocRequest; heap implementations may ignore it.
 *
 * Two built-in implementations:
 *   allocator_heap()        — wraps libc malloc/realloc/free
 *   allocator_arena(ctx, a) — wraps arena_alloc/arena_realloc; free_fn is a no-op
 *
 * A zero-initialised Allocator is NOT valid; always use one of the constructors.
 * Allocator values can be freely copied — they hold no owned resources. */
typedef struct Allocator {
    struct EngineContext *ctx;
    struct Arena *arena;
    void *(*malloc_fn)(struct EngineContext *ctx, struct Arena *arena, AllocRequest request);
    void *(*realloc_fn)(
        struct EngineContext *ctx, struct Arena *arena, void *ptr, size_t old_size, AllocRequest request);
    void (*free_fn)(struct EngineContext *ctx, struct Arena *arena, void *ptr);
} Allocator;

/* Construct a heap-backed allocator (wraps libc malloc/realloc/free). */
Allocator allocator_heap(void);

/* Construct an arena-backed allocator. free_fn is a no-op; the arena owns all memory. */
Allocator allocator_arena(struct EngineContext *ctx, struct Arena *arena);

#endif

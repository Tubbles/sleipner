#ifndef ALLOC_H
#define ALLOC_H

#include <stddef.h>

/* Generic allocator interface — type-erased function pointers.
 *
 * ctx is opaque: arena allocators store an Arena *, heap allocators use nullptr.
 * Constructors live next to the types they wrap:
 *   allocator_arena(Arena *) — declared in arena.h
 *   allocator_heap()         — declared in test_helpers.h (test-only)
 *
 * A zero-initialised Allocator is NOT valid; always use a constructor.
 * Allocator values can be freely copied — they hold no owned resources. */
typedef struct Allocator {
    void *ctx;
    void *(*malloc_fn)(void *ctx, size_t size);
    void *(*realloc_fn)(void *ctx, void *ptr, size_t new_size);
    void (*free_fn)(void *ctx, void *ptr);
} Allocator;

#endif

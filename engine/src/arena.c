#include "arena.h"
#include "alloc.h"
#include "error.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

bool arena_init(struct EngineContext *ctx, Arena *arena)
{
    arena->buffer =
        mmap(nullptr, ARENA_VIRTUAL_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (arena->buffer == MAP_FAILED) {
        error_set(ctx, "mmap(%zu) failed", (size_t)ARENA_VIRTUAL_SIZE);
        arena->buffer = nullptr;
        arena->offset = 0;
        arena->last_alloc = nullptr;
        return false;
    }
    arena->offset = 0;
    arena->last_alloc = nullptr;
    return true;
}

void *arena_alloc(Arena *arena, size_t size)
{
    size_t align = _Alignof(max_align_t);
    size_t data_offset = (arena->offset + sizeof(size_t) + align - 1) & ~(align - 1);
    *((size_t *)(arena->buffer + data_offset - sizeof(size_t))) = size;
    uint8_t *data = arena->buffer + data_offset;
    arena->offset = data_offset + size;
    arena->last_alloc = data;
    return data;
}

void arena_reset(Arena *arena)
{
    (void)madvise(arena->buffer, arena->offset, MADV_DONTNEED);
    arena->offset = 0;
    arena->last_alloc = nullptr;
}

void arena_free(Arena *arena)
{
    (void)munmap(arena->buffer, ARENA_VIRTUAL_SIZE);
    arena->buffer = nullptr;
    arena->offset = 0;
    arena->last_alloc = nullptr;
}

size_t arena_used(const Arena *arena)
{
    return arena->offset;
}

ArenaCheckpoint arena_save(const Arena *arena)
{
    return arena->offset;
}

void arena_restore(Arena *arena, ArenaCheckpoint checkpoint)
{
    arena->offset = checkpoint;
    arena->last_alloc = nullptr;
}

void *arena_realloc(Arena *arena, void *ptr, size_t new_size)
{
    if (!ptr) {
        return arena_alloc(arena, new_size);
    }
    size_t old_size = *((size_t *)ptr - 1);
    if (new_size <= old_size) {
        return ptr;
    }
    /* Extend in-place if this is the most recent allocation */
    if ((uint8_t *)ptr == arena->last_alloc) {
        *((size_t *)ptr - 1) = new_size;
        arena->offset = (size_t)((uint8_t *)ptr - arena->buffer) + new_size;
        return ptr;
    }
    /* Not the most recent — allocate new space and copy */
    void *new_ptr = arena_alloc(arena, new_size);
    memcpy(new_ptr, ptr, old_size);
    return new_ptr;
}

/* --- Allocator interface wrappers --- */

static void *arena_malloc_fn(void *ctx, size_t size)
{
    return arena_alloc((Arena *)ctx, size);
}

static void *arena_realloc_fn(void *ctx, void *ptr, size_t new_size)
{
    return arena_realloc((Arena *)ctx, ptr, new_size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void arena_free_fn(void *ctx, void *ptr)
{
    (void)ctx;
    (void)ptr;
}

Allocator allocator_arena(Arena *arena)
{
    return (Allocator){
        .ctx = arena,
        .malloc_fn = arena_malloc_fn,
        .realloc_fn = arena_realloc_fn,
        .free_fn = arena_free_fn,
    };
}

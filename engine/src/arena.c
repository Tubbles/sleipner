#include "arena.h"
#include "error.h"

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
        return false;
    }
    arena->offset = 0;
    return true;
}

void *arena_alloc(struct EngineContext *ctx, Arena *arena, AllocRequest request)
{
    (void)ctx;
    size_t aligned_offset = (arena->offset + request.alignment - 1) & ~(request.alignment - 1);
    void *pointer = arena->buffer + aligned_offset;
    arena->offset = aligned_offset + request.size;
    return pointer;
}

void arena_reset(Arena *arena)
{
    (void)madvise(arena->buffer, arena->offset, MADV_DONTNEED);
    arena->offset = 0;
}

void arena_free(Arena *arena)
{
    (void)munmap(arena->buffer, ARENA_VIRTUAL_SIZE);
    arena->buffer = nullptr;
    arena->offset = 0;
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
}

void *arena_realloc(struct EngineContext *ctx, Arena *arena, void *old_ptr, size_t old_size, AllocRequest request)
{
    if (!old_ptr) {
        return arena_alloc(ctx, arena, request);
    }
    if (request.size <= old_size) {
        return old_ptr;
    }
    /* Extend in-place if this allocation is at the top of the arena */
    if ((uint8_t *)old_ptr + old_size == arena->buffer + arena->offset) {
        size_t additional = request.size - old_size;
        arena->offset += additional;
        return old_ptr;
    }
    /* Not at top — allocate new space and copy */
    void *new_ptr = arena_alloc(ctx, arena, request);
    memcpy(new_ptr, old_ptr, old_size);
    return new_ptr;
}

#include "alloc.h"
#include "arena.h"

#include <stdlib.h>

static void *heap_malloc_fn(struct EngineContext *ctx, struct Arena *arena, AllocRequest request)
{
    (void)ctx;
    (void)arena;
    (void)request.alignment;
    return malloc(request.size);
}

static void *
heap_realloc_fn(struct EngineContext *ctx, struct Arena *arena, void *ptr, size_t old_size, AllocRequest request)
{
    (void)ctx;
    (void)arena;
    (void)old_size;
    (void)request.alignment;
    return realloc(ptr, request.size);
}

static void heap_free_fn(struct EngineContext *ctx, struct Arena *arena, void *ptr)
{
    (void)ctx;
    (void)arena;
    free(ptr);
}

static void *arena_malloc_fn(struct EngineContext *ctx, struct Arena *arena, AllocRequest request)
{
    return arena_alloc(ctx, arena, request);
}

static void *
arena_realloc_fn(struct EngineContext *ctx, struct Arena *arena, void *ptr, size_t old_size, AllocRequest request)
{
    return arena_realloc(ctx, arena, ptr, old_size, request);
}

static void arena_free_fn(struct EngineContext *ctx, struct Arena *arena, void *ptr)
{
    (void)ctx;
    (void)arena;
    (void)ptr;
}

Allocator allocator_heap(void)
{
    return (Allocator){
        .ctx = NULL,
        .arena = NULL,
        .malloc_fn = heap_malloc_fn,
        .realloc_fn = heap_realloc_fn,
        .free_fn = heap_free_fn,
    };
}

Allocator allocator_arena(struct EngineContext *ctx, struct Arena *arena)
{
    return (Allocator){
        .ctx = ctx,
        .arena = arena,
        .malloc_fn = arena_malloc_fn,
        .realloc_fn = arena_realloc_fn,
        .free_fn = arena_free_fn,
    };
}

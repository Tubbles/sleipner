#include "test_helpers.h"
#include "alloc.h"
#include "str.h"

#include <stdlib.h>

/* --- Heap allocator wrappers (test-only) --- */

static void *heap_malloc_fn(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void *heap_realloc_fn(void *ctx, void *ptr, size_t new_size)
{
    (void)ctx;
    return realloc(ptr, new_size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void heap_free_fn(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

Allocator allocator_heap(void)
{
    return (Allocator){
        .ctx = nullptr,
        .malloc_fn = heap_malloc_fn,
        .realloc_fn = heap_realloc_fn,
        .free_fn = heap_free_fn,
    };
}

Allocator test_heap_alloc;

void test_helpers_init(void)
{
    test_heap_alloc = allocator_heap();
}

void test_blueprint_table_free(BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(&table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

void test_blueprint_free(Blueprint *blueprint)
{
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(&test_heap_alloc, &blueprint->children.data[index].blueprint_name);
        str_free(&test_heap_alloc, &blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free(&blueprint->attrs);
}

void test_level_free(Level *level)
{
    level_free(&test_heap_alloc, level);
}

void test_entity_free(Entity *entity)
{
    str_free(&test_heap_alloc, &entity->blueprint_name);
    str_free(&test_heap_alloc, &entity->tag);
    test_attr_set_free(&entity->attrs);
}

void test_flag_set_free(FlagSet *flags)
{
    flag_set_free(&test_heap_alloc, flags);
}

void test_attr_set_free(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

#pragma once

#include "alloc.h"
#include "blueprint.h"
#include "level.h"
#include "rule.h"

/* Construct a heap-backed allocator (malloc/realloc/free). Test-only. */
Allocator allocator_heap(void);

/* Call once before running tests to initialise the heap allocator. */
void test_helpers_init(void);

/* Heap allocator for test code — initialised by test_helpers_init(). */
extern Allocator test_heap_alloc;

/* Cleanup helpers for test data structures */
void test_blueprint_table_free(BlueprintTable *table);
void test_blueprint_free(Blueprint *blueprint);
void test_level_free(Level *level);
void test_entity_free(Entity *entity);
void test_flag_set_free(FlagSet *flags);
void test_attr_set_free(AttrSet *set);

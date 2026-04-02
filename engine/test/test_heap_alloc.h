#ifndef TEST_HEAP_ALLOC_H
#define TEST_HEAP_ALLOC_H

#include "alloc.h"

/* Construct a heap-backed allocator (malloc/realloc/free). Test-only. */
Allocator allocator_heap(void);

/* Call once before running tests to initialise the heap allocator. */
void test_helpers_init(void);

/* Heap allocator for test code — initialised by test_helpers_init(). */
extern Allocator test_heap_alloc;

#endif

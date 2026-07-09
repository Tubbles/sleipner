#pragma once

/* vec.h — typed dynamic array via code-generation macros.
 *
 * Usage:
 *   In a header, after the element type definition:
 *     VEC_DECL(trigger_event, TriggerEvent)
 *   Emits: vec_trigger_event struct, and prototypes for
 *     vec_trigger_event_push / _clear / _free / _new.
 *
 *   In exactly one .c file:
 *     VEC_IMPL(trigger_event, TriggerEvent)
 *   Emits: the function bodies.
 *
 * Create a vec with vec_<name>_new(alloc) — the allocator is stored
 * in the struct and used by all subsequent push/free calls.
 * Access elements directly via vec.data[index] — it is a typed pointer.
 *
 * type may itself be a pointer type (e.g. VEC_DECL(foo_ptr, Foo *)) for a
 * vec of stable, individually-allocated elements referenced by pointer —
 * see "Vec growth and pointer stability" in CLAUDE.md for when that shape
 * is preferred over a vec of values. VEC_IMPL's Allocator calls cast
 * explicitly through void * for this case: vec->data is then a
 * multi-level pointer (Foo **), and an implicit conversion between a
 * multi-level pointer and void * is exactly what
 * bugprone-multi-level-implicit-pointer-conversion exists to catch. */

#include "alloc.h"

#include <stdbool.h>
#include <stddef.h> // IWYU pragma: export
#include <stdint.h> // IWYU pragma: export

#define VEC_INITIAL_CAPACITY 8
#define VEC_GROWTH_FACTOR 2

/* VEC_DECL(name, type) — declare the vec_<name> struct and function prototypes.
 * Place in a header, immediately after the element type definition.
 * typeof(type) is used in pointer declarations to satisfy bugprone-macro-parentheses. */
// clang-format off
#define VEC_DECL(name, type)                                                          \
    typedef struct {                                                                   \
        typeof(type) *data;                                                           \
        int           count;                                                          \
        int           capacity;                                                       \
        Allocator     alloc;                                                          \
    } vec_##name;                                                                     \
    static inline vec_##name vec_##name##_new(Allocator alloc_arg) {                  \
        return (vec_##name){.alloc = alloc_arg};                                      \
    }                                                                                 \
    [[nodiscard]] bool vec_##name##_push(vec_##name *vec, type element);              \
    void               vec_##name##_clear(vec_##name *vec);                           \
    void               vec_##name##_free(vec_##name *vec);

/* VEC_IMPL(name, type) — emit the function bodies declared by VEC_DECL.
 * Place in exactly one .c file per type. */
#define VEC_IMPL(name, type)                                                          \
    [[nodiscard]] bool vec_##name##_push(vec_##name *vec, type element) {             \
        if (vec->count >= vec->capacity) {                                            \
            int    new_cap   = vec->capacity == 0 ? VEC_INITIAL_CAPACITY              \
                                                  : vec->capacity * VEC_GROWTH_FACTOR;\
            size_t new_bytes = (size_t)new_cap * sizeof(type);                       \
            typeof(type) *new_data = (typeof(type) *)vec->alloc.realloc_fn(           \
                vec->alloc.ctx, (void *)vec->data, new_bytes);                        \
            if (!new_data) {                                                          \
                return false;                                                         \
            }                                                                         \
            vec->data     = new_data;                                                 \
            vec->capacity = new_cap;                                                  \
        }                                                                             \
        vec->data[vec->count] = element;                                              \
        vec->count++;                                                                 \
        return true;                                                                  \
    }                                                                                 \
    void vec_##name##_clear(vec_##name *vec) { vec->count = 0; }                     \
    void vec_##name##_free(vec_##name *vec) {                                        \
        if (vec->alloc.free_fn) {                                                      \
            vec->alloc.free_fn(vec->alloc.ctx, (void *)vec->data);                     \
        }                                                                             \
        vec->data     = nullptr;                                                      \
        vec->count    = 0;                                                            \
        vec->capacity = 0;                                                            \
    }
// clang-format on

/* --- Primitive type declarations ---
 * Implementations live in vec.c (already compiled into the engine library).
 * Include vec.h to use these types directly — no VEC_IMPL needed. */

VEC_DECL(bool, bool)
VEC_DECL(int, int)
VEC_DECL(float, float)
VEC_DECL(double, double)
VEC_DECL(size, size_t)
VEC_DECL(i8, int8_t)
VEC_DECL(i16, int16_t)
VEC_DECL(i32, int32_t)
VEC_DECL(i64, int64_t)
VEC_DECL(u8, uint8_t)
VEC_DECL(u16, uint16_t)
VEC_DECL(u32, uint32_t)
VEC_DECL(u64, uint64_t)

#ifdef __FLT16_MAX__
VEC_DECL(f16, _Float16)
#endif
#ifdef __FLT32_MAX__
VEC_DECL(f32, _Float32)
#endif
#ifdef __FLT64_MAX__
VEC_DECL(f64, _Float64)
#endif

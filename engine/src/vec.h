#ifndef VEC_H
#define VEC_H

/* vec.h — typed dynamic array via code-generation macros.
 *
 * Usage:
 *   In a header, after the element type definition:
 *     VEC_DECL(trigger_event, TriggerEvent)
 *   Emits: vec_trigger_event struct, and prototypes for
 *     vec_trigger_event_push / _clear / _free.
 *
 *   In exactly one .c file:
 *     VEC_IMPL(trigger_event, TriggerEvent)
 *   Emits: the function bodies.
 *
 * A zero-initialised vec_<name> is a valid empty container.
 * First push allocates VEC_INITIAL_CAPACITY elements.
 * Access elements directly via vec.data[index] — it is a typed pointer.
 *
 * Allocator overrides (set before including this header, or via -D):
 *   -DVEC_REALLOC=my_realloc
 *   -DVEC_FREE=my_free
 * Both must be provided together if either is overridden. */

#include <stdbool.h>

struct EngineContext; // IWYU pragma: export
#include <stddef.h>   // IWYU pragma: export
#include <stdint.h>   // IWYU pragma: export

#if !defined(VEC_REALLOC) || !defined(VEC_FREE)
#include <stdlib.h>
#endif

#ifndef VEC_REALLOC
#define VEC_REALLOC realloc
#endif

#ifndef VEC_FREE
#define VEC_FREE free
#endif

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
    } vec_##name;                                                                     \
    [[nodiscard]] bool vec_##name##_push(vec_##name *vec, type element);              \
    void               vec_##name##_clear(vec_##name *vec);                           \
    void               vec_##name##_free(vec_##name *vec);

/* VEC_IMPL(name, type) — emit the function bodies declared by VEC_DECL.
 * Place in exactly one .c file per type. */
#define VEC_IMPL(name, type)                                                          \
    [[nodiscard]] bool vec_##name##_push(vec_##name *vec, type element) {             \
        if (vec->count >= vec->capacity) {                                            \
            int new_cap = vec->capacity == 0 ? VEC_INITIAL_CAPACITY                   \
                                             : vec->capacity * VEC_GROWTH_FACTOR;     \
            typeof(type) *new_data =                                                  \
                VEC_REALLOC(vec->data, (size_t)new_cap * sizeof(type));               \
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
    void vec_##name##_free(vec_##name *vec) {                                         \
        VEC_FREE(vec->data);                                                          \
        *vec = (vec_##name){0};                                                       \
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

#endif

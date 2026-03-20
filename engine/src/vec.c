#include "vec.h"

/* Primitive vec implementations.
 * Structs and prototypes are in vec.h — include that to use these types. */

// clang-format off
VEC_IMPL(bool,   bool)
VEC_IMPL(int,    int)
VEC_IMPL(float,  float)
VEC_IMPL(double, double)
VEC_IMPL(size,   size_t)
VEC_IMPL(i8,     int8_t)
VEC_IMPL(i16,    int16_t)
VEC_IMPL(i32,    int32_t)
VEC_IMPL(i64,    int64_t)
VEC_IMPL(u8,     uint8_t)
VEC_IMPL(u16,    uint16_t)
VEC_IMPL(u32,    uint32_t)
VEC_IMPL(u64,    uint64_t)

#ifdef __FLT16_MAX__
VEC_IMPL(f16, _Float16)
#endif
#ifdef __FLT32_MAX__
VEC_IMPL(f32, _Float32)
#endif
#ifdef __FLT64_MAX__
VEC_IMPL(f64, _Float64)
#endif
// clang-format on

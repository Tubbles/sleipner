#include "map.h"

/* Primitive map implementations (int-keyed).
 * Structs and prototypes are in map.h — include that to use these types. */

// clang-format off
MAP_IMPL(int_bool,   int, bool,     map_hash_int, map_eq_int)
MAP_IMPL(int_int,    int, int,      map_hash_int, map_eq_int)
MAP_IMPL(int_float,  int, float,    map_hash_int, map_eq_int)
MAP_IMPL(int_double, int, double,   map_hash_int, map_eq_int)
MAP_IMPL(int_size,   int, size_t,   map_hash_int, map_eq_int)
MAP_IMPL(int_i8,     int, int8_t,   map_hash_int, map_eq_int)
MAP_IMPL(int_i16,    int, int16_t,  map_hash_int, map_eq_int)
MAP_IMPL(int_i32,    int, int32_t,  map_hash_int, map_eq_int)
MAP_IMPL(int_i64,    int, int64_t,  map_hash_int, map_eq_int)
MAP_IMPL(int_u8,     int, uint8_t,  map_hash_int, map_eq_int)
MAP_IMPL(int_u16,    int, uint16_t, map_hash_int, map_eq_int)
MAP_IMPL(int_u32,    int, uint32_t, map_hash_int, map_eq_int)
MAP_IMPL(int_u64,    int, uint64_t, map_hash_int, map_eq_int)
// clang-format on

#ifndef MAP_H
#define MAP_H

/* map.h — typed open-addressing hash map via code-generation macros.
 *
 * Usage:
 *   In a header, after key and value type definitions:
 *     MAP_DECL(name, key_type, value_type)
 *   Emits: map_name struct + map_name_entry entry type + prototypes for
 *     map_name_set / _get / _remove / _free.
 *
 *   In exactly one .c file:
 *     MAP_IMPL(name, key_type, value_type, hash_fn, eq_fn)
 *   Emits: function bodies.  hash_fn and eq_fn are only needed here.
 *
 * A zero-initialised map_<name> is a valid empty map.
 * Capacity is always a power of 2; initial capacity is MAP_INITIAL_CAPACITY on first insert.
 * Rehashes (doubles capacity) when (count + 1) * 4 > capacity * 3  (75% load factor).
 * Deleted entries use tombstone state MAP_ENTRY_DELETED so probe chains stay intact.
 *
 * Pass an Allocator * to set/free — nullptr is not permitted. */

#include "alloc.h"
#include "str.h"

#include <stdbool.h>
#include <stddef.h> // IWYU pragma: export
#include <stdint.h> // IWYU pragma: export
#include <string.h>

#define MAP_INITIAL_CAPACITY 16
#define MAP_ENTRY_EMPTY 0
#define MAP_ENTRY_OCCUPIED 1
#define MAP_ENTRY_DELETED 2
#define MAP_HASH_INT_MULTIPLIER 0x45d9f3bU /* Murmur3 finalizer mix constant */

/* Built-in hash and equality functions (int keys). */
static inline uint32_t map_hash_int(int key)
{
    uint32_t bits = (uint32_t)key;
    bits ^= bits >> 16;
    bits *= MAP_HASH_INT_MULTIPLIER;
    bits ^= bits >> 16;
    return bits;
}

static inline bool map_eq_int(int first, int second)
{
    return first == second;
}

/* MAP_DECL(name, key_type, value_type) — declare the map_<name> struct, entry type, and
 * function prototypes.  Place in a header after both key and value type definitions.
 * typeof(key_type) / typeof(value_type) satisfies bugprone-macro-parentheses. */
// clang-format off
#define MAP_DECL(name, key_type, value_type)                                               \
    typedef struct {                                                                        \
        typeof(key_type)   key;                                                            \
        typeof(value_type) value;                                                          \
        uint8_t            state;                                                          \
    } map_##name##_entry;                                                                  \
    typedef struct {                                                                        \
        map_##name##_entry *entries;                                                       \
        int                 count;                                                         \
        int                 capacity;                                                      \
    } map_##name;                                                                          \
    [[nodiscard]] bool            map_##name##_set(map_##name *map, key_type key, value_type value, Allocator *alloc); \
    const typeof(value_type)     *map_##name##_get(const map_##name *map, key_type key);   \
    bool                          map_##name##_remove(map_##name *map, key_type key);      \
    void                          map_##name##_free(map_##name *map, Allocator *alloc);

/* MAP_IMPL(name, key_type, value_type, hash_fn, eq_fn) — emit the function bodies
 * declared by MAP_DECL.  Place in exactly one .c file per type.
 *
 * hash_fn: uint32_t hash_fn(key_type key)
 * eq_fn:   bool     eq_fn(key_type a, key_type b) */
#define MAP_IMPL(name, key_type, value_type, hash_fn, eq_fn)                               \
    static int map_##name##_find_slot(const map_##name *map, key_type key) {               \
        if (!map->entries) return -1;                                                       \
        uint32_t hash = hash_fn(key);                                                      \
        for (int probe = 0; probe < map->capacity; probe++) {                              \
            int slot = (int)((hash + (uint32_t)probe) & (uint32_t)(map->capacity - 1));    \
            if (map->entries[slot].state == MAP_ENTRY_EMPTY) return -1;                    \
            if (map->entries[slot].state == MAP_ENTRY_OCCUPIED &&                          \
                eq_fn(map->entries[slot].key, key)) return slot;                           \
        }                                                                                  \
        return -1;                                                                         \
    }                                                                                      \
    static bool map_##name##_rehash(map_##name *map, Allocator *alloc) {                  \
        int    new_cap   = map->capacity == 0 ? MAP_INITIAL_CAPACITY : map->capacity * 2; \
        size_t new_bytes = (size_t)new_cap * sizeof(map_##name##_entry);                  \
        map_##name##_entry *new_entries = alloc->malloc_fn(alloc->ctx, new_bytes);                                                                                  \
        if (!new_entries) return false;                                                    \
        memset(new_entries, 0, new_bytes);                                                 \
        for (int index = 0; index < map->capacity; index++) {                             \
            if (map->entries[index].state != MAP_ENTRY_OCCUPIED) continue;                \
            uint32_t hash = hash_fn(map->entries[index].key);                              \
            for (int probe = 0; probe < new_cap; probe++) {                               \
                int slot = (int)((hash + (uint32_t)probe) & (uint32_t)(new_cap - 1));     \
                if (new_entries[slot].state == MAP_ENTRY_EMPTY) {                         \
                    new_entries[slot] = (map_##name##_entry){                             \
                        .key = map->entries[index].key, .value = map->entries[index].value, \
                        .state = MAP_ENTRY_OCCUPIED};                                     \
                    break;                                                                 \
                }                                                                          \
            }                                                                              \
        }                                                                                  \
        alloc->free_fn(alloc->ctx, map->entries);                                           \
        map->entries  = new_entries;                                                       \
        map->capacity = new_cap;                                                           \
        return true;                                                                       \
    }                                                                                      \
    [[nodiscard]] bool map_##name##_set(map_##name *map, key_type key, value_type value, Allocator *alloc) { \
        if (map->capacity > 0) {                                                           \
            int existing = map_##name##_find_slot(map, key);                               \
            if (existing >= 0) { map->entries[existing].value = value; return true; }      \
        }                                                                                  \
        if (map->capacity == 0 || (map->count + 1) * 4 > map->capacity * 3) {            \
            if (!map_##name##_rehash(map, alloc)) return false;                            \
        }                                                                                  \
        uint32_t hash          = hash_fn(key);                                             \
        int      first_deleted = -1;                                                       \
        for (int probe = 0; probe < map->capacity; probe++) {                              \
            int                 slot  = (int)((hash + (uint32_t)probe) & (uint32_t)(map->capacity - 1)); \
            map_##name##_entry *entry = &map->entries[slot];                               \
            if (entry->state == MAP_ENTRY_DELETED && first_deleted < 0) {                  \
                first_deleted = slot;                                                       \
            }                                                                              \
            if (entry->state == MAP_ENTRY_EMPTY) {                                         \
                int target = first_deleted >= 0 ? first_deleted : slot;                    \
                map->entries[target] = (map_##name##_entry){                               \
                    .key = key, .value = value, .state = MAP_ENTRY_OCCUPIED};              \
                map->count++;                                                               \
                return true;                                                                \
            }                                                                              \
        }                                                                                  \
        if (first_deleted >= 0) {                                                          \
            map->entries[first_deleted] = (map_##name##_entry){                            \
                .key = key, .value = value, .state = MAP_ENTRY_OCCUPIED};                  \
            map->count++;                                                                   \
            return true;                                                                    \
        }                                                                                   \
        return false;                                                                       \
    }                                                                                      \
    const typeof(value_type) *map_##name##_get(const map_##name *map, key_type key) {     \
        int slot = map_##name##_find_slot(map, key);                                       \
        return slot >= 0 ? &map->entries[slot].value : nullptr;                               \
    }                                                                                      \
    bool map_##name##_remove(map_##name *map, key_type key) {                             \
        int slot = map_##name##_find_slot(map, key);                                       \
        if (slot < 0) return false;                                                        \
        map->entries[slot].state = MAP_ENTRY_DELETED;                                      \
        map->count--;                                                                       \
        return true;                                                                        \
    }                                                                                      \
    void map_##name##_free(map_##name *map, Allocator *alloc) {                           \
        alloc->free_fn(alloc->ctx, map->entries);                                           \
        *map = (map_##name){0};                                                            \
    }
// clang-format on

/* --- Primitive type declarations (int-keyed) ---
 * Implementations live in map.c.  Include map.h to use these types directly
 * — no MAP_IMPL needed. */

MAP_DECL(int_bool, int, bool)
MAP_DECL(int_int, int, int)
MAP_DECL(int_float, int, float)
MAP_DECL(int_double, int, double)
MAP_DECL(int_size, int, size_t)
MAP_DECL(int_i8, int, int8_t)
MAP_DECL(int_i16, int, int16_t)
MAP_DECL(int_i32, int, int32_t)
MAP_DECL(int_i64, int, int64_t)
MAP_DECL(int_u8, int, uint8_t)
MAP_DECL(int_u16, int, uint16_t)
MAP_DECL(int_u32, int, uint32_t)
MAP_DECL(int_u64, int, uint64_t)
MAP_DECL(int_str, int, Str)

#endif

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *ptr;
    size_t len;
} Strv;

/* Construct a Strv from a null-terminated string. */
Strv strv_from_cstr(const char *cstr);

/* Advance ptr by n and decrease len by n. Clamped to len. */
void strv_shrink_left(Strv *strv, size_t n);

/* Decrease len by n. Clamped to len. */
void strv_shrink_right(Strv *strv, size_t n);

/* Remove leading whitespace. */
void strv_trim_left(Strv *strv);

/* Remove trailing whitespace. */
void strv_trim_right(Strv *strv);

/* Remove leading and trailing whitespace. */
void strv_trim(Strv *strv);

/* Return the portion of strv before the first occurrence of delim and
 * advance strv past the delimiter. If delim is not found, returns the
 * entire view and sets strv to {nullptr, 0}. */
Strv strv_split(Strv *strv, char delim);

/* Return true if a and b have the same length and content. */
bool strv_eq(Strv lhs, Strv rhs);

/* FNV-1a, 32-bit. Shared hash function for every Strv-keyed MAP_DECL
 * (map_strv_sound, audio.h; map_strv_int, rule.h) so they agree on
 * distribution without duplicating the algorithm per call site. */
uint32_t strv_hash(Strv value);

/* Return true if strv has the same content as the null-terminated cstr. */
bool strv_eq_cstr(Strv strv, const char *cstr);

/* Return true if strv begins with the null-terminated prefix. */
bool strv_starts_with_cstr(Strv strv, const char *prefix);

/* Copy at most dest_size-1 bytes from strv into dest and null-terminate.
 * dest_size must be at least 1. */
void strv_copy_to_cstr(Strv strv, char *dest, size_t dest_size);

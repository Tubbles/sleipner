#ifndef STR_H
#define STR_H

#include "alloc.h"
#include "strv.h"

#include <stdbool.h>
#include <stddef.h>

/* Owning, heap-backed, growable string.  ptr[len] == '\0' is always maintained.
 * All functions that allocate take alloc; pass NULL to use libc heap.
 * A zero-initialised Str {0} is uninitialized; call str_from_* before use. */
typedef struct {
    char *ptr;  /* null-terminated; never NULL after a successful str_from_* */
    size_t len; /* character count, NOT including the null terminator */
    size_t cap; /* allocated bytes, NOT counting the null terminator */
} Str;

/* Construct a Str by copying a null-terminated C string. */
[[nodiscard]] bool str_from_cstr(Allocator *alloc, Str *out, const char *cstr);

/* Construct a Str by copying a string view. */
[[nodiscard]] bool str_from_strv(Allocator *alloc, Str *out, Strv strv);

/* Free the string's backing memory and zero the struct. Safe on {0}. */
void str_free(Allocator *alloc, Str *str);

/* Append a single character. Grows if needed. */
[[nodiscard]] bool str_push_char(Allocator *alloc, Str *str, char character);

/* Append a null-terminated C string. Grows if needed. */
[[nodiscard]] bool str_append_cstr(Allocator *alloc, Str *str, const char *cstr);

/* Append a string view. Grows if needed. */
[[nodiscard]] bool str_append_strv(Allocator *alloc, Str *str, Strv strv);

/* Return a non-owning view of the string. Requires an initialized Str. */
Strv str_to_strv(Str str);

/* Reset length to zero, keeping allocation. ptr[0] set to '\0'. */
void str_clear(Str *str);

#endif

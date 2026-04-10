#pragma once

#include "alloc.h"
#include "strv.h"

#include <stdbool.h>
#include <stddef.h>

/* Owning, growable string.  ptr[len] == '\0' is always maintained.
 * Create a Str with str_new(alloc) — the allocator is stored in the struct
 * and used by all subsequent operations.
 * A zero-initialised Str {0} is uninitialized; call str_new then str_from_* before use. */
typedef struct {
    char *ptr;       /* null-terminated; never nullptr after a successful str_from_* */
    size_t len;      /* character count, NOT including the null terminator */
    size_t cap;      /* allocated bytes, NOT counting the null terminator */
    Allocator alloc; /* stored allocator, set by str_new */
} Str;

/* Create an empty Str with the given allocator stored for all future operations. */
static inline Str str_new(Allocator alloc_arg)
{
    return (Str){.alloc = alloc_arg};
}

/* Construct a Str by copying a null-terminated C string. */
[[nodiscard]] bool str_from_cstr(Str *out, const char *cstr);

/* Construct a Str by copying a string view. */
[[nodiscard]] bool str_from_strv(Str *out, Strv strv);

/* Free the string's backing memory and zero data fields (preserves allocator). */
void str_free(Str *str);

/* Append a single character. Grows if needed. */
[[nodiscard]] bool str_push_char(Str *str, char character);

/* Append a null-terminated C string. Grows if needed. */
[[nodiscard]] bool str_append_cstr(Str *str, const char *cstr);

/* Append a string view. Grows if needed. */
[[nodiscard]] bool str_append_strv(Str *str, Strv strv);

/* Return a non-owning view of the string. Requires an initialized Str. */
Strv str_to_strv(Str str);

/* Reset length to zero, keeping allocation. ptr[0] set to '\0'. */
void str_clear(Str *str);

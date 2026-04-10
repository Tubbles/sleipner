#include "str.h"

#include "check.h"
#include "strv.h"

#include <string.h>

#define STR_INITIAL_CAP 16

/* Ensure at least `needed` bytes of capacity are available.
 * Always allocates when ptr is nullptr, so ptr is non-nullptr on success. */
static bool str_ensure_cap(Str *str, size_t needed)
{
    if (str->ptr != nullptr && needed <= str->cap) {
        return true;
    }
    size_t new_cap = str->cap < STR_INITIAL_CAP ? STR_INITIAL_CAP : str->cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *new_ptr = str->alloc.realloc_fn(str->alloc.ctx, str->ptr, new_cap + 1);
    if (!new_ptr) {
        return false;
    }
    str->ptr = new_ptr;
    str->cap = new_cap;
    return true;
}

bool str_from_strv(Str *out, Strv strv)
{
    str_free(out);
    if (!str_ensure_cap(out, strv.len)) {
        return false;
    }
    if (strv.len > 0) {
        memcpy(out->ptr, strv.ptr, strv.len);
    }
    out->len = strv.len;
    out->ptr[out->len] = '\0';
    return true;
}

bool str_from_cstr(Str *out, const char *cstr)
{
    return str_from_strv(out, strv_from_cstr(cstr));
}

void str_free(Str *str)
{
    CHECK(str);
    if (str->alloc.free_fn) {
        str->alloc.free_fn(str->alloc.ctx, str->ptr);
    }
    str->ptr = nullptr;
    str->len = 0;
    str->cap = 0;
}

bool str_append_strv(Str *str, Strv strv)
{
    if (strv.len == 0) {
        return true;
    }
    if (!str_ensure_cap(str, str->len + strv.len)) {
        return false;
    }
    memcpy(str->ptr + str->len, strv.ptr, strv.len);
    str->len += strv.len;
    str->ptr[str->len] = '\0';
    return true;
}

bool str_append_cstr(Str *str, const char *cstr)
{
    return str_append_strv(str, strv_from_cstr(cstr));
}

bool str_push_char(Str *str, char character)
{
    if (!str_ensure_cap(str, str->len + 1)) {
        return false;
    }
    str->ptr[str->len++] = character;
    str->ptr[str->len] = '\0';
    return true;
}

Strv str_to_strv(Str str)
{
    return (Strv){.ptr = str.ptr, .len = str.len};
}

void str_clear(Str *str)
{
    str->len = 0;
    if (str->ptr) {
        str->ptr[0] = '\0';
    }
}

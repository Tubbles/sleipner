#include "str.h"

#include "error.h"
#include "strv.h"

#include <stdlib.h>
#include <string.h>

#define STR_INITIAL_CAP 16

/* Ensure at least `needed` bytes of capacity are available.
 * Always allocates when ptr is NULL, so ptr is non-NULL on success. */
static bool str_ensure_cap(struct EngineContext *ctx, Str *str, size_t needed)
{
    if (str->ptr != nullptr && needed <= str->cap) {
        return true;
    }
    size_t new_cap = str->cap < STR_INITIAL_CAP ? STR_INITIAL_CAP : str->cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *new_ptr = realloc(str->ptr, new_cap + 1);
    if (!new_ptr) {
        error_set(ctx, "str: allocation failed");
        return false;
    }
    str->ptr = new_ptr;
    str->cap = new_cap;
    return true;
}

bool str_from_strv(struct EngineContext *ctx, Str *out, Strv strv)
{
    str_free(ctx, out);
    if (!str_ensure_cap(ctx, out, strv.len)) {
        return false;
    }
    if (strv.len > 0) {
        memcpy(out->ptr, strv.ptr, strv.len);
    }
    out->len = strv.len;
    out->ptr[out->len] = '\0';
    return true;
}

bool str_from_cstr(struct EngineContext *ctx, Str *out, const char *cstr)
{
    return str_from_strv(ctx, out, strv_from_cstr(cstr));
}

void str_free(struct EngineContext *ctx, Str *str)
{
    (void)ctx;
    free(str->ptr);
    *str = (Str){0};
}

bool str_append_strv(struct EngineContext *ctx, Str *str, Strv strv)
{
    if (strv.len == 0) {
        return true;
    }
    if (!str_ensure_cap(ctx, str, str->len + strv.len)) {
        return false;
    }
    memcpy(str->ptr + str->len, strv.ptr, strv.len);
    str->len += strv.len;
    str->ptr[str->len] = '\0';
    return true;
}

bool str_append_cstr(struct EngineContext *ctx, Str *str, const char *cstr)
{
    return str_append_strv(ctx, str, strv_from_cstr(cstr));
}

bool str_push_char(struct EngineContext *ctx, Str *str, char character)
{
    if (!str_ensure_cap(ctx, str, str->len + 1)) {
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

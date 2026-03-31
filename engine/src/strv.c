#include "strv.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

Strv strv_from_cstr(const char *cstr)
{
    return (Strv){.ptr = cstr, .len = strlen(cstr)};
}

void strv_shrink_left(Strv *strv, size_t n)
{
    if (n > strv->len) {
        n = strv->len;
    }
    strv->ptr += n;
    strv->len -= n;
}

void strv_shrink_right(Strv *strv, size_t n)
{
    if (n > strv->len) {
        n = strv->len;
    }
    strv->len -= n;
}

void strv_trim_left(Strv *strv)
{
    while (strv->len > 0 && isspace((unsigned char)strv->ptr[0])) {
        strv_shrink_left(strv, 1);
    }
}

void strv_trim_right(Strv *strv)
{
    while (strv->len > 0 && isspace((unsigned char)strv->ptr[strv->len - 1])) {
        strv_shrink_right(strv, 1);
    }
}

void strv_trim(Strv *strv)
{
    strv_trim_left(strv);
    strv_trim_right(strv);
}

Strv strv_split(Strv *strv, char delim)
{
    for (size_t index = 0; index < strv->len; index++) {
        if (strv->ptr[index] == delim) {
            Strv head = {.ptr = strv->ptr, .len = index};
            strv->ptr += index + 1;
            strv->len -= index + 1;
            return head;
        }
    }
    Strv head = *strv;
    *strv = (Strv){nullptr, 0};
    return head;
}

bool strv_eq(Strv lhs, Strv rhs)
{
    return lhs.len == rhs.len && memcmp(lhs.ptr, rhs.ptr, lhs.len) == 0;
}

bool strv_eq_cstr(Strv strv, const char *cstr)
{
    return strv_eq(strv, strv_from_cstr(cstr));
}

bool strv_starts_with_cstr(Strv strv, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    return strv.len >= prefix_len && memcmp(strv.ptr, prefix, prefix_len) == 0;
}

void strv_copy_to_cstr(Strv strv, char *dest, size_t dest_size)
{
    size_t copy_len = strv.len < dest_size - 1 ? strv.len : dest_size - 1;
    memcpy(dest, strv.ptr, copy_len);
    dest[copy_len] = '\0';
}

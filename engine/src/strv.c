#include "strv.h"

#include <ctype.h>
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

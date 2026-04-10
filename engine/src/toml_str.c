#include "toml_str.h"

#include "str.h"
#include "toml.h"

#include <stdlib.h>

bool str_from_toml_datum(Str *out, toml_datum_t *datum)
{
    bool success = str_from_cstr(out, datum->u.s);
    free(datum->u.s);
    datum->u.s = nullptr;
    return success;
}

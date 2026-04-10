#pragma once

#include "str.h"
#include "toml.h"

#include <stdbool.h>

/* Copy datum->u.s into out (which must have been created via str_new), free TOML's
 * allocation, and null datum->u.s to prevent use-after-free at the call site.
 * This is the standard boundary crossing from TOML into our memory model. */
[[nodiscard]] bool str_from_toml_datum(Str *out, toml_datum_t *datum);

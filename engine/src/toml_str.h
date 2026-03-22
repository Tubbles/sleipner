#ifndef TOML_STR_H
#define TOML_STR_H

#include "str.h"
#include "toml.h"

#include <stdbool.h>

struct EngineContext;

/* Copy datum->u.s into a Str via ctx's allocator, free TOML's allocation,
 * and null datum->u.s to prevent use-after-free at the call site.
 * This is the standard boundary crossing from TOML into our memory model. */
[[nodiscard]] bool str_from_toml_datum(struct EngineContext *ctx, Str *out, toml_datum_t *datum);

#endif

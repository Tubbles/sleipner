#pragma once

#include "diag.h"

/* Copy `path` to `path`.bak via a raw byte-for-byte fread/fwrite pass (not
 * a text round-trip), so a caller about to overwrite `path` can preserve
 * the previous version first. Extracted from main.c's save_gamedata (the
 * original, still the primary caller) so save.c's save_write can reuse it
 * too. Returns false and sets diag->error on any open/read/write failure;
 * `path` itself is left untouched either way. */
[[nodiscard]] bool backup_file(Diag *diag, const char *path);

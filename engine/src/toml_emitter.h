#ifndef TOML_EMITTER_H
#define TOML_EMITTER_H

#include "blueprint.h"
#include "level.h"

/* Emit blueprint and level data as TOML into a buffer.
 * Returns the number of bytes written (excluding null terminator),
 * or -1 if the buffer is too small. */
[[nodiscard]] int toml_emit_gamedata(struct EngineContext *ctx,
                                     char *buffer,
                                     int capacity,
                                     const BlueprintTable *blueprints,
                                     const Level *levels,
                                     int level_count);

#endif

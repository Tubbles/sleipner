#pragma once

#include "alloc.h"
#include "error.h"
#include "progression.h"
#include "str.h"

/* Save-file format version (S6.15c, D33). save_deserialize rejects any
 * other value with an error rather than guessing at a migration -- bump
 * this when the [save] schema changes in a way old readers can't tolerate. */
#define SAVE_FORMAT_VERSION 1

/* Thin bundle over the process-lifetime ProgressionState (flags/vars/items/
 * level_deltas, progression.h) plus the one extra fact a save needs that
 * ProgressionState doesn't already carry: which level to reload on restore.
 * There is deliberately no separate cross-level "player" section yet -- the
 * player's position/attrs for whichever level it was in at save time ride
 * inside that level's own EntityDelta (progression_capture_level_delta must
 * have run for the current level before save_serialize is called so that
 * delta is fresh); see TODO.md for this gap. save_serialize/save_deserialize
 * read/write this struct directly against caller-supplied storage -- it owns
 * nothing beyond what ProgressionState itself owns. */
typedef struct {
    Str current_level_name;
    ProgressionState progression;
} SaveState;

/* Serialize `save` as TOML text (schema documented in save.c, reusing
 * toml_emitter.c's typed-attr encoding for vars/item counts/entity-delta
 * attrs) into a freshly allocated Str. Returns an empty Str ({0}, ptr ==
 * nullptr) and sets `err` if the internal formatting buffer is too small
 * for the given save. */
[[nodiscard]] Str save_serialize(const SaveState *save, Allocator *alloc, ErrorState *err);

/* Parse `toml` (a NUL-terminated save-format string) into `out`, populating
 * a fresh SaveState allocated from `alloc`. `out` is unconditionally
 * overwritten with a freshly zeroed value before parsing -- pass a
 * zero-initialized (or otherwise discardable) SaveState. A missing/empty
 * section (no `flags`, no [save.vars], no [[level_delta]], etc.) yields an
 * empty-but-valid result, not an error. Returns false and sets `err` on
 * malformed TOML, a missing [save] header, or a `version` this build does
 * not understand (SAVE_FORMAT_VERSION mismatch) -- never crashes on bad
 * input. */
[[nodiscard]] bool save_deserialize(const char *toml, Allocator *alloc, ErrorState *err, SaveState *out);

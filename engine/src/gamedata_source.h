#pragma once

#include "game.h"

/* File-first / embedded-fallback resolution for gamedata.toml (S7.2, D40).
 *
 * Extracted out of main.c (rather than kept static there) so the fallback
 * logic is reachable from engine_tests, which does not link main.c -- see
 * test/test_helpers.c's test_level_loader comment for why the test binary
 * feeds gamedata.toml's content through its own in-memory path instead of
 * production's disk-reading load_gamedata. */
typedef struct {
    /* Null on failure -- state->error holds the reason. Otherwise a
     * null-terminated TOML string. game_load_gamedata memcpy's this into
     * its own arena buffer before parsing, so the pointer only needs to
     * stay valid for the duration of that one call. */
    const char *toml_string;
    /* true: read from the on-disk file at `path`. false: SLEIPNER_EMBED_GAMEDATA
     * fallback (the build-time embedded copy of data/gamedata.toml). */
    bool from_file;
} GamedataSource;

/* Prefers the on-disk file at `path` (via FileExists + a stat/fopen/fread
 * pass logged the same way production always has); when it is absent, a
 * build with SLEIPNER_EMBED_GAMEDATA falls back to the copy of
 * data/gamedata.toml embedded via the asset pipeline (assets.h,
 * engine/sources.cmake), so a distributed binary boots with no external
 * gamedata present. Without that build flag, a missing file is a hard
 * error (state->error set, toml_string nullptr). */
[[nodiscard]] GamedataSource gamedata_source_read(GameState *state, const char *path);

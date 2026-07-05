#include "gamedata_source.h"

#include "arena.h"
#include "assets.h"
#include "debug.h"
#include "error.h"
#include "fopen_mode.h"
#include "game.h"
#include "raylib.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Read cap for gamedata.toml, mirroring main.c's own MAX_GAMEDATA_SIZE
 * (which separately bounds the save-side emit buffer in save_gamedata --
 * the two constants aren't coupled, just historically sized the same). */
#define GAMEDATA_SOURCE_MAX_FILE_SIZE (256UL * 1024)

/* Read `path` in full into a null-terminated buffer allocated from `arena`.
 * Moved out of main.c verbatim (S7.2) alongside gamedata_source_read, its
 * sole caller. */
static char *read_file_text(GameState *state, const char *path, Arena *arena)
{
    /* Stat the file first for diagnostics */
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        error_set(&state->error, "stat(%s): %s", path, strerror(errno));
        return nullptr;
    }
    debug_log(&state->debug, "gamedata: stat(%s): size=%ld mode=%o uid=%d gid=%d", path, (long)file_stat.st_size,
              (unsigned)file_stat.st_mode, (int)file_stat.st_uid, (int)file_stat.st_gid);

    FILE *file = fopen(path, FOPEN_READ);
    if (!file) {
        error_set(&state->error, "fopen(%s): %s", path, strerror(errno));
        return nullptr;
    }

    ArenaCheckpoint read_cp = arena_save(arena);
    char *buffer = arena_alloc(arena, GAMEDATA_SOURCE_MAX_FILE_SIZE + 1);
    if (!buffer) {
        (void)fclose(file);
        return nullptr;
    }
    /* Pre-zero so null termination is automatic; avoids tainted-index write after fread */
    (void)memset(buffer, 0, GAMEDATA_SOURCE_MAX_FILE_SIZE + 1);

    size_t bytes_read = fread(buffer, 1, GAMEDATA_SOURCE_MAX_FILE_SIZE, file);
    if (ferror(file)) {
        error_set(&state->error, "fread(%s): %s", path, strerror(errno));
        arena_restore(arena, read_cp);
        (void)fclose(file);
        return nullptr;
    }
    (void)fclose(file);
    debug_log(&state->debug, "gamedata: read %zu bytes from %s", bytes_read, path);
    return buffer;
}

GamedataSource gamedata_source_read(GameState *state, const char *path)
{
    if (FileExists(path)) {
        return (GamedataSource){.toml_string = read_file_text(state, path, &state->gamedata_arena), .from_file = true};
    }

#if defined(SLEIPNER_EMBED_GAMEDATA)
    debug_log(&state->debug, "gamedata: %s not found, falling back to embedded copy", path);
    EmbeddedAsset embedded = ASSET(gamedata_toml);
    return (GamedataSource){.toml_string = (const char *)embedded.data, .from_file = false};
#else
    error_set(&state->error, "gamedata file not found at %s (no embedded fallback in this build)", path);
    return (GamedataSource){.toml_string = nullptr, .from_file = false};
#endif
}

#pragma once

#include "alloc.h"
#include "error.h"
#include "str.h"

/* User-overridable runtime preferences.
 *
 * `data_dir` controls where gamedata.toml, keybindings.toml, and
 * trace.log are read and written. `master_volume`/`music_volume`/
 * `sfx_volume` (S6.13a, D32/F31) scale the music stream and SFX
 * aliases -- see preferences_effective_music_volume /
 * preferences_effective_sfx_volume below. New top-level preference
 * fields go here as the Settings UI grows.
 *
 * Storage: `preferences.toml` at the OS-conventional config directory
 * (see platform_paths.h). File schema:
 *
 *     [paths]
 *     data_dir = "/path/to/dir/"
 *
 *     [audio]
 *     master_volume = 1.0
 *     music_volume = 1.0
 *     sfx_volume = 1.0
 *
 * Lifetime: data_dir's Str is allocated once at startup against the
 * caller-provided allocator (in the engine, gamedata_arena). On
 * commit from the Settings UI, str_clear + str_append_cstr reuse the
 * existing allocation when the new value fits; growth is event-driven
 * and bounded, not per-frame, so any orphaned bytes left in the arena
 * are negligible across a session. The volume fields are plain floats
 * with no allocation of their own.
 *
 * The defaults come from preferences_init_defaults() and match the
 * existing pre-prefs constants:
 *   Linux/desktop      : "data/"
 *   Android            : "/storage/emulated/0/Sync/sleipner/"
 * Volumes default to PREFERENCES_VOLUME_DEFAULT (1.0, unattenuated).
 *
 * Loading rules (preferences_load):
 *   - Missing file → returns true, prefs unchanged (defaults remain).
 *   - Parse failure → returns false with an error chain set.
 *   - Successful parse → overlays present fields onto prefs. Fields not
 *     present in the file (including an entirely absent [audio] table,
 *     the back-compat case for preferences.toml files written before
 *     S6.13a) keep whatever value prefs already held (the defaults).
 *     Volume fields are clamped to [PREFERENCES_VOLUME_MIN,
 *     PREFERENCES_VOLUME_MAX] after parsing.
 *
 * Saving rules (preferences_save):
 *   - Always overwrites the file with the full current preference set.
 *   - The caller is responsible for ensuring the parent directory exists
 *     (use platform_ensure_parent_dir before the first call). */

#define PREFERENCES_VOLUME_MIN 0.0F
#define PREFERENCES_VOLUME_MAX 1.0F
#define PREFERENCES_VOLUME_DEFAULT 1.0F

typedef struct {
    Str data_dir;
    float master_volume;
    float music_volume;
    float sfx_volume;
} Preferences;

/* Initialize prefs to the platform's compile-time defaults. data_dir
 * is allocated against `alloc`. Subsequent commits to data_dir reuse
 * the same allocation. Volumes default to PREFERENCES_VOLUME_DEFAULT. */
void preferences_init_defaults(Preferences *prefs, Allocator alloc);

/* Load preferences from `path`. Missing file returns true with prefs
 * untouched. Parse errors return false with `err` set. */
[[nodiscard]] bool preferences_load(Preferences *prefs, ErrorState *err, const char *path);

/* Write preferences to `path`. Caller must have ensured the parent
 * directory exists. */
[[nodiscard]] bool preferences_save(const Preferences *prefs, ErrorState *err, const char *path);

/* Effective volume applied to the music stream: master folded into the
 * music channel (master * music), clamped to [PREFERENCES_VOLUME_MIN,
 * PREFERENCES_VOLUME_MAX]. Pure -- no raylib calls, safe to unit test
 * without an audio device. */
[[nodiscard]] float preferences_effective_music_volume(const Preferences *prefs);

/* Effective volume applied to SFX aliases: master folded into the sfx
 * channel (master * sfx), clamped the same way as the music helper
 * above. */
[[nodiscard]] float preferences_effective_sfx_volume(const Preferences *prefs);

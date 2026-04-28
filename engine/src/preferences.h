#pragma once

#include "alloc.h"
#include "error.h"
#include "str.h"

/* User-overridable runtime preferences.
 *
 * Currently a single field, `data_dir`, controlling where gamedata.toml,
 * keybindings.toml, and trace.log are read and written. New top-level
 * preference fields go here as the Settings UI grows.
 *
 * Storage: `preferences.toml` at the OS-conventional config directory
 * (see platform_paths.h). File schema:
 *
 *     [paths]
 *     data_dir = "/path/to/dir/"
 *
 * Lifetime: data_dir's Str is allocated once at startup against the
 * caller-provided allocator (in the engine, gamedata_arena). On
 * commit from the Settings UI, str_clear + str_append_cstr reuse the
 * existing allocation when the new value fits; growth is event-driven
 * and bounded, not per-frame, so any orphaned bytes left in the arena
 * are negligible across a session.
 *
 * The defaults come from preferences_init_defaults() and match the
 * existing pre-prefs constants:
 *   Linux/desktop      : "data/"
 *   Android            : "/storage/emulated/0/Sync/sleipner/"
 *
 * Loading rules (preferences_load):
 *   - Missing file → returns true, prefs unchanged (defaults remain).
 *   - Parse failure → returns false with an error chain set.
 *   - Successful parse → overlays present fields onto prefs. Fields not
 *     present in the file keep whatever value prefs already held (the
 *     defaults).
 *
 * Saving rules (preferences_save):
 *   - Always overwrites the file with the full current preference set.
 *   - The caller is responsible for ensuring the parent directory exists
 *     (use platform_ensure_parent_dir before the first call). */

typedef struct {
    Str data_dir;
} Preferences;

/* Initialize prefs to the platform's compile-time defaults. data_dir
 * is allocated against `alloc`. Subsequent commits to data_dir reuse
 * the same allocation. */
void preferences_init_defaults(Preferences *prefs, Allocator alloc);

/* Load preferences from `path`. Missing file returns true with prefs
 * untouched. Parse errors return false with `err` set. */
[[nodiscard]] bool preferences_load(Preferences *prefs, ErrorState *err, const char *path);

/* Write preferences to `path`. Caller must have ensured the parent
 * directory exists. */
[[nodiscard]] bool preferences_save(const Preferences *prefs, ErrorState *err, const char *path);

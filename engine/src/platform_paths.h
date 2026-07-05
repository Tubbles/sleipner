#pragma once

#include "alloc.h"
#include "error.h"
#include "str.h"

/* Resolve the path to preferences.toml on the current platform.
 *
 * Hierarchy, first match wins:
 *   1. <binary_dir>/preferences.toml — if it exists, return that. Lets
 *      portable installs and dev builds carry their own preferences next
 *      to the binary without touching $HOME or %APPDATA%.
 *   2. OS-conventional per-user config dir, regardless of whether the file
 *      exists yet:
 *        Linux/BSD : $XDG_CONFIG_HOME/sleipner/preferences.toml
 *                    (fallback $HOME/.config/sleipner/preferences.toml)
 *        Windows   : %APPDATA%/sleipner/preferences.toml
 *        Android   : <internalDataPath>/preferences.toml
 *                    (raylib's GetApplicationDirectory() returns this on
 *                    PLATFORM_ANDROID)
 *
 * The returned Str is allocated against `alloc`. The function does not
 * touch the filesystem beyond a single FileExists check for the
 * binary-adjacent override; in particular it does NOT create the parent
 * directory. Use platform_ensure_parent_dir() before writing the file. */
[[nodiscard]] bool platform_preferences_path(Str *out, Allocator alloc, ErrorState *err);

/* Create the parent directory of `path`, including any intermediate
 * directories (mkdir -p semantics, delegating to raylib MakeDirectory).
 * `alloc` is used for the scratch Str that holds the parent path; pass
 * a scratch arena. Returns true if the directory now exists or already
 * existed. */
[[nodiscard]] bool platform_ensure_parent_dir(const char *path, Allocator alloc, ErrorState *err);

/* Resolve the directory game saves live in on the current platform:
 *   Linux/BSD : $XDG_DATA_HOME/sleipner/saves/
 *               (fallback $HOME/.local/share/sleipner/saves/)
 *   Windows   : %APPDATA%/sleipner/saves/
 *   Android   : <internalDataPath>/saves/
 *               (raylib's GetApplicationDirectory() returns this on
 *               PLATFORM_ANDROID; no "sleipner" segment is inserted,
 *               matching platform_preferences_path's Android case,
 *               since internalDataPath is already per-app)
 *
 * Unlike platform_preferences_path there is no binary-adjacent portable
 * override — saves are always app-local runtime state, never shipped
 * alongside the binary. The returned Str is allocated against `alloc`
 * and always ends with a trailing '/', matching how Preferences.data_dir
 * is composed. This function only composes the path; it does NOT touch
 * the filesystem. Use platform_ensure_saves_dir() to create it. */
[[nodiscard]] bool platform_saves_dir(Str *out, Allocator alloc, ErrorState *err);

/* Create `dir_path` (typically the Str produced by platform_saves_dir),
 * including any intermediate directories, delegating to raylib
 * MakeDirectory. Unlike platform_ensure_parent_dir this takes the
 * directory to create directly rather than stripping a basename off a
 * file path — platform_saves_dir already returns a directory, not a
 * file. Safe to call when the directory already exists. Returns true if
 * the directory now exists or already existed. */
[[nodiscard]] bool platform_ensure_saves_dir(const char *dir_path, ErrorState *err);

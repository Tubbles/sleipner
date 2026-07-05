#include "platform_paths.h"

#include "alloc.h"
#include "error.h"
#include "raylib.h"
#include "str.h"
#include "strv.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PREFERENCES_FILENAME "preferences.toml"
#define APP_DIR_NAME "sleipner"
#define SAVES_DIR_NAME "saves"

/* Append "/<segment>" (or "/<segment>/<rest>") to `out`. Caller must have
 * already initialized `out` via str_new with the desired allocator. */
static bool append_path_segment(Str *out, const char *segment)
{
    if (out->len > 0 && out->ptr[out->len - 1] != '/') {
        if (!str_append_cstr(out, "/")) {
            return false;
        }
    }
    return str_append_cstr(out, segment);
}

/* Strip the trailing "/<basename>" from `path` into `out`. Returns false
 * if `path` has no '/' (i.e. caller passed a bare filename — the
 * "directory" is then implicitly the cwd, which we don't materialize). */
static bool parent_dir(const char *path, Str *out)
{
    const char *last_slash = strrchr(path, '/');
    if (last_slash == nullptr) {
        return false;
    }
    Strv parent = (Strv){.ptr = path, .len = (size_t)(last_slash - path)};
    if (parent.len == 0) {
        /* Path was "/something" — parent is "/". */
        return str_append_cstr(out, "/");
    }
    return str_append_strv(out, parent);
}

/* Compose the OS-conventional preferences path into `out`, ignoring
 * whether the binary-adjacent override exists. Caller initialized `out`
 * with str_new(alloc). */
static bool compose_user_config_path(Str *out, ErrorState *err)
{
#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata == nullptr || appdata[0] == '\0') {
        error_set(err, "platform_preferences_path: APPDATA is not set");
        return false;
    }
    if (!str_append_cstr(out, appdata)) {
        error_set(err, "platform_preferences_path: out of memory");
        return false;
    }
    if (!append_path_segment(out, APP_DIR_NAME) || !append_path_segment(out, PREFERENCES_FILENAME)) {
        error_set(err, "platform_preferences_path: out of memory");
        return false;
    }
    return true;
#elif defined(__ANDROID__)
    /* On Android, raylib resolves GetApplicationDirectory() to
     * android_app->internalDataPath at startup. Use that as the
     * canonical per-user config dir. */
    const char *base = GetApplicationDirectory();
    if (base == nullptr || base[0] == '\0') {
        error_set(err, "platform_preferences_path: GetApplicationDirectory returned empty");
        return false;
    }
    if (!str_append_cstr(out, base)) {
        error_set(err, "platform_preferences_path: out of memory");
        return false;
    }
    if (!append_path_segment(out, PREFERENCES_FILENAME)) {
        error_set(err, "platform_preferences_path: out of memory");
        return false;
    }
    return true;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] != '\0') {
        if (!str_append_cstr(out, xdg)) {
            error_set(err, "platform_preferences_path: out of memory");
            return false;
        }
    } else {
        const char *home = getenv("HOME");
        if (home == nullptr || home[0] == '\0') {
            error_set(err, "platform_preferences_path: neither XDG_CONFIG_HOME nor HOME is set");
            return false;
        }
        if (!str_append_cstr(out, home) || !str_append_cstr(out, "/.config")) {
            error_set(err, "platform_preferences_path: out of memory");
            return false;
        }
    }
    if (!append_path_segment(out, APP_DIR_NAME) || !append_path_segment(out, PREFERENCES_FILENAME)) {
        error_set(err, "platform_preferences_path: out of memory");
        return false;
    }
    return true;
#endif
}

/* Compose the OS-conventional saves directory into `out`, mirroring
 * compose_user_config_path but rooted at the platform DATA dir instead
 * of the config dir, and ending in the "saves" subdirectory with a
 * trailing '/'. Caller initialized `out` with str_new(alloc). */
static bool compose_user_data_path(Str *out, ErrorState *err)
{
#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata == nullptr || appdata[0] == '\0') {
        error_set(err, "platform_saves_dir: APPDATA is not set");
        return false;
    }
    if (!str_append_cstr(out, appdata)) {
        error_set(err, "platform_saves_dir: out of memory");
        return false;
    }
    if (!append_path_segment(out, APP_DIR_NAME) || !append_path_segment(out, SAVES_DIR_NAME) ||
        !append_path_segment(out, "")) {
        error_set(err, "platform_saves_dir: out of memory");
        return false;
    }
    return true;
#elif defined(__ANDROID__)
    /* Same as compose_user_config_path's Android case: internalDataPath
     * is already app-scoped, so no extra APP_DIR_NAME segment. */
    const char *base = GetApplicationDirectory();
    if (base == nullptr || base[0] == '\0') {
        error_set(err, "platform_saves_dir: GetApplicationDirectory returned empty");
        return false;
    }
    if (!str_append_cstr(out, base)) {
        error_set(err, "platform_saves_dir: out of memory");
        return false;
    }
    if (!append_path_segment(out, SAVES_DIR_NAME) || !append_path_segment(out, "")) {
        error_set(err, "platform_saves_dir: out of memory");
        return false;
    }
    return true;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg != nullptr && xdg[0] != '\0') {
        if (!str_append_cstr(out, xdg)) {
            error_set(err, "platform_saves_dir: out of memory");
            return false;
        }
    } else {
        const char *home = getenv("HOME");
        if (home == nullptr || home[0] == '\0') {
            error_set(err, "platform_saves_dir: neither XDG_DATA_HOME nor HOME is set");
            return false;
        }
        if (!str_append_cstr(out, home) || !str_append_cstr(out, "/.local/share")) {
            error_set(err, "platform_saves_dir: out of memory");
            return false;
        }
    }
    if (!append_path_segment(out, APP_DIR_NAME) || !append_path_segment(out, SAVES_DIR_NAME) ||
        !append_path_segment(out, "")) {
        error_set(err, "platform_saves_dir: out of memory");
        return false;
    }
    return true;
#endif
}

bool platform_saves_dir(Str *out, Allocator alloc, ErrorState *err)
{
    *out = str_new(alloc);
    return compose_user_data_path(out, err);
}

bool platform_preferences_path(Str *out, Allocator alloc, ErrorState *err)
{
    /* Step 1: probe for binary-adjacent override. */
    const char *bin_dir = GetApplicationDirectory();
    if (bin_dir != nullptr && bin_dir[0] != '\0') {
        Str adjacent = str_new(alloc);
        if (!str_append_cstr(&adjacent, bin_dir) || !append_path_segment(&adjacent, PREFERENCES_FILENAME)) {
            str_free(&adjacent);
            error_set(err, "platform_preferences_path: out of memory");
            return false;
        }
        if (FileExists(adjacent.ptr)) {
            *out = adjacent;
            return true;
        }
        str_free(&adjacent);
    }

    /* Step 2: OS-conventional path. */
    *out = str_new(alloc);
    return compose_user_config_path(out, err);
}

/* Create `dir_path` if it doesn't already exist, including any
 * intermediate directories. Shared by platform_ensure_parent_dir and
 * platform_ensure_saves_dir. */
static bool ensure_dir_exists(const char *dir_path, ErrorState *err)
{
    if (DirectoryExists(dir_path)) {
        return true;
    }
    /* MakeDirectory returns 0 on success per raylib.h:1160, including
     * when intermediate directories must be created. */
    int result = MakeDirectory(dir_path);
    if (result != 0) {
        error_set(err, "MakeDirectory(%s) failed (rc=%d)", dir_path, result);
        return false;
    }
    return true;
}

bool platform_ensure_parent_dir(const char *path, Allocator alloc, ErrorState *err)
{
    Str parent = str_new(alloc);
    if (!parent_dir(path, &parent)) {
        /* No parent component (bare filename) — current directory is
         * already valid; nothing to create. */
        str_free(&parent);
        return true;
    }
    bool success = ensure_dir_exists(parent.ptr, err);
    str_free(&parent);
    return success;
}

bool platform_ensure_saves_dir(const char *dir_path, ErrorState *err)
{
    return ensure_dir_exists(dir_path, err);
}

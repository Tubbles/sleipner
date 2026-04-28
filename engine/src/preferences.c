#include "preferences.h"

#include "alloc.h"
#include "error.h"
#include "str.h"
#include "toml.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOML_PREFS_ERRBUF 200

#if defined(__ANDROID__)
#define DEFAULT_DATA_DIR "/storage/emulated/0/Sync/sleipner/"
#else
#define DEFAULT_DATA_DIR "data/"
#endif

void preferences_init_defaults(Preferences *prefs, Allocator alloc)
{
    prefs->data_dir = str_new(alloc);
    (void)str_append_cstr(&prefs->data_dir, DEFAULT_DATA_DIR);
}

/* Apply a parsed string field into a Str, freeing the parser-owned
 * heap copy that tomlc99 returns. The Str's existing allocation is
 * reused when the new value fits (bounded-growth pattern documented
 * in DESIGN.md). */
static bool apply_string_field(Str *out, toml_datum_t parsed)
{
    if (!parsed.ok) {
        return true;
    }
    str_clear(out);
    bool appended = str_append_cstr(out, parsed.u.s);
    free(parsed.u.s); /* tomlc99 returns malloc'd strings; we own them now */
    return appended;
}

bool preferences_load(Preferences *prefs, ErrorState *err, const char *path)
{
    if (path == nullptr) {
        return true;
    }
    /* Probe with stat first: missing file is the common path on first
     * launch and degrades to "defaults remain". */
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        return true;
    }
    FILE *file = fopen(path, "re");
    if (file == nullptr) {
        error_set(err, "fopen(%s): %s", path, strerror(errno));
        return false;
    }
    char errbuf[TOML_PREFS_ERRBUF];
    toml_table_t *root = toml_parse_file(file, errbuf, (int)sizeof(errbuf));
    (void)fclose(file);
    if (root == nullptr) {
        error_set(err, "toml_parse_file(%s): %s", path, errbuf);
        return false;
    }
    toml_table_t *paths = toml_table_in(root, "paths");
    if (paths != nullptr) {
        if (!apply_string_field(&prefs->data_dir, toml_string_in(paths, "data_dir"))) {
            toml_free(root);
            error_set(err, "preferences_load(%s): out of memory writing data_dir", path);
            return false;
        }
    }
    toml_free(root);
    return true;
}

bool preferences_save(const Preferences *prefs, ErrorState *err, const char *path)
{
    FILE *file = fopen(path, "we");
    if (file == nullptr) {
        error_set(err, "fopen(%s): %s", path, strerror(errno));
        return false;
    }
    /* TOML basic strings cannot contain raw backslashes or quotes
     * unescaped. data_dir paths are filesystem paths under user control;
     * the path picker keeps them ASCII-only and the TOML form here uses
     * a single-line basic string, so a forward slash is the only path
     * separator we ever emit. */
    int written = fprintf(file, "[paths]\ndata_dir = \"%s\"\n", prefs->data_dir.ptr ? prefs->data_dir.ptr : "");
    int close_rc = fclose(file);
    if (written < 0) {
        error_set(err, "fprintf(%s): write failed", path);
        return false;
    }
    if (close_rc != 0) {
        error_set(err, "fclose(%s): %s", path, strerror(errno));
        return false;
    }
    return true;
}

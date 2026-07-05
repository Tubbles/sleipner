#pragma once

#include "alloc.h"
#include "diag.h"
#include "error.h"
#include "game.h"
#include "progression.h"
#include "str.h"
#include "undo.h"

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

/* Callback save_load uses to load the level named by a save's
 * current_level_name into state->gamedata.current_level. Structurally
 * identical to frame.h's LevelLoaderFn (same four parameter types), declared
 * fresh here rather than reused so save.h does not have to pull in frame.h's
 * much heavier include list (editor/editor.h, menu.h, settings.h, ...) --
 * production wires production_level_loader and tests wire test_level_loader
 * into this parameter without a cast, since C function pointer types are
 * structurally compatible. */
typedef bool (*SaveLevelLoaderFn)(Diag *diag, GameState *state, const char *level_name, void *user_data);

/* S6.15d1, D33 (the file+autosave+apply mechanism; the pause-menu Save/Load
 * slot picker is a later slice, S6.15d2).
 *
 * Capture the CURRENT level's delta (progression_capture_level_delta, see
 * progression.h) so in-progress gameplay changes (opened chests, moved
 * entities, defeated enemies, the player's own position) are included,
 * bundle the result with state->progression into a SaveState, serialize it,
 * back up `path` if it already exists (file_backup.h's backup_file), and
 * write the result to `path` via raylib's SaveFileText. Sets diag->error and
 * returns false on any serialize or I/O failure -- `path` is left as
 * whatever it was before the call (unwritten on failure; backup_file itself
 * never touches `path`, only `path`.bak). */
[[nodiscard]] bool save_write(Diag *diag, GameState *state, const char *path);

/* Load `path`, deserialize it, and apply it onto `state`: replace
 * state->progression (flags/vars/items/level_deltas) with the loaded values
 * (deserialized directly into progression_arena, see save.c), load the
 * saved current_level via `level_loader_fn`, re-apply that level's delta
 * (progression_apply_level_delta) so the saved player position and entity
 * state are restored, snap the camera, and clear + rebaseline
 * `undo_history` the same way every other reload path does (frame.c's
 * run_transition_swap, main.c's reset_editor_after_reload) -- old undo
 * snapshots reference arena bytes from before the load and must not be
 * replayed against it.
 *
 * Returns false and sets diag->error, without modifying `state`, if `path`
 * cannot be read (LoadFileText failure -- covers a missing file) or its
 * contents fail to parse (save_deserialize error). If `path` parses
 * successfully but names a level no longer present in the game's data (an
 * edited-since-save gamedata.toml) or `level_loader_fn` is nullptr,
 * `level_loader_fn` is not treated as having succeeded and this function
 * returns false too, without having committed the parsed progression onto
 * `state` -- `state->progression` is left as it was; `state->gamedata` may
 * already reflect game_load_gamedata's own partial-failure behavior in that
 * one case, same as any other reload failure (see game_load_gamedata). */
[[nodiscard]] bool save_load(Diag *diag,
                             GameState *state,
                             const char *path,
                             SaveLevelLoaderFn level_loader_fn,
                             void *level_loader_user_data,
                             UndoHistory *undo_history);

/* Autosave helper (S6.15d1, D33): compose the platform saves directory
 * (platform_paths.h's platform_saves_dir), ensure it exists
 * (platform_ensure_saves_dir), and save_write to "<saves_dir>autosave.toml".
 * Matches frame.h's AutosaveFn signature so production wires this directly
 * into FrameContext.autosave_fn (called from frame.c's run_transition_swap)
 * and main.c's quit path calls it directly too -- both call sites treat a
 * false return as non-fatal (log + continue), never propagating the
 * failure into the transition or the exit sequence. Headless-safe by
 * construction: if platform_saves_dir can't resolve (no HOME/XDG_DATA_HOME
 * in a stripped-down environment), this returns false without touching the
 * filesystem or crashing -- it never assumes a saves directory exists. */
[[nodiscard]] bool save_autosave(Diag *diag, GameState *state);

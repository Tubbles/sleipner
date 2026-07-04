# Sleipner TODO

## Architecture audit findings

- **Editor test coverage — Phase 4+5** — 176 unit tests across 6 files (draw 14,
  core 30, attr 26, widgets 64, child 20, blueprint 22). Still missing:
  integration tests for full editor workflows (select → edit → confirm), draw
  function testing.

## Type safety cleanup

- **Replace pre-resolved `entity_defaults[]` parallel array** — rule.c currently
  receives a `const AttrSet *const *entity_defaults` array (indexed by vec
  position) that game.c pre-builds before each rule evaluation. This couples
  rule contexts to vec indices and forces `resolve_target` callers to do pointer
  subtraction. Two approaches, no preference between them — pick whichever fits
  each case: **(a) Decompose at the boundary (option 4):** game.c builds an
  enriched view struct (e.g.
  `EntityView { Entity *entity; const AttrSet *defaults; }`) and passes an array
  of those to rule.c. Resolution happens once at the call boundary, rule.c just
  reads plain data. **(b) Callback / ops struct (option 1):** pass a resolver
  struct (function pointer + `void *` state) that rule.c calls through without
  knowing about GameState or blueprints. Better when resolution must happen
  lazily or the set of entities isn't known up front. Note: handles (option 3 —
  storing a lookup key on the object) only make sense when the association is
  intrinsic to the type. Entity→blueprint is not guaranteed (dynamic entities
  may have no blueprint), so a null handle would be a code smell. Prefer options
  1 or 4 here.
- **AttrSet value type rethink** — consider what AttrValue should support beyond
  float/int/bool/string: entity handles, blueprint handles? (D15:
  `attr_get_scoped_int` is now strict, returning the fallback on a float-typed
  attribute instead of truncating; `attr_get_scoped_float`'s int-to-float
  promotion is intentional and documented in `attribute.c`. Deferred per D15:
  per-key `debug_log` on mismatch, since the getter is a pure hot-path
  function with no `DebugState` to thread through.)

## Editor: missing top-level modes

Phase 6 of the keybinding audit (`work/keybinding-audit.md`) enumerated five
editor modes that DESIGN.md specifies but the engine does not yet implement.
The keybinding registry is now the place to plug each one in — each item
below is a new set of submodes plus their handlers, pickers, and hint tables.

- **Tile mode** (DESIGN.md §26) — `EDITOR_SUB_TILE_PAINT`,
  `EDITOR_SUB_TILE_PALETTE` (radial/scroll picker of tile kinds), ground /
  overlay layer toggle.
- **Atlas mode** (§27) — `EDITOR_SUB_ATLAS_BROWSE`,
  `EDITOR_SUB_ATLAS_REGION_EDIT` (source-rect drag, like HANDLES but on the
  atlas image).
- **Animation mode** (§28) — `EDITOR_SUB_ANIM_FRAMES` (scrub),
  `EDITOR_SUB_ANIM_EDIT` (frame count / speed via the existing value
  adjuster).
- **Rule mode** (§29, §100-104) — `EDITOR_SUB_RULE_LIST`,
  `EDITOR_SUB_RULE_TRIGGER_PICK`, `EDITOR_SUB_RULE_COND_PICK`,
  `EDITOR_SUB_RULE_ACTION_PICK` (all radial). Reuses existing FUZZY_FINDER
  for flag / item refs.
- **Level mode** (§30) — `EDITOR_SUB_LEVEL_LIST`,
  `EDITOR_SUB_LEVEL_TRANSITIONS` (edit transitions + spawn points), music
  picker (fuzzy finder over embedded assets).

## Editor: missing pickers inside existing modes

- **Level switcher from editor** — swapping levels in the editor currently
  requires editing gamedata. Add a "Switch level" entry to the Tools radial
  that opens a scroll picker of level names.

## Collision system follow-ups

- **Editor cannot author or edit composite collision shapes.** S4.5 added
  `[[blueprint.collision]]` TOML parsing/emission and deep-copy into
  `entity->collision_region`, but the in-game editor's collision handles
  (`draw_collision_handles`) still only drag primitive 0 of the one-rect
  fallback. Authoring a multi-primitive composite (rect+circle+triangle) is
  TOML-only today. A visual editor for this would need a primitive list UI
  (add/remove/select kind) plus per-kind drag handles.

## Input system future work

- **Plugin-declared actions.** When a plugin/engine system arrives,
  bindings need a parallel registry alongside the central enum.
- **Per-gamepad binding.** AtomicInput.int_b stores a gamepad id but
  the entire codebase still assumes gamepad 0. Settings UI does not
  expose a gamepad selector. Defer until the engine actually supports
  multiple gamepads.

## misc
- for some reason, when running against the walls significantly warps the
  sprite. could be related to float position not scaling up correctly or other
  scaling issue.
- the radial menu is hard to use using a keyboard, not all directions are easily representable

## Pause menu follow-ups

Carried over from the pause-overlay menu landing:

- **`blur_resize` is implemented but never called (moot today).**
  Verified 2026-07-03: the window is created non-resizable (no
  `FLAG_WINDOW_RESIZABLE`; fullscreen or fixed-size) and `game_bounds`
  plus the scene render texture are computed once at startup and never
  recomputed, so `IsWindowResized()` cannot fire and `blur_resize`
  would be a no-op (the blur is `game_bounds`-sized and scales with the
  scene). This only becomes meaningful once a resizable-window feature
  exists, at which point the resize path should recompute
  `game_bounds`/the render texture, set `menu->blur_captured = false`
  (and the settings sibling), then call `blur_resize`. Function kept
  for that future.
- **Engine-lib text assets can't use `embed_asset()`.** The blur
  shader source had to live as a C string literal in `blur.c` because
  `embed_*_start` symbols are only resolved on the `sleipner`
  executable target via `embed_all_assets`, and `engine_tests` does
  not provide them. Adding more shaders or GLSL fragments to engine
  code will keep hitting this. Options: provide stub symbols on the
  test target, restructure `embed_all_assets` to apply to any target
  that links the engine lib, or formalise inline C strings as the
  engine-lib-internal convention for text assets and document it.

## Possible action items

Carried over from the Settings tabs + path picker series (commits
d890de8 through 3641352).

- **`preferences_load` doesn't normalize trailing slash on
  `data_dir`.** The Settings UI commit path appends `/` when missing
  (see `path_edit_commit` in `engine/src/settings.c`), but
  `preferences_load` in `engine/src/preferences.c` does not. A user
  manually editing `preferences.toml` to `data_dir = "data"` will
  make `gamedata_path()` compose `datagamedata.toml`. One-line fix
  at the end of `preferences_load`.
- **No path validation on commit.** `path_edit_commit` accepts any
  string the user types; failure surfaces only at the next gamedata
  reload via the existing fopen/stat error path. Out of scope in
  v1, but the path picker could flag obviously-bad paths
  (non-existent, not a directory, not writable) before exiting the
  screen and offer the user a chance to fix it.
- **TestGame can't verify on-disk preferences write.**
  `frame_ctx.preferences_save_fn = nullptr` in
  `engine/test/test_helpers.c::test_game_setup_with_level`, so
  `test_integration_settings_path_edit_commit` can only assert
  `save_preferences_requested` was raised + consumed. Add a
  settable fake save fn (mirror of how `KeybindingsSaveFn` would
  need similar treatment) so tests can assert that the actual
  preferences.toml write happened.
- **Browse mode swallows `LoadDirectoryFilesEx` failures.** A
  permission-denied or stat-failed target directory shows an empty
  list with no explanation (`path_edit_refresh` in
  `engine/src/settings.c`). Surface a toast or an explicit "(no
  permission)" row.
- **Hardcoded filenames in path helpers.** `gamedata_path` /
  `keybindings_path` / `trace_log_path` in `engine/src/main.c` bake
  the filename into the helper. If per-file overrides are wanted
  later (separate `gamedata_dir`, `keybindings_dir`, `trace_dir`),
  that's where to start.
- **General tab `NAV_DOWN` clamp is dead code.**
  `handle_general_tab_input` in `engine/src/settings.c` has
  `general_index < GENERAL_TOTAL_ROWS - 1` which is always false
  with `GENERAL_TOTAL_ROWS == 1`. Currently NOLINTNEXTLINE'd; the
  suppression goes away naturally when the second General row
  lands.
- **Desktop `trace.log` path migrated.** Pre-refactor the desktop
  trace lived at cwd; post-refactor it resolves to
  `<data_dir>/trace.log` (so `data/trace.log` by default). Anyone
  with tooling pointed at the old location needs to update or wire
  up a CLI/env override that reads the boot-stage path.
- **Future top-level `.c` files using `M_PI` will break MinGW.**
  The engine library's `_GNU_SOURCE` opt-in is gated on `!WIN32`,
  so MinGW does not see `M_PI` from `<math.h>`. `keyboard_widget.c`
  hit this in commit 626b0e4 and was hot-fixed with a local
  `KB_PI` constant in 2642e6d. `engine/src/math_consts.h` now
  provides a shared `SLEIPNER_PI` constant for this;
  `keyboard_widget.c` has been migrated to it. `editor/widgets.c`
  still uses `M_PI` via the fallback `#define` in
  `editor/internal.h`, which is fine for editor code, but new
  top-level `.c` files should use `SLEIPNER_PI` instead of `M_PI`.
- **DRIVE_SELECT on POSIX shows a single `/` entry.** The user
  picker is functional but degenerate on Linux/Android, where the
  OS has no drive concept. On Android in particular, useful roots
  are `/storage/emulated/0/`, `/sdcard`, and the app-specific
  `getExternalFilesDir()` path. Populate those into
  `path_edit_populate_drives` (POSIX branch in
  `engine/src/settings.c`) so the row also serves as a "jump to
  common Android root" shortcut, gated on `__ANDROID__`.
- **Path picker has no manual edit field on top.** The user can
  switch into KEYBOARD mode to type a path, but cannot directly
  edit the buffer line shown above the browse list — they have to
  delete to empty and rebuild via the radial. A simple in-place
  text-edit cursor on that line (or a "press CONFIRM on the buf
  display row to enter edit") would shave clicks off, especially
  on desktop where a real keyboard is hooked up.

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
editor modes that DESIGN.md specifies but the engine did not yet implement.
Level mode (§30), Tile mode (§26), Atlas mode (§27), Animation mode (§28),
and Rule mode (§29) are now done — see the follow-up bullets below for the
gaps each one left open (Level mode's music picker, Tile mode's autotiling,
Atlas mode's fuzzy-finder integration, Rule mode's if_else/for_each
predicate rows and MOVE reparenting).

- **Tile mode autotiling** (DESIGN.md §26, D36) — S5.3b shipped manual
  concrete-tile-id painting (`EDITOR_SUB_TILE_PAINT`/`_PALETTE`,
  `editor/tile.c`): a cursor over the ground/overlay grid, CONFIRM paints
  the palette-picked tile id, EDITOR_DELETE erases. D36's "Autotiling
  (automatic edge/corner sprite selection) is an editor feature" line is
  still open — the file format already stores plain concrete tile ids, so
  autotiling would live entirely in the editor's placement logic with no
  further engine-side changes. Not attempted in S5.3b.
- **Atlas mode fuzzy-finder integration** (§27, D37) — S5.4a shipped the
  engine-side named-atlas-region system; S5.4b shipped the editor mode
  (`EDITOR_SUB_ATLAS_BROWSE`, `EDITOR_SUB_ATLAS_REGION_EDIT`,
  `editor/atlas.c`): browse textures from the runtime registry, list/create
  named regions per texture, drag-set a region's `src` via the same
  dual-stick math as HANDLES. Authoring `sprite = "name"` on a blueprint
  already works today through the generic string-attribute system (ADD ->
  word builder, no new code needed) but the fuzzy finder
  (`fuzzy_finder_build_items`, `editor/widgets.c`) doesn't yet suggest
  existing atlas region names as candidates when picking that value, so
  there's no autocomplete/browse-by-name for it — purely additive UX, not a
  round-trip gap.
- **Rule mode** (§29, §100-104) — S5.6a shipped the read-only foundation;
  S5.6b shipped leaf editing (`begin_rule_edit_for_row`/
  `dispatch_rule_radial_confirm`/`rule_edit_argument_step_complete`/
  `finalize_rule_edit`, `editor/rule.c`); S5.6c shipped structural editing:
  ACTION_EDITOR_PLACE inserts a new action node (appended into a
  control-flow node's own children when that's what's focused, otherwise
  spliced in right after the focused sibling) and immediately reuses
  S5.6b's ACTION_TYPE radial so the new node gets a real type/argument;
  ACTION_EDITOR_DELETE drops the focused node's index from its containing
  list (roots/children/else_children) without compacting the flat pool —
  the orphaned subtree just stops being emitted; ACTION_EDITOR_MOVE_UP/DOWN
  (new chords, `[Ctrl/L1, Up/Down]`) reorder a node among its siblings
  (`insert_rule_action_node`/`delete_rule_action_node`/
  `move_rule_action_node`, `editor/rule.c`); S5.6d shipped subroutines: the
  blueprint picker's trailing "Subroutines" row switches
  `EDITOR_SUB_RULE_LIST` to a list over `gamedata.subroutines` (name + action
  count, "+ NEW SUBROUTINE", `ACTION_EDITOR_DELETE`), and each subroutine's
  action tree reuses the exact same `EDITOR_SUB_RULE_TREE` editor as a rule's
  — `rule_tree_flatten`/`rule_tree_row_count` and every S5.6b/c leaf/
  structural-edit function were generalized to a `RuleTreeTarget`/
  `RuleTreeTargetConst` (trigger+conditions+ActionTree for a Rule, ActionTree
  alone for a Subroutine) resolved by `rule_current_target`/
  `rule_current_target_mut` (`editor/rule.c`, `editor/internal.h`). Rule mode
  is now feature-complete per the S5.6 brief. Remaining gaps: (1)
  `RuleTreeRow` (`editor/internal.h`) still only flattens rule-level
  conditions, not a control-flow node's own predicate
  (`ActionNode.conditions`) — an if_else row's CONFIRM is a no-op, and
  for_each's CONFIRM only edits its bind name, since there's no row yet to
  edit either node type's predicate through; (2)
  `create_timer`/`create_timer_periodic`'s duration and
  `set_attr`/`add_attr`'s value are edited as free strings (word builder +
  gamepad keyboard for digits) rather than through the numeric adjuster,
  since the brief for S5.6b only called out repeat-count and condition
  compare_value as adjuster fields; (3) S5.6c's MOVE only reorders a node
  among its existing siblings — promoting a node out of an if_else's
  then/else list into its parent's list (or the reverse) once the cursor
  runs off the end of its current siblings needs a design call (which
  branch is on the receiving/donating end isn't determined by "direction"
  alone) that the S5.6c brief left for a future slice to resolve.
- **Animation mode's sparse-attr round trip** (§28, D20) —
  `emit_animation_if_present` (`engine/src/toml_emitter.c`) emits all four
  `animation = {...}` fields as soon as any one of
  `anim_frames`/`anim_size`/`anim_speed`/`anim_row` exists, defaulting the
  untouched ones to 0 for emission. Bumping a single field via the
  Animation editor (`editor/anim.c`, S5.5) therefore writes `size = 0` /
  `speed = 0` / `row = 0` for the sibling fields into the saved TOML, and
  `parse_animation` re-reads them as real 0-valued attrs on reload — below
  the editor's own `>= 1` floor for `anim_size`, since parsing bypasses
  `anim_edit_clamp`. Pre-existing S3.4 emitter/parser gap, not introduced
  by S5.5, but the row-by-row adjuster is the first UI path that naturally
  invites touching only one of the four fields. Not attempted in S5.5;
  would need `emit_animation_if_present` to track which fields were
  authored vs defaulted, or the editor to eagerly set all four attrs on
  first touch.
- **Level mode music picker** — S5.2b's detail-row editor lets you type an
  arbitrary string into `music` via the word builder (`editor/level.c`
  `enter_level_detail_string_edit`); the master plan (S5.2, S6.13) always
  intended a fuzzy-finder over an embedded music-name registry once that
  registry exists. Revisit when S6.13 (audio polish) lands.
- **DESIGN.md's "Editor Controls (Draft)" table is stale for multi-select
  and grid-snap (DESIGN.md:121-122).** S5.7 (D38) implemented multi-select-
  add as L1+CONFIRM / Ctrl+Enter and the grid-snap toggle as L1+Up /
  Ctrl+Up (`default_editor_multiselect_add`/`default_editor_grid_snap_toggle`,
  `input_func.c`), not the draft table's "LT (hold)" / "RT + D-pad up" — R1
  and L2 were already independently bound to actions checked every frame in
  Scene Browse (`ACTION_EDITOR_PLACE`, `ACTION_EDITOR_WATCH`), so chording
  either would fire that action alongside the new gesture. The Undo/
  Duplicate rows in the same table were already stale before this slice.
  Revisit the whole table if it's ever promoted out of "Draft."

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

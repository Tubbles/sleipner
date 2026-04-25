# Sleipner TODO

## Engineering Goals

- **Vec types for all linear data** — `ActionNode.children` /
  `ActionNode.else_children` still use raw pointers (blocked, see below)

---

## Promote vec types — remaining

- **`ActionNode.children`** / **`ActionNode.else_children`** — raw
  `ActionNode *` + count fields → `vec_action_node` — **Blocked**:
  self-referential struct constraint. `VEC_DECL(action_node, ActionNode)`
  requires `ActionNode` to be complete, but `ActionNode` can't embed
  `vec_action_node` before the vec type is declared.

## Architecture audit findings

- **Editor test coverage — Phase 4+5** — 176 unit tests across 6 files (draw 14,
  core 30, attr 26, widgets 64, child 20, blueprint 22). Still missing:
  integration tests for full editor workflows (select → edit → confirm), draw
  function testing.
- **`TriggerEventQueue` uses fixed array** — `rule.h` defines
  `events[MAX_CASCADE_EVENTS]` instead of a vec. Violates "prefer vec over
  fixed-size arrays with MAX\_\* constants" in CLAUDE.md.

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
  float/int/bool/string: entity handles, blueprint handles? Current int↔float
  coercion in `attr_get_scoped_*` silently papers over type mismatches — should
  probably be strict, with the parser deciding canonical types.

## Editor index caching

- **Replace cached entity/attr indices with ID-based getters** —
  `EditorState.selected_entity_index`, `selected_attr_index`, and
  `WatchList.entity_indices[]` store raw vec indices into gamedata. These are a
  form of caching over a getter function and require manual invalidation on
  undo/redo, hot-reload, and entity deletion. Store stable identifiers (entity
  IDs) instead and resolve via getter functions, eliminating the invalidation
  edge cases entirely.

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

- **Watch-list picker** — `Shift / L2` in BROWSE toggles watch on the focused
  entity but there is no UI to view / clear the full watch set. Add
  `EDITOR_SUB_WATCH_LIST` (scroll picker; A removes, B closes). New Tools
  radial entry.
- **Level switcher from editor** — swapping levels in the editor currently
  requires editing gamedata. Add a "Switch level" entry to the Tools radial
  that opens a scroll picker of level names.

## Editor: small residual keybinding issues

Carried over from `work/keybinding-audit.md`:

- **ATTR_EDIT ±100 labels.** Hint text shows `PgDn/L2: -100 | PgUp/R2: +100`,
  but `L2 / R2` are already bound to `-10 / +10`. Either drop the `L2 / R2`
  labels for ±100 or rebind ±100 to a different gamepad combo.

## misc

- improve integration test framework / ergonomics for black-box bug-repro tests:
  today the editor's button bindings read raylib globals directly and there is
  no headless frame entry point, so driving a bug like "press LEFT in the
  editor, assert the player didn't move" end-to-end is prohibitively painful.
  Make it cheap and idiomatic. See DESIGN.md § "Test ergonomics for black-box
  integration testing" and CLAUDE.md Bug Investigation Discipline rule 3.
- add persisted attrs for children in toml emit and editor ui
- for some reason, when running against the walls significantly warps the
  sprite. could be related to float position not scaling up correctly or other
  scaling issue.
- pressing select to bring up the tools radial menu with a gamepad also triggers a reload
- the radial menu is hard to use using a keyboard, not all directions are easily representable
- full fledged main menu, move some actions to it (save, restore)

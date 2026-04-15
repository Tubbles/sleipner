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

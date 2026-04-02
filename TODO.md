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

- **Editor test coverage — Phase 3** — 62 unit tests (45 pure-logic + 17
  mocked-input via fff.h). Still missing: integration tests for full editor
  workflows (select → edit → confirm), draw function testing.
- **`TriggerEventQueue` uses fixed array** — `rule.h` defines
  `events[MAX_CASCADE_EVENTS]` instead of a vec. Violates "prefer vec over
  fixed-size arrays with MAX_* constants" in CLAUDE.md.

## Rework unit test harness

- **Self-contained test executables** — each `test_*.c` file gets its own
  `main()` and `RUN_TEST()` calls. Remove `main_test.c` entirely.
- **No linking against engine or raylib** — unit test binaries compile only the
  file under test (`#include "../src/foo.c"`) plus test helpers. No linking
  against `libengine.a`, `raylib`, or any other library beyond Unity and libc.
- **Mock all external dependencies** — every function external to the unit under
  test is mocked via fff.h (or a manual stub). This includes raylib, other
  engine modules, and any transitive dependency.
- **Shared custom mock routines** — when a mock needs non-trivial behavior
  (beyond an empty stub / zero return), put the custom fake in a shared helper
  file (e.g. `test_mocks.c`) so it can be reused across test files without
  duplication.
- **Scope: unit tests only** — integration tests remain as-is (linked against
  the full engine, real subsystem interactions).

## Type safety cleanup

- **Replace pre-resolved `entity_defaults[]` parallel array** — rule.c
  currently receives a `const AttrSet *const *entity_defaults` array
  (indexed by vec position) that game.c pre-builds before each rule
  evaluation. This couples rule contexts to vec indices and forces
  `resolve_target` callers to do pointer subtraction. Two approaches,
  no preference between them — pick whichever fits each case:
  **(a) Decompose at the boundary (option 4):** game.c builds an enriched
  view struct (e.g. `EntityView { Entity *entity; const AttrSet *defaults; }`)
  and passes an array of those to rule.c. Resolution happens once at the
  call boundary, rule.c just reads plain data.
  **(b) Callback / ops struct (option 1):** pass a resolver struct
  (function pointer + `void *` state) that rule.c calls through without
  knowing about GameState or blueprints. Better when resolution must happen
  lazily or the set of entities isn't known up front.
  Note: handles (option 3 — storing a lookup key on the object) only make
  sense when the association is intrinsic to the type. Entity→blueprint is
  not guaranteed (dynamic entities may have no blueprint), so a null handle
  would be a code smell. Prefer options 1 or 4 here.
- **AttrSet value type rethink** — consider what AttrValue should support
  beyond float/int/bool/string: entity handles, blueprint handles? Current
  int↔float coercion in `attr_get_scoped_*` silently papers over type
  mismatches — should probably be strict, with the parser deciding canonical
  types.


## misc

- do the same stored-allocator change for map and Str types (parallels vec work)

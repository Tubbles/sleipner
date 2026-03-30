# Sleipner TODO

## Engineering Goals

- **No opaque cross-module forward declarations** — `struct EngineContext;` is
  the last one; eliminate it by splitting the logging/error concern into a
  lightweight header with no upward dependencies (see Coding Style in
  CLAUDE.md). Also investigate cppcheck for a built-in check; if none exists,
  write a small cppcheck Python plugin (lives in this repo) to enforce it
  automatically.
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
- **`nullptr` vs `NULL` inconsistency** — `str.c:17` uses `nullptr` while the
  rest of the codebase uses `NULL`. Minor style issue (see also the `NULL` →
  `nullptr` migration item below).

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

## misc

- remove allocator fallback of NULL -> libc heap from all our data types
- change all NULL -> nullptr since this is C23
- change it so vec and friends store their allocators, makes realloc and free
  very easy and their signatures gets simpler. requires the allocator type to be
  a very simple thing with no/low dependencies

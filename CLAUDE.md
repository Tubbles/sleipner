# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**IMPORTANT:** This document applies to all LLMs working on this project. However, only Mistral LLM (devstral-2) shall also read and follow CONTRIBUTING.md.

**DO NOT default to traditional C idioms.** This is C23 with its own abstractions (`Str`, `vec`, `map`, arenas, `Allocator`). Reaching for `char[]`, `malloc`/`free`, or `MAX_*`-sized arrays is almost always wrong. See "Coding Style" and "Core Types" below.

## Project Overview

Sleipner is a top-down Zelda-like action RPG written in C using raylib. The game is controller-driven and targets Linux x86_64, Windows x86_64 (cross-compiled via MinGW), and native Android arm64 (APK built with Gradle and the NDK, raylib `PLATFORM=Android`). Proton provides Windows-to-Linux translation and FEX provides x86-to-ARM emulation, so the Windows binary can additionally be run on Linux via Proton, or on Android via Proton layered on FEX (for example through the "Game Native" app).

**MANDATORY READING:** `DESIGN.md` contains the game design specification and implementation roadmap. Read it before making any changes to understand the intended architecture and feature set.

## Building

The project uses a Nix flake (`flake.nix`) for the reproducible toolchain and CMake presets (`CMakePresets.json`) for build-target shapes. Enter a dev shell and invoke a workflow preset; there is no `ci.sh` wrapper.

```bash
# Native (Linux desktop) — configure + build + test in one go
nix develop -c cmake --workflow --preset linux --fresh

# Or step by step against the same preset
nix develop -c cmake --preset linux             # configure
nix develop -c cmake --build --preset linux     # build
nix develop -c ctest --preset linux             # test

# Format check / auto-format
clang-format --dry-run --Werror engine/src/*.c engine/src/*.h engine/test/*.c
clang-format -i engine/src/*.c engine/src/*.h engine/test/*.c

# Lint
cd build/Release && clang-tidy -p . $(ls ../../engine/src/*.c ../../engine/test/*.c | grep -v arena_win32)

# Cppcheck forward-decl addon + pytest
cppcheck --enable=warning --addon=tools/cppcheck/no_forward_decl.py --suppress=unknownMacro --error-exitcode=1 engine/src/*.h engine/src/*.c
pytest tools/cppcheck/test_no_forward_decl.py -v

# Windows cross-compile (mingw-w64 via pkgsCross). Tests are skipped: cross binaries can't run on the host.
nix develop .#windows -c cmake --workflow --preset windows --fresh

# Android APK (NDK + Gradle via androidenv) — Gradle-driven, no CMake preset.
nix develop .#android -c bash -c 'cd android && gradle wrapper --gradle-version 8.11.1 && ./gradlew assembleRelease'

# Run (from any shell, inside or outside `nix develop`)
./build/Release/engine/sleipner
```

Preset definitions live in `CMakePresets.json` at the repo root: `linux` builds into `build/Release/`, `windows` into `build/windows/` with the in-tree mingw toolchain. Add a new platform by adding a configure preset, a matching build preset, optionally a test preset, and a workflow preset that chains them.

The Linux build runs `patchelf` over `sleipner` to strip Nix-store paths from `DT_RUNPATH` and point the interpreter at `/lib64/ld-linux-x86-64.so.2`. `engine/src/glibc_compat.h` is force-included everywhere and pins `fmod`/`fmodf` to `GLIBC_2.2.5`, capping the binary's symbol-version floor at `GLIBC_2.35`. Result: the binary runs on any x86-64 Linux with glibc >= 2.36 (bookworm, Ubuntu 22.04, Fedora 36+) and the usual desktop graphics stack, no Nix on the target. See top-level + `engine/CMakeLists.txt` for the wiring.

macOS contributors can use `nix develop` directly; Windows contributors need WSL2.

## Dependencies

- Nix (flake-based toolchain; enter with `nix develop`)
- C23 compiler (clang-22 via `llvmPackages_22` in the flake)
- CMake (invoked directly; no package manager layer)
- raylib (graphics, input, audio — vendored at `engine/vendor/raylib/`)
- Unity (ThrowTheSwitch — unit test framework, vendored at `engine/vendor/unity/`)
- fff.h (Fake Function Framework — mocking, vendored at `engine/vendor/fff/`)
- tomlc99 (TOML parser, vendored at `engine/vendor/tomlc99/`)

## Coding Style

- **No OOP patterns.** This is C — think plain structs and functions.
- **Small, focused functions.** Aim for 5-10 lines per function. Extract logic into named helpers rather than writing long functions.
- **Pure functions where possible.** Functions should take inputs, return outputs, and avoid side effects. Side effects (I/O, rendering, audio) should be pushed to the edges — thin wrapper functions that call pure logic.
- **Data-oriented design.** Game state is plain structs. Logic operates on that data. Data flow is one-directional: input -> state -> render.
- **Test everything with Unity + fff.h.** Every non-trivial pure function should have corresponding tests in `test/`. If a function is hard to test, it probably does too much.
- **Full descriptive names always.** No single-letter variables anywhere, including loop counters (`i` → `index`, `j` → `next`). No small abbreviations either (`pt` → `particle`, `dx` → `delta_x`, `wp` → `world_pos`). The codebase should be self-documenting through clear naming.
- **Vendor libraries go in `engine/vendor/`.** Not the top-level `vendor/`.
- **Prefer `vec` over fixed-size arrays with `MAX_*` constants.** Whenever you need a dynamic collection, reach for a `vec` type backed by the appropriate arena — not a `Type array[MAX_SOMETHING]` with a companion count. Fixed-size arrays are only justified for truly fixed-size data (e.g. a 4-button input state). If you find yourself defining a `MAX_*` constant to size a buffer, stop and ask whether a `vec` fits instead.
- **No opaque cross-module forward declarations.** Never write `struct Foo;` or `typedef struct Foo Foo;` in a header to dodge including the defining header. It hides circular dependencies behind opaque pointers; fix the architecture instead (extract a lower-level definition, inject the dependency, or split the type).
  - **Enforcement:** cppcheck addon `tools/cppcheck/no_forward_decl.py`. Suppress with `// cppcheck-suppress noForwardDecl-noForwardDecl` only for justified exceptions.
  - **Allowed:** self-referential structs within the same file (`struct Node { struct Node *next; }`).

## Core Types: Str, Strv, vec, map

The project has its own string and container types. **Never use raw `char[]` buffers, `malloc`-backed arrays, or hand-rolled hash tables** — use these instead. Full API in the headers listed below.

### Strv — non-owning string view (`engine/src/strv.h`)

A `{const char *ptr, size_t len}` pair. Does not own memory, does not require null-termination (though `strv_from_cstr` produces views of null-terminated strings). Use for read-only string references, function parameters, and temporaries. Key entry points: `strv_from_cstr`, `str_to_strv`, `strv_eq_cstr`, `strv_split`.

`Strv` is safe to store in structs when the backing memory outlives the view (e.g. string literals, arena-backed `Str` data). Be careful with views into scratch arena — they die at `SCRATCH_SCOPE` exit.

### Str — owning, growable string (`engine/src/str.h`)

A `{char *ptr, size_t len, size_t cap}` triple. Always null-terminated. All mutating functions take an `Allocator *` — in engine code, always `allocator_arena(arena)`. Zero-init with `{0}`, then build via `str_from_cstr`, `str_append_cstr`, etc. Borrow as a `Strv` with `str_to_strv`.

In arena-backed code, `str_free` is a no-op (arena reclaims memory on rewind). It exists for test code using heap allocators.

### vec — typed dynamic array (`engine/src/vec.h`)

Code-generated via `VEC_DECL` / `VEC_IMPL` macros (declare in a header after the element type, implement in exactly one `.c` file). Each `vec_<name>` stores `{data, count, capacity, alloc}` — the allocator is stored at creation time and used by all subsequent pushes. Construct with `vec_<name>_new(alloc)`, mutate with `vec_<name>_push` / `vec_<name>_clear`, read via `.data[index]` and `.count`.

Primitive types (`vec_int`, `vec_bool`, `vec_float`, etc.) are pre-declared — just include `vec.h`. Pointer-stability rules live under "Vec growth and pointer stability" below.

### map — typed hash map (`engine/src/map.h`)

Code-generated via `MAP_DECL` / `MAP_IMPL` macros. Open-addressing, power-of-2 capacity, 75% load factor, tombstone deletion. `MAP_IMPL` requires hash and equality functions for the key type. All mutating functions take an `Allocator *` parameter. Zero-init with `{0}` is a valid empty map. Mutate with `map_<name>_set` / `map_<name>_remove`; lookup via `map_<name>_get` returns a pointer (or `nullptr` if missing).

Int-keyed maps (`map_int_bool`, `map_int_int`, `map_int_str`, etc.) are pre-declared with built-in hash/eq — just include `map.h`.

**Critical rule:** `map_get` returns a pointer into the bucket array; any `map_set` that triggers a rehash invalidates it. Re-fetch immediately before use.

## Input Function Layer

All gameplay, editor, menu, and widget code reads input through a high-level **function layer** (`engine/src/input_func.h`) — never raw keys, gamepad buttons, or stick values. Two enums separate discrete actions from analog axes for static type safety:

- **`InputAction`** — discrete events: `ACTION_CONFIRM`, `ACTION_CANCEL`, `ACTION_NAV_*`, `ACTION_PAGE_*`, `ACTION_TAB_PREV`, `ACTION_TAB_NEXT`, `ACTION_INTERACT`, `ACTION_EDITOR_*`, `ACTION_ATTR_*`, `ACTION_BLUEPRINT_*`, `ACTION_WB_*`, `ACTION_MENU_TOGGLE`, `ACTION_FONT_PREVIEW_TOGGLE`, `ACTION_QUIT`. Hybrid naming: shared verbs unprefixed, context-specific actions prefixed. `ACTION_TAB_PREV/NEXT` (gamepad L1/R1, keyboard Shift+Tab/Tab) is the universal tab-switch verb consumed by the Settings overlay; it shares L1/R1 with `ACTION_PAGE_UP/DOWN`, so tabbed UIs check `TAB_*` first and early-return.
- **`InputAxis`** — analog scalars: `AXIS_PRIMARY_X/Y` (left stick / arrows-WASD), `AXIS_SECONDARY_X/Y` (right stick / Q-E), `AXIS_TRIGGER_LEFT/RIGHT`.

Bindings live in a `BindingStore` on `GameState.bindings`, loaded once in `game_init` from baked-in defaults. The store is enum-indexed fixed-size arrays — a justified `MAX_*` exception per the "no MAX_*" rule because there is exactly one binding per enum value.

### API

- `input_pressed(in, store, ACTION_X)` — edge: fires once per press.
- `input_held(in, store, ACTION_X)` — level: true while held.
- `input_axis(in, store, AXIS_X)` — sums alternatives, clamps to `[-1, +1]`.
- `input_axis_pair(in, store, AXIS_X, AXIS_Y)` — paired read with unit-disc clamp.

All four are `[[nodiscard]]`. Use `input_axis_pair` for any (x, y) directional pair (player movement, editor camera pan, entity drag) — it preserves the "diagonals not faster than cardinals" guarantee.

### Chord support

A `PhysicalInput` is a list of one or more atoms (`AtomicInput`). One atom is a single key/button; two or more atoms is a chord (all must be held, one freshly pressed to fire). `ACTION_EDITOR_UNDO` is `[Ctrl, Z]` on keyboard or `[L1, Left]` on gamepad. Cross-device chord protection drops out of the data shape — no separate rule needed.

### Order-sensitivity

The function layer is a **binding lookup, not a priority resolver**. If `ACTION_NAV_LEFT` is bound to `LEFT_FACE_LEFT` and `ACTION_EDITOR_UNDO` is bound to chord `[L1, LEFT_FACE_LEFT]`, then pressing L1+Left fires *both* unless the caller checks UNDO first and early-returns on match. See `editor/core.c handle_browse_input` for the canonical pattern.

### Tests

Construct an `InputState` directly and drive it through the helpers in `input.h`: `input_state_press_key` (edge: sets both `_pressed` and `_down`), `input_state_hold_key` (level: sets `_down` only), `input_state_press_gp_button`, `input_state_set_gp_axis`. Then assert against `input_pressed` / `input_held` / etc. against the test's `BindingStore`.

The legacy `test_input_mock` `--wrap` shim still exists for pre-existing integration tests but should not be the preferred path for new tests.

## Arena Architecture

**ALL engine memory is arena-backed. Using `malloc`, `realloc`, or `free` anywhere in engine code is strictly forbidden — no exceptions, no workarounds, no "just this once".** The only permitted exemptions are: (1) the `NULL`-allocator fallback path inside the allocator infrastructure itself, and (2) `free(datum.u.s)` calls for TOML vendor string datums (a vendor limitation). If you find yourself reaching for `malloc`, the architecture is wrong — restructure to pass an arena allocator instead.

### Arena philosophy

Keep the number of arenas as low as possible, but don't force incompatible lifetimes into a single arena — object lifetimes can cross arena boundaries. Each arena must have a clearly defined lifetime and purpose. Add new arenas consciously and document what lifetime they correspond to.

### Arena growth strategy

The rule that decides whether an allocation is permitted in `gamedata_arena` is about **growth shape**, not absolute size. Allocations whose count grows with frame count `n` (one per frame, one per game tick) are forbidden — they leak forever and the arena grows without bound. Allocations whose count is bounded by user actions or events (settings saves, level loads, blueprint reloads, occasional editor commits) are explicitly fine even if they orphan a previous block, because total leaked bytes are `<events> * <size per event>` — kilobytes at most over a session.

Before adding a new dedicated `Arena` field on `GameState`, ask: does this allocation's count grow with frame count, or is it bounded by user/event count? If bounded, allocate against `gamedata_arena`. If frame-count-proportional, that's a real architectural problem and a new arena is not the right fix either — restructure the data flow.

`Preferences.data_dir` is a worked example: it lives in `gamedata_arena`, and the Settings UI's commit path (`str_clear` + `str_append_cstr`) reuses the existing buffer when the new value fits. Only an unprecedentedly long path triggers an arena bump, and even then the leaked block is bounded by one per session per longest-yet path.

### Current arenas in `GameState`

- **`gamedata_arena`** — all persistent data: blueprints, levels, entity strings, attribute names/values, rules, runtime flags, asset vecs (textures, fonts). On hot-reload/level transition, rewound to `gamedata_base` via `arena_restore` — **not** `arena_reset`. `arena_reset` (which calls `MADV_DONTNEED`) is only for full teardown.
- **`scratch_arena`** — temporaries that must not outlive their enclosing scope. Always used via `SCRATCH_SCOPE` — no exceptions. `arena_restore` does a bare pointer rewind with no `madvise`; scratch memory is reused immediately.

### `gamedata_base` checkpoint

`GameState` holds an `ArenaCheckpoint gamedata_base` that is saved **after** persistent assets (textures, fonts) are loaded into `gamedata_arena` at startup — but **before** any gamedata (blueprints, level, rules) is parsed. Layout:

```
gamedata_arena:
  [0 .. gamedata_base)   ← textures + fonts (survive all reloads, freed only at game exit)
  [gamedata_base .. top) ← blueprints, level, rules (rewound on each reload)
```

`game_load_gamedata` calls `arena_restore(&state->gamedata_arena, state->gamedata_base)` before re-parsing. This preserves the texture/font data and reclaims all gamedata memory without a single `free()`. **Never call `arena_reset` on `gamedata_arena` from game code** — it would wipe the textures and cause a black screen.

### Backing storage

Both arenas reserve 1 TiB via `mmap(MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE)` and demand-page on first write. `MAP_NORESERVE` is required so the reservation succeeds inside containers with strict overcommit.

### `SCRATCH_SCOPE` macro

`SCRATCH_SCOPE(&state.scratch_arena)` saves the arena offset on entry and restores it on any block exit (return, break, goto, fall-through) via `__attribute__((cleanup))`. Every function that allocates into `scratch_arena` must open with `SCRATCH_SCOPE`. No manual `arena_save`/`arena_restore` on `scratch_arena` is permitted.

### Passing allocators

**Always pass `allocator_arena(arena)`.** `Str`, `vec`, and `map` all accept an `Allocator *`. Passing `NULL` is only allowed in test code that manages its own lifetime via `test_*_free` helpers — never in engine code. If you are about to pass `NULL` or call `allocator_heap()` in non-test code, stop and fix the architecture instead.

### Vec growth and pointer stability

When a vec backed by an arena grows, it extends in place only if its backing array is the
**topmost allocation**. Otherwise a new block is allocated at the top, data is copied, and the old
block is orphaned in the arena (valid but unreachable). Two rules follow from this:

- **Never hold a pointer to a vec element across a push to that vec.** `&vec.data[i]` becomes
  stale if a reallocation occurs. Always re-derive via `vec.data[i]` after any push.
- **Build then read.** Finish all pushes to a vec before taking any element pointer. The
  load-time / play-time split naturally satisfies this: vecs grow at load time, are read-only at
  runtime.

For `map<K, vec_V>` two-dimensional structures: prefer storing `vec_V *` (pointer) in the map
rather than `vec_V` (value), so map rehash copies the pointer and not the vec. Use the two-phase
protocol — map keys defined at load time (no runtime rehash), vec contents grown at runtime. See
DESIGN.md § "Vec Growth and Pointer Stability" for the full analysis and the named entity group
store design.

### Undo system

The editor uses snapshot-based undo. `UndoHistory` (in `undo.h`) maintains a doubly-linked
list of `UndoEntry` nodes in a dedicated undo arena. Each entry stores a typed
`GamedataState` copy and the raw `gamedata_arena` bytes from `gamedata_base` to the current
offset. Restore = memcpy arena bytes back + `arena_restore` to the saved checkpoint + assign
the struct copy.

**Key rules for codebase changes:**
- All `GamedataState` pointers must point into `gamedata_arena`. Heap/stack pointers become dangling after undo restore.
- New arena-backed fields belong in `GamedataState`, not `GameState`. Fields outside the sub-struct are not snapshotted.
- Any code path calling `game_load_gamedata` must also call `undo_history_clear` — hot-reload and level transitions invalidate all snapshots.
- Snapshot after mutating (push-after model). Multi-frame ops push at confirm. Single-frame ops push inline after the mutation. A baseline entry is pushed at game load, hot-reload, and level transition.
- `EditorState` is not snapshotted — don't store undo-critical data there.

See DESIGN.md § "Undo System" for the full architecture and safety rules.

## Testing Strategy

Two levels of testing, both run in CI:

### Unit Tests (`test/unit/`)
- Test individual pure functions in isolation.
- Use Unity + fff.h for mocking.
- Fast, no engine initialization needed.
- Example: test collision resolution, attribute lookup, TOML parsing helpers.

### Integration Tests (`test/integration/`)
- Run the full engine in **headless mode** — no window, no rendering, no audio.
- Load real gamedata.toml files (test fixtures in `test/fixtures/`).
- Feed synthetic inputs, advance N frames, assert on game state.
- Exercise subsystem interactions: gamedata loading → entity instantiation → rule evaluation → attribute changes → collision → state transitions.
- Use Unity for assertions, but the setup is a real engine init/teardown cycle.
- Example: load a level with a locked chest, simulate player walking to it, simulate interact input, assert flag is set and item is in inventory.

### Headless Engine Mode
- The engine must support initialization without creating a window or audio device.
- The game loop splits cleanly into `update(state, input, delta_time)` and `render(state)`. Tests only call update.
- Every new subsystem must be testable headlessly. If adding a feature requires a screen to test, the architecture is wrong — refactor until the logic is separable from the rendering.

### Testing Discipline
- **Bugs get tests FIRST, not last.** See "Bug Investigation Discipline" below — every bug report begins with a failing integration test that doubles as the regression guard.

## Bug Investigation Discipline

**READ THIS BEFORE INVESTIGATING ANY BUG REPORT.** Two rules, both non-negotiable:

### 1. Reproduce before hypothesizing — write the test first

The first action on any bug report is a failing integration test in `engine/test/integration_test.c` (or a focused unit test if the bug is purely pure logic). Headless engine, real gamedata, real inputs. **No diagnostic logging, hypotheses, or fixes before the test fails for the reason the user reported.** That test, once green, IS the regression guard — don't write it twice.

If you can't reproduce after reasonable effort, stop and ask the user for specifics (exact input sequence, level, toggle state, surrounding UI). Never fill gaps with guesses.

### 2. User reports are authoritative — never blame the user

Treat what the user says as ground truth. Do **not** construct hypotheses that amount to "the user did X accidentally" or "the user misread what they saw" without first asking them directly.

Before writing any hypothesis into a plan:
- Re-read the report looking for evidence that already falsifies it. Visible side effects the user would have mentioned — toasts, sounds, animations, overlay text, obvious state changes — are strong negative evidence. If your hypothesis would have produced one of those and the user didn't mention it, the hypothesis is likely dead.
- If the hypothesis requires the user to have done something they didn't mention, ask with `AskUserQuestion` first. One direct yes/no question usually kills or confirms it in seconds.
- Hypotheses consistent with "the user did exactly what they said" are high-prior. Hypotheses that require unreported user input are low-prior.

**"The user must have pressed X by accident" is almost never the right answer, and it is never an acceptable first hypothesis without explicit confirmation.**

### 3. Integration tests for bugs must drive the game as a black box

The failing test that reproduces a bug is a claim about **external** behavior — what the user sees on screen, what they physically did to the controller or keyboard, and what the game responded with. The test must be written in those terms. It must not skip layers to reach the "obvious" internal mechanism.

**Concretely:**

- **Inputs go through the real input layer.** Build an `InputState` (or raise it through whatever top-level frame entry point tests use) and feed it to the frame loop. Do not call internal editor handlers, undo helpers, rule firings, or subsystem functions directly in a bug-repro test.
- **Outputs are observable game state.** Assert on things a player would see: entity positions, the current level name, attribute values, toast text, flags that gate progression. Do not assert on arena offsets, linked-list cursors, map bucket counts, or other internal plumbing.
- **The test must still fail if a "fix" touches the wrong layer.** If you can silence the test by renaming or no-opping an internal function without changing behavior at the keyboard→pixel boundary, the test is wired to the wrong layer. Bug-repro tests must only go green when the externally observable behavior changes.
- **The test must still pass after any refactor that preserves behavior.** Swapping out the undo strategy, restructuring the editor handlers, renaming internal functions — none of these should break a bug-repro test. If they do, the test was over-coupled.

**Heuristic while writing a bug-repro test:** write every step in the vocabulary of the bug report. "I pressed the arrow key" → the test presses the arrow key through the input layer. "The player snapped back" → the test asserts on the player's position. Any internal symbol in the test body (`undo_history_`, `gamedata_arena_`, `handle_*_input`) is a smell — you are testing the wrong layer.

If the input layer does not yet support driving the path headlessly (e.g. a binding reads raylib globals directly), the correct response is to **extend the test infrastructure** so it can, not to reach past the abstraction. Improving the integration test framework so black-box bug-repro tests are ergonomic is tracked as open work — see DESIGN.md § "Test ergonomics for black-box integration testing".

## Diagnostics: Logging and Error Handling

Two separate concerns, one header (`debug.h`):

- **Logging** — observational output for humans. "What happened." Fire-and-forget, no return value.
- **Errors** — values that propagate through the call stack for code to act on. "What went wrong and why."

### Logging (`debug_log`)

For events worth observing. Goes to stdout (timestamped), the in-game debug overlay (ring buffer), and the trace file. Not for errors — use the error system for failures.

**Zero Static State:** The logging system must use explicit context passing. We do not use a global `static FILE *trace_file`. To log an event, the function must be passed a `DebugState *` pointer: `debug_log(dbg, "loaded %d blueprints", count)`. This "context poisoning" is intentional to guarantee that hot-reloads leave no stale file pointers and headless tests can run in complete isolation.

### Error Handling

Go-style error propagation: every function that can fail reports *why* via its return value, callers cannot silently ignore the result, and only the top-level caller logs the chain.

**`[[nodiscard]]` on all fallible functions.** C23's `[[nodiscard]]` attribute makes clang emit a warning (promoted to error by our `WarningsAsErrors: '*'` config) when a caller discards the return value. This is the enforcement mechanism — equivalent to Go's "unused variable" error on the `err` return. To intentionally discard an error (rare, must be justified), use an explicit `(void)` cast — the same pattern we already use for stdio functions. This makes the decision visible in code review.

**Contextual error propagation (`error.h`).** The error chain must be stored in an explicit `ErrorState` struct, not a static buffer. We strictly avoid global singletons. The convention: the function that detects the failure calls `error_set` and returns `false`; intermediate callers call `error_wrap` and return `false`; only the top-level caller (game loop, test harness) logs the chain via `debug_log(dbg, "error: %s", error_get(err))`. The resulting message reads like `load_gamedata: level_load: fopen(/path): Permission denied`.

**API surface:**
- `error_set(err, format, ...)` — set the root error (clears any previous chain).
- `error_wrap(err, format, ...)` — prepend context to the existing error.
- `error_get(err)` — return the full error chain as a string.
- `error_clear(err)` — explicitly clear the error state.

**Migration:** existing functions are migrated incrementally. When touching a function that returns `bool` or a pointer, add `[[nodiscard]]` to its declaration, replace `debug_log` + `return false` with `error_set` + `return false`, and update callers to wrap with `error_wrap` instead of logging directly.

## Gamedata Sync Workflow

Two copies of `gamedata.toml` exist:
- **Repo:** `data/gamedata.toml` — versioned in git, read by the desktop game at runtime.
- **Syncthing:** `~/Sync/sleipner/gamedata.toml` — synced to Android via Syncthing, read by the Android game at runtime.

**Hard links do not work.** Syncthing's atomic write (temp file + rename) breaks hard links. The two files must be kept in sync by explicit copy.

### Editing gamedata — strict procedure

1. **Pull from Syncthing into repo** (pick up any in-game editor changes from Android):
   ```bash
   cp ~/Sync/sleipner/gamedata.toml data/gamedata.toml
   ```
2. **Diff and commit if needed.** If the Syncthing copy has changes, commit them to git before making further edits.
3. **Make changes** to `data/gamedata.toml` in the repo.
4. **Push from repo to Syncthing:**
   ```bash
   cp data/gamedata.toml ~/Sync/sleipner/gamedata.toml
   ```
5. **Commit the repo copy** so git tracks the final state.

The game engine creates `.bak` files automatically via `backup_file()` when saving gamedata on Android. Do not manually create `.bak` files.

### Conflict resolution

If `data/gamedata.toml` and `~/Sync/sleipner/gamedata.toml` have diverged (both were edited independently), **do not overwrite either file**. Instead:
1. Diff the two files: `diff data/gamedata.toml ~/Sync/sleipner/gamedata.toml`
2. Ask the user — the Syncthing copy likely has in-game editor changes they want to keep.
3. Merge manually, commit, then push to Syncthing.

### Runtime paths

The path to `gamedata.toml`, `keybindings.toml`, and `trace.log` is composed at runtime from `state->preferences.data_dir + filename` (see `gamedata_path` / `keybindings_path` / `trace_log_path` in `engine/src/main.c`). The directory itself is overridable from the in-game Settings → General tab. Defaults match the pre-prefs constants:

- **Desktop default:** `data_dir = "data/"`, so `data/gamedata.toml`, `data/keybindings.toml`, `data/trace.log`.
- **Android default:** `data_dir = "/storage/emulated/0/Sync/sleipner/"`, so `<that>/gamedata.toml` etc.

`preferences.toml` itself is NOT inside `data_dir` — that would be a chicken-and-egg cycle since `preferences.toml` is what overrides `data_dir`. It lives at the OS-conventional config path resolved by `engine/src/platform_paths.c`:

- Linux/BSD: `$XDG_CONFIG_HOME/sleipner/preferences.toml` (fallback `$HOME/.config/sleipner/preferences.toml`).
- Windows: `%APPDATA%/sleipner/preferences.toml`.
- Android: `<internalDataPath>/preferences.toml` (raylib `GetApplicationDirectory()`).
- A `<binary_dir>/preferences.toml` next to the executable trumps the OS path if it exists, for portable installs.

Trace.log opens twice: once at the boot-default path before preferences are loaded (so even pre-prefs failures get logged), then `debug_reopen_trace` switches to the resolved `data_dir`-based path once preferences load. Append mode preserves any boot-stage entries.

### Keybindings overlay

`keybindings.toml` is a **per-user overlay** written by the in-game Settings UI. Unlike `gamedata.toml` it is **not** versioned in git (`.gitignore` covers `data/keybindings.toml` and the `.bak` sibling). Built-in defaults in `engine/src/input_func.c` are the source of truth; the file overlays user customizations on top via `input_func_load_bindings_toml`.

- **Desktop:** `data/keybindings.toml` — only created after the user rebinds something via the Settings UI; absent file means defaults remain.
- **Android:** `/storage/emulated/0/Sync/sleipner/keybindings.toml` — synced via Syncthing the same way trace logs are. Schema is whatever `engine/src/input_func_toml.c` reads/writes.

Conflict resolution mirrors the gamedata workflow if both desktop and Android edited the file independently.

## Lint Discipline

- **Never assume warnings are false positives.** Treat every clang-tidy/clang-analyzer warning as a real issue. Exhaust all code-level fixes before even considering suppression. Hard proof is required that something is truly a false positive before adding any NOLINTNEXTLINE.
- **Avoid NOLINT comments.** Prefer fixing the code. NOLINTs are noise that hide real issues.
- **Never disable lint checks without asking.** Do not modify `.clang-tidy` Checks or add inline suppressions without explicit user approval.
### Known tricky checks (add new entries when a check has a non-obvious fix)
- `bugprone-easily-swappable-parameters` — Two adjacent parameters of the same type. Fix by reordering params, changing one to a different type (e.g. index instead of pointer), or wrapping in a struct.
- `performance-no-int-to-ptr` — Don't cast integers to pointers. Use index arithmetic or memcpy instead of `(Type *)(uintptr_t)value`.

## Toolchain Reference

- LLVM/Clang 22.1.0 (provided by `llvmPackages_22` in `flake.nix`)
- Clang Static Analyzer checkers: https://releases.llvm.org/22.1.0/tools/clang/docs/analyzer/checkers.html
- LLVM 22 release docs root: https://releases.llvm.org/22.1.0/
- Note: `ReportMode` for `security.insecureAPI.DeprecatedOrUnsafeBufferHandling` is NOT available in LLVM 22 — trunk-only feature (LLVM 23+).

## Game Design Notes

- **Input:** Controller-first. **Every feature must have a gamepad binding** — the user tests on Android with gamepad only, keyboard is unreachable there.
- **Performance:** Keep things light enough for mid-range phones — avoid heavy compute, prefer simple draw calls.
- **Assets:** Embedded in the binary via `.incbin` assembly directives. Each asset gets a `.S` file from CMake's `embed_asset()`. Loaded at runtime via raylib's memory-loading functions (`LoadImageFromMemory`, etc.). To add a new asset: an `embed_asset()` call in `engine/CMakeLists.txt`, a `DECLARE_ASSET()` in `assets.h`, and load with `ASSET(name)`.
- **Architecture:** All C code lives in `engine/`. There is no separate "game code" — the game is defined entirely by `data/gamedata.toml` and `assets/`. The engine interprets the game data at runtime.
- **Font Preview Panel:** Debug overlay (F4 / gamepad Right Thumb) shows every embedded font at 32px with sample text.

## Android APK Signing

Android requires update APKs to be signed with the same key as the original install. The project ships a tracked development keystore at `android/keystore.jks` (password `sleipner`, hardcoded in `.github/workflows/android.yml`) so every machine and CI run produces a compatible signature. Pre-alpha only — not a production-grade setup.

## Development Discipline

- **Every feature touches the full picture.** When adding new functionality, always consider what existing code needs to be modified, refactored, or removed. Don't just bolt on — integrate. A feature isn't done until dead code is removed and affected subsystems are updated.
- **Continuous refactoring.** Refactor as you go, not as a separate pass. If adding a feature reveals that an existing function does too much, split it now. If a struct gains a field that makes an old field redundant, remove the old field now.
- **Keep the delta clear.** DESIGN.md tracks what is designed. The roadmap in DESIGN.md tracks what is actually implemented. When completing a feature, update the roadmap in the same commit. The gap between "designed" and "implemented" should always be visible and accurate.
- **Remove before adding.** Before writing new code, check if existing code already handles part of the task, or if existing code will become dead after the change. Remove or update it first, then add the new code. This prevents accumulation of unused code paths.
- **Reset state on initialization.** Be vigilant about resetting counters, registries, and state arrays during game initialization. Failure to reset can cause bugs across game restarts (e.g., the font preview bug where fonts appeared twice because `font_preview_count` wasn't reset). Audit initialization code when adding new stateful features.

- **Minimize static data.** Strive for zero static variables. Use explicit state passing and holder structs instead. Global state should not exist, not even for logging, error handling, or registries. Any state must live in a holder struct (like `GameState`, `ErrorState`, or `DebugState`) that is explicitly passed to functions that need it. If you find yourself reaching for a `static` variable or a global array, restructure the architecture to pass a context pointer instead.
- **One subsystem at a time.** Implement features incrementally, one subsystem at a time. Get it working, tested, and integrated before moving to the next. Don't build multiple half-finished subsystems in parallel.

## Git Workflow

- **Always commit and push when you're done with a task.** Do not wait to be asked — committing and pushing is part of completing the work. This applies to all changes, including documentation updates, unless explicitly instructed otherwise.
- Create small, focused commits as you go so changes are easy to review and revert.
- Each commit should address a single concern (one bug fix, one feature, one refactor).
- Use a succinct imperative commit title (e.g. "Add player dash mechanic").
- Include gotchas, caveats, or non-obvious side effects in the commit message body.
- Never add "Co-Authored-By" lines or email addresses to commit messages.
- Push freely without asking, but never use `git push --force` or any force-push variant.
- **Keep all documentation up to date.** When changing behavior, update CLAUDE.md and code comments in the same commit. Stale docs are worse than no docs.
- **Run `nix develop -c clang-format -i engine/src/*.c engine/src/*.h engine/test/*.c` before committing.** Always auto-format code before creating commits to avoid CI failures from clang-format violations. The formatter handles line wrapping, indentation, and other style rules automatically.
- **Run clang-tidy before committing.** `nix develop -c bash -c "cd build/Release && clang-tidy -p . $(ls ../../engine/src/*.c ../../engine/test/*.c | grep -v arena_win32)"`. The most common lint failure is `misc-include-cleaner` — if you use a type or function, its providing header must be directly included, not reached transitively. Fix lint errors before committing; do not push code that fails lint.

## Claude Code Guidelines

- **Only do what was asked.** Never carry out unrequested changes — no bundling extra fixes, no proactively addressing future improvements. If something seems worth doing, ask first.
- **Pre-alpha: weigh options on technical merit, not continuity.** No external users depend on stable UX, so "muscle memory", "convention familiarity", "established workflows" and similar are not decision criteria. Equally, "it's just pre-alpha, keep it simple" is not a technical argument. Decide on clarity, footprint, conflict-freeness, idiomatic fit, and architectural value. When only continuity-style reasons separate two options, treat them as equivalent and pick the simpler implementation.
- **Use `nix develop` for tooling.** Don't install compilers, libraries, or build tools globally — if something's missing, add it to `flake.nix`. Use `nix develop .#windows` or `nix develop .#android` for the cross-compile shells.
- **Never run cmake, clang-tidy, cppcheck, etc. outside a Nix shell.** Always prefix with `nix develop -c ...` (or enter the shell first) so the toolchain is reproducible.
- **Skip local builds for small/trivial changes.** Let GitHub Actions CI catch issues instead — time is precious. Only run locally when the change is non-trivial.
- **Log long-running commands to `tmp/`.** Redirect output of long builds to log files: `nix develop -c cmake --build build/Release > tmp/build.log 2>&1`, then read/tail the log. Use descriptive names: `tmp/test.log`, `tmp/lint.log`, `tmp/build.log`.
- **No comments in bash commands.** Use the Bash tool's `description` field for context, not inline `#` comments.
- **Don't use `-C` or `cd` unnecessarily.** If already in the right working directory, don't pass `-C` flags to git or other commands. Equally, never use `cd` to move around the active session — prefer subshells `(cd foo && make)` or `-C` flags only when the target directory differs from the current working directory.
- **Read documentation before probing.** Use WebFetch/WebSearch to read library docs rather than running exploratory commands in containers.
- **TODO.md workflow.** After completing a task, first check GitHub Actions CI — list all workflows with `gh workflow list` and check the latest run of each with `gh run list --workflow <name> --limit 1`. Use `gh run view` on any failures and present them as items to address. Then check `TODO.md` for pending items. If there are items, ask the user if they want to work through them. One item at a time, one commit per item, remove items as they're done, keep the file.
- **Keep `TODO.md` current as you work, both directions.** After every task, re-read `TODO.md` end-to-end and reconcile it against the change just shipped:
  - **Add** any tech debt, hiccup, deferred polish, known gap, or surprising constraint that came up during the work and is not worth fixing in scope. Cite the relevant file/line and the reason it was deferred so the bullet is actionable later.
  - **Remove** any bullet the change resolved, even unintentionally (e.g. a binding conflict that disappeared as a side effect of unrelated work). Resolved bullets stay misleading otherwise.
  - **Update** any bullet whose scope or premise has shifted (e.g. cited file paths or function names that moved).
  - Bundle the `TODO.md` edit into the same commit as the change when the change resolves a bullet; use a separate small commit when the change only adds or restructures bullets. Never let `TODO.md` drift more than one commit behind reality.
- **Memories go in CLAUDE.md.** Store all project instructions, feedback, and conventions in this file (versioned in git), not in `~/.claude/` hidden memory files. Exception: if the user explicitly asks for "user local" memory, write to `~/.claude/projects/-home-tubbles-dev-sleipner/memory/`.

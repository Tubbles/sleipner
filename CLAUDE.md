# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**IMPORTANT:** This document applies to all LLMs working on this project. However, only Mistral LLM (devstral-2) shall also read and follow CONTRIBUTING.md.

## Project Overview

Sleipner is a top-down Zelda-like action RPG written in C using raylib. The game is controller-driven and targets Linux x86_64. It is designed to also run on Android phones via the "Game Native" app, which uses FEX (x86_64 emulation) and Proton under the hood — so the build target is a standard Linux binary, not an Android NDK build.

**MANDATORY READING:** `DESIGN.md` contains the game design specification and implementation roadmap. Read it before making any changes to understand the intended architecture and feature set.

## Building

The project uses a containerized toolchain (Podman/Docker) for reproducible builds. All build steps run via `ci.sh`, which wraps `podman run` against the toolchain image.

```bash
# Full pipeline: format check + build + test + lint
./ci.sh all

# Individual steps
./ci.sh format    # Auto-format source files in-place
./ci.sh check     # Check formatting (dry-run, fails on violations)
./ci.sh build     # Install deps + compile
./ci.sh test      # Run unit tests
./ci.sh lint      # Run clang-tidy

# Native build (if no container runtime available)
conan install . --output-folder=build --build=missing
conan build .

# Run
./build/Release/engine/sleipner
```

## Dependencies

- C23 compiler (clang-22 in container)
- Conan 2 (package manager — drives CMake)
- CMake (generated/invoked by Conan)
- raylib (graphics, input, audio)
- Unity (ThrowTheSwitch — unit test framework)
- fff.h (Fake Function Framework — mocking)

## Coding Style

- **No OOP patterns.** This is C — think plain structs and functions.
- **Small, focused functions.** Aim for 5-10 lines per function. Extract logic into named helpers rather than writing long functions.
- **Pure functions where possible.** Functions should take inputs, return outputs, and avoid side effects. Side effects (I/O, rendering, audio) should be pushed to the edges — thin wrapper functions that call pure logic.
- **Data-oriented design.** Game state is plain structs. Logic operates on that data. Data flow is one-directional: input -> state -> render.
- **Test everything with Unity + fff.h.** Every non-trivial pure function should have corresponding tests in `test/`. If a function is hard to test, it probably does too much.
- **Full descriptive names always.** No single-letter variables anywhere, including loop counters (`i` → `index`, `j` → `next`). No small abbreviations either (`pt` → `particle`, `dx` → `delta_x`, `wp` → `world_pos`). The codebase should be self-documenting through clear naming.
- **Vendor libraries go in `engine/vendor/`.** Not the top-level `vendor/`.
- **Avoid forward declarations.** Prefer including actual headers to make dependencies explicit and maintain module integrity. Forward declarations can hide circular dependencies and break the "code module" concept. If circular dependencies exist, refactor to eliminate them rather than hiding them.

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
- **Every new feature ships with tests.** Unit tests for the pure logic, integration tests for the subsystem interaction. A feature is not done until its tests are written.
- **Tests document behavior.** Integration test scenarios serve as executable documentation of how the game systems work together.
- **Test the interesting cases.** Don't test trivial getters. Test state transitions, edge cases, rule interactions, and anything that has broken before.
- **Regression tests for bugs.** Every bug gets an integration-level regression test that reproduces the failure before the fix is applied.

## Diagnostics: Logging and Error Handling

Two separate concerns, one header (`debug.h`):

- **Logging** — observational output for humans. "What happened." Fire-and-forget, no return value.
- **Errors** — values that propagate through the call stack for code to act on. "What went wrong and why."

### Logging (`debug_log`)

For events worth observing: `debug_log("loaded %d blueprints", count)`. Goes to stdout (timestamped), the in-game debug overlay (ring buffer), and the trace file. Not for errors — use the error system for failures.

### Error Handling

Go-style error propagation: every function that can fail must report *why* it failed, and callers must not be able to silently ignore errors.

**Principles:**

1. **Errors are values, not side effects.** A function that can fail communicates failure through its return value, not by logging and returning void.
2. **Callers must handle errors.** It should be a compile error to ignore a fallible return value.
3. **Errors carry context.** When propagating an error up the call stack, each layer adds context — like Go's `fmt.Errorf("load level: %w", err)`. The final error message reads as a chain: `load_gamedata: level_load: blueprint 'player' not found`.
4. **Log at the boundary, not at the source.** The function that *detects* the error sets it. Intermediate callers wrap it. Only the top-level caller (game loop, test harness) logs it via `debug_log`. This avoids duplicate log spam and keeps inner functions pure.

**`[[nodiscard]]` on all fallible functions.** C23's `[[nodiscard]]` attribute makes clang emit a warning (promoted to error by our `WarningsAsErrors: '*'` config) when a caller discards the return value. This is the enforcement mechanism — equivalent to Go's "unused variable" error on the `err` return.

```c
[[nodiscard]] bool level_load(Level *level, ...);
```

To intentionally discard an error (rare, must be justified), use an explicit `(void)` cast — the same pattern we already use for stdio functions. This makes the decision visible in code review.

**Global error context module (`error.h`).** A singleton that holds the current error chain. Since the engine is single-threaded, a static buffer is sufficient. If multi-threading is added later, this extends naturally to an error context handle that callers pass around — the call-site API shape stays the same.

```c
// At the point of failure — set the root cause:
error_set("fopen(%s): %s", path, strerror(errno));
return false;

// Intermediate caller — wrap with context:
if (!level_load(&level, ...)) {
    error_wrap("load_gamedata");
    return false;
}

// Top-level caller — log the full chain:
if (!game_load_gamedata(&state, params)) {
    debug_log("error: %s", error_get());
    // prints: "load_gamedata: level_load: fopen(/path): Permission denied"
}
```

**API surface:**
- `error_set(format, ...)` — set the root error (clears any previous chain).
- `error_wrap(format, ...)` — prepend context to the existing error.
- `error_get()` — return the full error chain as a string.
- `error_clear()` — explicitly clear the error state.

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

- **Desktop:** `data/gamedata.toml` (repo-relative, from working directory).
- **Android:** `/storage/emulated/0/Sync/sleipner/gamedata.toml` (hardcoded).
- **Trace log (Android):** `/storage/emulated/0/Sync/sleipner/trace.log` — readable from desktop via Syncthing at `~/Sync/sleipner/trace.log`.
- **Trace log (desktop):** `trace.log` in the working directory.

## Lint Discipline

- **Never assume warnings are false positives.** Treat every clang-tidy/clang-analyzer warning as a real issue. Exhaust all code-level fixes before even considering suppression. Hard proof is required that something is truly a false positive before adding any NOLINTNEXTLINE.
- **Avoid NOLINT comments.** Prefer fixing the code (e.g. explicit `(bool)` casts for C's int-returning `&&`/`||` operators). NOLINTs are noise that hide real issues.
- **Never disable lint checks without asking.** Do not modify `.clang-tidy` Checks or add inline suppressions without explicit user approval.
- **Keep a list of tricky checks.** When encountering a lint check that requires a non-obvious fix pattern, document the check name and the fix in this section so it's available for future reference. Keep adding to this list over time.

### Known tricky checks
- `bugprone-easily-swappable-parameters` — Two adjacent parameters of the same type. Fix by reordering params, changing one to a different type (e.g. index instead of pointer), or wrapping in a struct.
- `performance-no-int-to-ptr` — Don't cast integers to pointers. Use index arithmetic or memcpy instead of `(Type *)(uintptr_t)value`.
- Implicit boolean conversion — C's `&&`/`||` return int, not bool. Use explicit `(bool)` cast when assigning to a bool field.

## Toolchain Reference

- LLVM/Clang 22.1.0 (installed via apt.llvm.org llvm.sh script in Dockerfile)
- Clang Static Analyzer checkers: https://releases.llvm.org/22.1.0/tools/clang/docs/analyzer/checkers.html
- LLVM 22 release docs root: https://releases.llvm.org/22.1.0/
- Note: `ReportMode` for `security.insecureAPI.DeprecatedOrUnsafeBufferHandling` is NOT available in LLVM 22 — trunk-only feature (LLVM 23+).

## Game Design Notes

- **Genre:** Top-down action RPG in the style of classic Zelda (Link to the Past / Link's Awakening).
- **Input:** Controller-first. Gamepad is the primary input method; keyboard fallback for development. **Every feature must have a gamepad binding** — the user tests on Android with gamepad only, keyboard is unreachable there.
- **Mobile target:** The game runs on Android via Game Native (FEX + Proton). This means we build a normal Linux x86_64 binary — no Android-specific code. Keep performance reasonable for emulated execution (avoid heavy compute, prefer simple draw calls).
- **Assets:** Embedded in the binary via `.incbin` assembly directives. Each asset gets a `.S` file generated by CMake's `embed_asset()` function. The assembler copies raw bytes into `.rodata` — no C parsing overhead, scales to many assets. Loaded at runtime via raylib's memory-loading functions (`LoadImageFromMemory`, `LoadFontFromMemory`, `LoadMusicStreamFromMemory`). To add a new asset: add an `embed_asset()` call in `engine/CMakeLists.txt`, a `DECLARE_ASSET()` in `assets.h`, and load with `ASSET(name)`.
- **Architecture:** All C code lives in `engine/`. There is no separate "game code" — the game is defined entirely by `data/gamedata.toml` and `assets/`. The engine interprets the game data at runtime.
- **Font Preview Panel:** Debug overlay (toggle with F4/gamepad Right Thumb) shows all embedded fonts at 32px size with sample text "The quick brown fox 0123456789". Includes: Earth Illusion, Golden Apple, MenuCard, Nudge Orb, CardboardCrown, and RoyalFibre fonts.

## Android APK Signing

Android requires APK updates to be signed with the same key as the original installation. The project uses a development keystore (`android/keystore.jks`) for consistent signing:

- **Keystore location:** `android/keystore.jks`
- **Password:** `sleipner` (hardcoded in ci.sh for development)
- **Keystore tracking:** The keystore is tracked in git to ensure consistent signing across different machines and CI runs.

**Important:** This development keystore uses a public password and is suitable only for development builds. For production releases:
- Generate a new keystore with a strong, unique password
- Use proper secret management (e.g., GitHub Secrets)
- Never commit production keystores or passwords to version control

## Development Discipline

- **Every feature touches the full picture.** When adding new functionality, always consider what existing code needs to be modified, refactored, or removed. Don't just bolt on — integrate. A feature isn't done until dead code is removed and affected subsystems are updated.
- **Continuous refactoring.** Refactor as you go, not as a separate pass. If adding a feature reveals that an existing function does too much, split it now. If a struct gains a field that makes an old field redundant, remove the old field now.
- **Keep the delta clear.** DESIGN.md tracks what is designed. The roadmap in DESIGN.md tracks what is actually implemented. When completing a feature, update the roadmap in the same commit. The gap between "designed" and "implemented" should always be visible and accurate.
- **Remove before adding.** Before writing new code, check if existing code already handles part of the task, or if existing code will become dead after the change. Remove or update it first, then add the new code. This prevents accumulation of unused code paths.
- **Reset state on initialization.** Be vigilant about resetting static variables, counters, and state arrays during game initialization. Failure to reset can cause bugs across game restarts (e.g., the font preview bug where fonts appeared twice because `font_preview_count` wasn't reset). Audit initialization code when adding new stateful features.

- **Minimize static data.** Strive for zero static variables. Use explicit state passing and holder structs instead. Static variables should be extremely rare exceptions, not the norm. When static data is truly unavoidable (e.g., a one-time platform registration that has no caller-owned home), it must have a matching reset function called during game init, and its lifetime must be clearly documented. This is a last resort — if you find yourself reaching for a static, first ask whether it can live in an existing holder struct.

### Singleton Pattern for Global Systems

For true singletons (global systems used across many modules), use a **static pointer to externally owned state**:

- **Pattern**: Owner module declares state struct, passes pointer to singleton module via init function
- **Key insight**: Singleton module uses single static pointer to avoid global variables while maintaining single instance
- **Examples**: Debug logging, asset manager, sound engine, input system

**Best practice**: Wrap singleton state in a struct to minimize global data surface:

```c
// In debug.h:
typedef struct {
    char log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
    int log_head;
    int log_count;
    FILE *trace_file;
} DebugState;

// In main.c (owner):
static DebugState debug_state;
debug_init(&debug_state, TRACE_LOG_PATH);

// In debug.c (singleton):
static DebugState *debug_state = NULL;
void debug_init(DebugState *state, const char *trace_path) {
    debug_state = state;
    // Initialize...
}
```

This approach makes the singleton easier to refactor and test by reducing the global data surface from multiple variables to a single struct pointer.

- **Prefer pure functions.** Functions should take inputs and return outputs without relying on or modifying static state. Use holder structs to group related data and pass them explicitly rather than using global variables.
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
- **Run `./ci.sh format` before committing.** Always auto-format code before creating commits to avoid CI failures from clang-format violations. The formatter handles line wrapping, indentation, and other style rules automatically.

## Claude Code Guidelines

Rules for how Claude Code should operate in this project. Keep adding to this list as new patterns emerge.

- **Only do what was asked.** Never carry out unrequested changes — no bundling extra fixes, no proactively addressing future improvements. If something seems worth doing, ask first.
- **Use the project Dockerfile for tooling.** Don't spin up ad-hoc containers — if a tool is needed, it should be in the existing Dockerfile or added to it.
- **Always use `./ci.sh`** without specifying `CONTAINER_CMD`. The script auto-detects Docker/Podman. Never run conan/cmake commands directly.
- **Skip `ci.sh` for small/trivial changes.** Let GitHub Actions CI catch issues instead — time is precious. Only run locally when the change is non-trivial.
- **Log long-running commands to `tmp/`.** Redirect output of commands like `./ci.sh` to log files: `./ci.sh test > tmp/test.log 2>&1`, then read/tail the log. Use descriptive names: `tmp/test.log`, `tmp/lint.log`, `tmp/build.log`.
- **No comments in bash commands.** Use the Bash tool's `description` field for context, not inline `#` comments.
- **Don't use `-C` or `cd` unnecessarily.** If already in the right working directory, don't pass `-C` flags to git or other commands. Equally, never use `cd` to move around the active session — prefer subshells `(cd foo && make)` or `-C` flags only when the target directory differs from the current working directory.
- **Read documentation before probing.** Use WebFetch/WebSearch to read library docs rather than running exploratory commands in containers.
- **TODO.md workflow.** After completing a task, check `TODO.md` for pending items. If there are items, ask the user if they want to work through them. One item at a time, one commit per item, remove items as they're done, keep the file.
- **Memories go in CLAUDE.md.** Store all project instructions, feedback, and conventions in this file (versioned in git), not in `~/.claude/` hidden memory files. Exception: if the user explicitly asks for "user local" memory, write to `~/.claude/projects/-home-tubbles-dev-sleipner/memory/`.

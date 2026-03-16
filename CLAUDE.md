# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Sleipner is a top-down Zelda-like action RPG written in C using raylib. The game is controller-driven and targets Linux x86_64. It is designed to also run on Android phones via the "Game Native" app, which uses FEX (x86_64 emulation) and Proton under the hood — so the build target is a standard Linux binary, not an Android NDK build.

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
./build/Release/sleipner
```

## Dependencies

- C23 compiler (clang-22 in container, for `#embed` support)
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

## Game Design Notes

- **Genre:** Top-down action RPG in the style of classic Zelda (Link to the Past / Link's Awakening).
- **Input:** Controller-first. Gamepad is the primary input method; keyboard fallback for development.
- **Mobile target:** The game runs on Android via Game Native (FEX + Proton). This means we build a normal Linux x86_64 binary — no Android-specific code. Keep performance reasonable for emulated execution (avoid heavy compute, prefer simple draw calls).
- **Assets:** Embedded in the binary via C23 `#embed` for portability — single-binary distribution with no external asset files.

## Development Discipline

- **Every feature touches the full picture.** When adding new functionality, always consider what existing code needs to be modified, refactored, or removed. Don't just bolt on — integrate. A feature isn't done until dead code is removed and affected subsystems are updated.
- **Continuous refactoring.** Refactor as you go, not as a separate pass. If adding a feature reveals that an existing function does too much, split it now. If a struct gains a field that makes an old field redundant, remove the old field now.
- **Keep the delta clear.** DESIGN.md tracks what is designed. The roadmap in DESIGN.md tracks what is actually implemented. When completing a feature, update the roadmap in the same commit. The gap between "designed" and "implemented" should always be visible and accurate.
- **Remove before adding.** Before writing new code, check if existing code already handles part of the task, or if existing code will become dead after the change. Remove or update it first, then add the new code. This prevents accumulation of unused code paths.
- **One subsystem at a time.** Implement features incrementally, one subsystem at a time. Get it working, tested, and integrated before moving to the next. Don't build multiple half-finished subsystems in parallel.

## Git Workflow

- **Always commit and push when you're done with a task.** Do not wait to be asked — committing and pushing is part of completing the work.
- Create small, focused commits as you go so changes are easy to review and revert.
- Each commit should address a single concern (one bug fix, one feature, one refactor).
- Use a succinct imperative commit title (e.g. "Add player dash mechanic").
- Include gotchas, caveats, or non-obvious side effects in the commit message body.
- Never add "Co-Authored-By" lines or email addresses to commit messages.
- Push freely without asking, but never use `git push --force` or any force-push variant.
- **Keep all documentation up to date.** When changing behavior, update CLAUDE.md and code comments in the same commit. Stale docs are worse than no docs.

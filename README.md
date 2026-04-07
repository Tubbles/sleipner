# Sleipner

Top-down action RPG in the style of classic Zelda, written in C with raylib.

## Features

- Controller-first input (gamepad primary, keyboard fallback)
- Data-driven engine: game defined entirely by `data/gamedata.toml` and `assets/`
- Blueprint/entity system with attribute inheritance
- SAT-based 2D collision detection (circles, rectangles, triangles, composite shapes)
- Particle system with pooled allocation
- Hot-reload of gamedata during play
- In-game editor with full gamepad UX (no keyboard required)
- Runs on Android via [Game Native](https://play.google.com/store/apps/details?id=com.nicolefeelsgood.gamenative) (FEX + Proton)

## Building

Requires [Podman](https://podman.io/) or Docker. All build steps run inside a containerized toolchain (Debian Bookworm + clang-22).

```bash
./ci.sh all       # format check + build + test + lint
./ci.sh format    # auto-format source files in-place
./ci.sh build     # install deps + compile
./ci.sh test      # run unit tests
./ci.sh lint      # run clang-tidy

./build/Release/engine/sleipner   # run the game
```

Native build (without a container runtime):

```bash
conan install . --output-folder=build --build=missing
conan build .
```

## Toolchain

- **Compiler:** clang-22
- **Build system:** CMake, driven by Conan 2
- **Linting:** clang-tidy with strict checks (all checks enabled, warnings as errors)
- **Formatting:** clang-format (LLVM-based style)
- **Testing:** Unity (ThrowTheSwitch) + fff.h (Fake Function Framework)

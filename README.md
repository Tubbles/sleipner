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

Requires [Nix](https://nixos.org/download/) with flakes enabled. The toolchain (clang-22, cmake, ninja, cppcheck, X11/GL deps, plus mingw-w64 and Android NDK cross shells) is defined in `flake.nix`.

```bash
nix develop                                                     # native shell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
ctest --test-dir build/Release --output-on-failure

./build/Release/engine/sleipner                                 # run the game
```

Cross-compile targets:

```bash
# Windows .exe (mingw-w64)
nix develop .#windows -c cmake -S . -B build/windows -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
    -DCMAKE_BUILD_TYPE=Release
nix develop .#windows -c cmake --build build/windows

# Android APK (NDK + Gradle)
nix develop .#android -c bash -c 'cd android && gradle wrapper --gradle-version 8.11.1 && ./gradlew assembleRelease'
```

## Toolchain

- **Compiler:** clang-22 (via `llvmPackages_22` in the flake)
- **Build system:** CMake (invoked directly, no package manager layer)
- **Vendored libraries:** raylib, Unity, fff, tomlc99 under `engine/vendor/`
- **Linting:** clang-tidy with strict checks (all checks enabled, warnings as errors)
- **Formatting:** clang-format (LLVM-based style)
- **Testing:** Unity (ThrowTheSwitch) + fff.h (Fake Function Framework)

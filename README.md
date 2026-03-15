# Sleipner

Top-down action RPG in the style of classic Zelda, written in C23 with raylib.

## Features

- Controller-first input (gamepad primary, keyboard fallback)
- SAT-based 2D collision detection (circles, rectangles, triangles, composite shapes)
- Particle system with pooled allocation
- Procedural audio synthesis (tones and pops)
- Single-binary distribution via C23 `#embed` (all assets compiled in)
- Runs on Android via [Game Native](https://play.google.com/store/apps/details?id=com.nicolefeelsgood.gamenative) (FEX + Proton)

## Building

Requires [Podman](https://podman.io/) or Docker. All build steps run inside a containerized toolchain (Debian Bookworm + clang-22).

```bash
./ci.sh all       # format check + build + test + lint
./ci.sh format    # auto-format source files in-place
./ci.sh build     # install deps + compile
./ci.sh test      # run unit tests
./ci.sh lint      # run clang-tidy

./build/Release/sleipner   # run the game
```

Native build (without a container runtime):

```bash
conan install . --output-folder=build --build=missing
conan build .
```

## Toolchain

- **Compiler:** clang-22 (C23 with `#embed` support)
- **Build system:** CMake, driven by Conan 2
- **Linting:** clang-tidy with strict checks (clang-analyzer, bugprone, cert, misc, performance, portability, readability)
- **Formatting:** clang-format (LLVM-based style)
- **Testing:** Unity (ThrowTheSwitch) + fff.h (Fake Function Framework)

## Known improvements

- **RNG:** Currently uses `rand()`/`srand()` for particle effects. A proper PRNG (e.g. xoshiro256) would improve quality. When done, re-enable the disabled clang-tidy checks: `bugprone-random-generator-seed`, `cert-msc30-c`, `cert-msc32-c`, `cert-msc50-cpp`, `cert-msc51-cpp`, `concurrency-mt-unsafe`, `misc-predictable-rand`.
